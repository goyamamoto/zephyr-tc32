/*
 * USB device controller driver (UDC API) for the Telink TLSR8278.
 *
 * DS-TLSR8278 describes the USB block's endpoints (7.6), its interrupt
 * sources (6.2.1), the DP pull-up (afe_0x0b bit 7) and the USB wakeup (0x6e),
 * but not the controller's own registers (0x800100-0x80013f); the comments
 * give what the driver writes, reads and waits for.
 *
 * Control endpoint: an 8-byte FIFO serviced by software. Each stage waits
 * for the driver's handshake in the control register: DAT_ACK or DAT_STALL
 * after a SETUP or a data packet, STA_ACK or STA_STALL after the status
 * interrupt. The driver writes that handshake when the stack has answered:
 *  - SETUP: the 8 bytes go to the stack; its data IN buffer (first packet
 *    written, DAT_ACK), data OUT buffer (DAT_ACK), status IN buffer (DAT_ACK
 *    for a request without data) or halt (DAT_STALL) arms the next stage;
 *  - IN data: each data interrupt means the host took a packet; the next
 *    one is written and armed; after the last, an empty FIFO is armed;
 *  - OUT data: each packet is read into the data buffer (its size follows
 *    from wLength); the last one goes to the stack, and its status IN buffer
 *    (DAT_ACK) or halt (DAT_STALL) answers it;
 *  - status: STA_ACK completes the status buffer the stack queued.
 *
 * The controller itself answers SET_ADDRESS, SET/GET_INTERFACE, GET_STATUS,
 * SET/CLEAR_FEATURE and SYNCH_FRAME: the driver hands only standard,
 * descriptor and configuration requests to software (0x104 bits 7, 5, 1
 * cleared) and never writes the address. So that the stack's device state
 * follows the controller's, the driver hands the stack two requests the
 * stack does not see: SET_ADDRESS once the stack has handled a bus reset,
 * and SET/CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP) when the controller's remote
 * wakeup flag (0x10a bit 2) changes. Their status stage is completed without
 * touching the controller.
 *
 * Data endpoints: 1, 2, 3, 4, 7, 8 are IN, 5 and 6 are OUT; register slot is
 * (ep & 7), so endpoint 8 uses slot 0. Buffers come from the 256-byte USB
 * RAM in 8-byte units. A slot's control register takes the busy bit and the
 * data PID the driver chooses (DATA0 after enable and clear halt, then
 * alternating); the controller clears busy when the host has taken an IN
 * packet or delivered an OUT packet, and sets the slot's bit in 0x139.
 *
 * Events: the controller's interrupt lines (CONFIG_UDC_TELINK_B87_IRQ_LINES)
 * and a 1 ms poll (a k_timer, or the board's timer with
 * CONFIG_UDC_TELINK_B87_EXTERNAL_POLL) run the same handlers in interrupt
 * context, each acting only on status bits it then clears. Bus suspend (the
 * level usb_pwdn, 0x648 bit 3) and the remote wakeup flag are seen by the
 * poll only. The SETUP bytes reach the stack from a work item
 * (udc_setup_received() needs thread context); everything else completes
 * from the interrupt or poll.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_usbd

#include "udc_common.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/drivers/usb/udc_b87.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_ch9.h>
#if !defined(CONFIG_UDC_TELINK_B87_SIM)
#include <tlsr827x/irq.h> /* tlsr827x_irq_clear_parent() */
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(udc_b87, CONFIG_UDC_DRIVER_LOG_LEVEL);

#if defined(CONFIG_UDC_TELINK_B87_SIM)
/* Host tests: a register model provides these (see UDC_TELINK_B87_SIM). */
uint8_t tlsr_usb_sim_rd8(uintptr_t addr);
void tlsr_usb_sim_wr8(uintptr_t addr, uint8_t value);
#define RD8(addr)        tlsr_usb_sim_rd8(addr)
#define WR8(addr, value) tlsr_usb_sim_wr8((addr), (value))
#else
#define RD8(addr)        (*(volatile uint8_t *)(uintptr_t)(addr))
#define WR8(addr, value) (*(volatile uint8_t *)(uintptr_t)(addr) = (value))
#endif
#define SET8(addr, mask) WR8((addr), (uint8_t)(RD8(addr) | (mask)))
#define CLR8(addr, mask) WR8((addr), (uint8_t)(RD8(addr) & ~(mask)))

#define USB_BASE DT_INST_REG_ADDR(0)

/* Control endpoint */
#define EP0_PTR        (USB_BASE + 0x00)
#define EP0_DATA       (USB_BASE + 0x01)
#define EP0_CTRL       (USB_BASE + 0x02)
#define EP0_IRQ_STATUS (USB_BASE + 0x03)
#define EP0_IRQ_MODE   (USB_BASE + 0x04)

#define EP0_CTRL_DATA_ACK     BIT(0)
#define EP0_CTRL_DATA_STALL   BIT(1)
#define EP0_CTRL_STATUS_ACK   BIT(2)
#define EP0_CTRL_STATUS_STALL BIT(3)

#define EP0_IRQ_SETUP        BIT(4)
#define EP0_IRQ_DATA         BIT(5)
#define EP0_IRQ_STATUS_STAGE BIT(6)
#define EP0_IRQ_ALL          (EP0_IRQ_SETUP | EP0_IRQ_DATA | EP0_IRQ_STATUS_STAGE)

#define EP0_AUTO_CFG  BIT(1)
#define EP0_AUTO_DESC BIT(5)
#define EP0_AUTO_STD  BIT(7)

/* Remote wakeup enabled by the host (set by the controller, not documented) */
#define USB_REG_0A       (USB_BASE + 0x0a)
#define USB_REG_0A_RWUP  BIT(2)

/* Data endpoints; i is the register slot (ep & 7) */
#define EP_ENABLE      (USB_BASE + 0x0e)
#define EP_PTR(i)      (USB_BASE + 0x10 + (i))
#define EP_DATA(i)     (USB_BASE + 0x18 + (i))
#define EP_CTRL(i)     (USB_BASE + 0x20 + (i))
#define EP_BUF_ADDR(i) (USB_BASE + 0x28 + (i))
#define EP_IRQ         (USB_BASE + 0x39)
#define EP_IRQ_MASK    (USB_BASE + 0x3a)
#define EP_MAX_SIZE    (USB_BASE + 0x3e)

#define EP_CTRL_BUSY  BIT(0)
#define EP_CTRL_STALL BIT(1)
#define EP_CTRL_DATA0 BIT(2)
#define EP_CTRL_DATA1 BIT(3)

/* Outside the USB block */
#define CLKEN0                  0x00800063
#define CLKEN0_USB              BIT(3)
#define GPIO_PA_IE              0x00800581
#define GPIO_PA_GPIO            0x00800586
#define USB_PINS_PA             (BIT(5) | BIT(6)) /* PA5 = DM, PA6 = DP */
/* usb_reset (DS-TLSR8278 6.2.1), an edge source; the poll reads it unmasked */
#define IRQ_SOURCE_B2           0x0080064a
#define IRQ_SOURCE_B2_USB_RESET BIT(1)
/* usb_pwdn (DS-TLSR8278 6.2.1), a level: the host keeps the bus suspended */
#define IRQ_SOURCE_B0           0x00800648
#define IRQ_SOURCE_B0_USB_PWDN  BIT(3)
#define WAKEUP_EN               0x0080006e /* DS-TLSR8278 Table 2-7 */
#define WAKEUP_EN_USB           BIT(2)
#define WAKEUP_EN_USB_RESUME    BIT(6)

/* Analog registers */
#define ANALOG_PORT_ADDR  0x008000b8
#define ANALOG_PORT_DATA  0x008000b9
#define ANALOG_PORT_CTRL  0x008000ba
#define ANALOG_PORT_BUSY  BIT(0)
#define ANALOG_PORT_WRITE BIT(5)
#define ANALOG_PORT_START BIT(6)
#define ANA_USB_DP_PULLUP 0x0b /* bit 7: 1.5 kOhm pull-up on DP */
#define ANA_USB_POWER     0x34 /* bit 1 (not documented): cleared to power USB */

#define EP0_MPS      8U
#define EP_MPS       64U
#define USB_RAM_UNIT 8U
#define NUM_EP_SLOTS 8U
#define EP_IN_MASK   (BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(7) | BIT(8))
#define EP_OUT_MASK  (BIT(5) | BIT(6))
#define NUM_IN_EPS   7U /* EP0 and the six IN endpoints */
#define NUM_OUT_EPS  3U /* EP0 and the two OUT endpoints */

#define IRQ_EP0_SETUP  DT_INST_IRQ_BY_NAME(0, ep0_setup, irq)
#define IRQ_EP0_DATA   DT_INST_IRQ_BY_NAME(0, ep0_data, irq)
#define IRQ_EP0_STATUS DT_INST_IRQ_BY_NAME(0, ep0_status, irq)
#define IRQ_EP_DATA    DT_INST_IRQ_BY_NAME(0, ep_data, irq)
#define IRQ_USB_RESET  DT_INST_IRQ_BY_NAME(0, reset, irq)

/* Where the control transfer stands. */
enum ep0_state {
	EP0_IDLE,         /* between transfers */
	EP0_SETUP,        /* a SETUP was read, not yet handed to the stack */
	EP0_WAIT,         /* the SETUP is with the stack, nothing armed */
	EP0_DATA_IN,      /* an IN data packet is armed */
	EP0_DATA_OUT,     /* armed for an OUT data packet */
	EP0_DATA_OUT_END, /* the last OUT packet is with the stack, unanswered */
	EP0_STATUS_OUT,   /* IN data done: the host's status stage awaited */
	EP0_STATUS_IN,    /* the device's status stage (empty IN) awaited */
	EP0_REPLAY,       /* a request the controller answered is with the stack */
};

/* Requests the controller answers itself, handed to the stack. */
#define REPLAY_ADDRESS    BIT(0)
#define REPLAY_RWUP_SET   BIT(1)
#define REPLAY_RWUP_CLEAR BIT(2)

struct tlsr_slot {
	struct udc_ep_config *cfg;
	uint8_t ram;     /* buffer offset in the USB RAM */
	uint8_t units;   /* buffer size in USB_RAM_UNIT */
	uint8_t len;     /* IN: bytes in the armed packet */
	bool toggle;     /* DATA1 next */
	bool armed;      /* a packet is armed and not yet taken or received */
};

struct udc_tlsr_data {
	const struct device *dev;
	struct k_work work;
	struct k_timer poll_timer;
	struct tlsr_slot slot[NUM_EP_SLOTS];
	uint32_t ram_used; /* one bit per USB_RAM_UNIT of the USB RAM */

	uint8_t setup[8];
	uint16_t out_left;  /* OUT data stage bytes still expected */
	uint8_t in_len;     /* bytes in the armed EP0 IN packet */
	uint8_t ep0_state;
	bool ep0_stall;     /* a halt to apply at the next stage */
	bool status_irq;    /* status interrupt seen, the stack's buffer not yet */
	uint8_t replay;     /* REPLAY_* still to hand to the stack */
	bool rwup;          /* 0x10a bit 2 as last handed to the stack */
	bool suspended;
	bool enabled;
};

struct udc_tlsr_config {
	struct udc_ep_config *ep_cfg_in;
	struct udc_ep_config *ep_cfg_out;
};

static const uint8_t in_eps[NUM_IN_EPS] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x87, 0x88};
static const uint8_t out_eps[NUM_OUT_EPS] = {0x00, 0x05, 0x06};

/* ------------------------------------------------------------- helpers */

static uint8_t analog_read(uint8_t addr)
{
	unsigned int key = irq_lock();
	uint8_t data;

	WR8(ANALOG_PORT_ADDR, addr);
	WR8(ANALOG_PORT_CTRL, ANALOG_PORT_START);
	while ((RD8(ANALOG_PORT_CTRL) & ANALOG_PORT_BUSY) != 0U) {
	}
	data = RD8(ANALOG_PORT_DATA);
	WR8(ANALOG_PORT_CTRL, 0U);
	irq_unlock(key);
	return data;
}

static void analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = irq_lock();

	WR8(ANALOG_PORT_ADDR, addr);
	WR8(ANALOG_PORT_DATA, value);
	WR8(ANALOG_PORT_CTRL, ANALOG_PORT_START | ANALOG_PORT_WRITE);
	while ((RD8(ANALOG_PORT_CTRL) & ANALOG_PORT_BUSY) != 0U) {
	}
	WR8(ANALOG_PORT_CTRL, 0U);
	irq_unlock(key);
}

static void dp_pullup(bool on)
{
	uint8_t v = analog_read(ANA_USB_DP_PULLUP);

	analog_write(ANA_USB_DP_PULLUP, on ? (v | BIT(7)) : (v & (uint8_t)~BIT(7)));
}

/*
 * The interrupt controller's source flags for the USB lines: each handler
 * clears its flag after the status bit, so a latched flag cannot keep the
 * line pending and a level flag reasserts while the status bit is set.
 */
static void clear_parent(unsigned int irq)
{
#if !defined(CONFIG_UDC_TELINK_B87_SIM)
	tlsr827x_irq_clear_parent(irq);
#else
	ARG_UNUSED(irq);
#endif
}

static inline uint8_t ep_slot(uint8_t ep)
{
	return USB_EP_GET_IDX(ep) & 0x07U;
}

static inline uint8_t slot_ep_num(uint8_t slot)
{
	return slot == 0U ? 8U : slot;
}

static inline bool slot_is_in(uint8_t slot)
{
	return (EP_IN_MASK & BIT(slot_ep_num(slot))) != 0U;
}

/* Allocates units contiguous USB RAM units; returns the offset or -ENOMEM. */
static int ram_alloc(struct udc_tlsr_data *priv, uint8_t units)
{
	const uint32_t mask = BIT_MASK(units);

	for (unsigned int i = 0; i + units <= 32U; i++) {
		if ((priv->ram_used & (mask << i)) == 0U) {
			priv->ram_used |= mask << i;
			return (int)(i * USB_RAM_UNIT);
		}
	}
	return -ENOMEM;
}

static void ram_free(struct udc_tlsr_data *priv, uint8_t ram, uint8_t units)
{
	priv->ram_used &= ~(BIT_MASK(units) << (ram / USB_RAM_UNIT));
}

static inline struct udc_tlsr_data *priv_of(const struct device *dev)
{
	return udc_get_private(dev);
}

/*
 * Reads n bytes from a data port (the pointer advances with each read) into
 * buf, as far as it has room; the rest is read and dropped.
 */
static void fifo_read(uintptr_t port, struct net_buf *buf, uint8_t n)
{
	size_t room = buf != NULL ? MIN(n, net_buf_tailroom(buf)) : 0U;
	uint8_t *dst = room != 0U ? net_buf_add(buf, room) : NULL;

	for (uint8_t i = 0; i < n; i++) {
		uint8_t v = RD8(port);

		if (i < room) {
			dst[i] = v;
		}
	}
}

/* --------------------------------------------------- control endpoint */

static void ep0_submit_work(struct udc_tlsr_data *priv)
{
	(void)k_work_submit_to_queue(udc_get_work_q(), &priv->work);
}

/* Writes the next IN packet (up to 8 bytes) of buf and arms it. */
static void ep0_write_in(struct udc_tlsr_data *priv, struct net_buf *buf)
{
	uint8_t n = (uint8_t)MIN(buf->len, EP0_MPS);

	WR8(EP0_PTR, 0U);
	for (uint8_t i = 0; i < n; i++) {
		WR8(EP0_DATA, buf->data[i]);
	}
	priv->in_len = n;
	WR8(EP0_CTRL, EP0_CTRL_DATA_ACK);
}

/* Arms an empty IN packet, as after the last packet of an IN data stage. */
static void ep0_ack_empty(void)
{
	WR8(EP0_PTR, 0U);
	WR8(EP0_CTRL, EP0_CTRL_DATA_ACK);
}

static void ep0_complete(const struct device *dev, uint8_t ep)
{
	struct net_buf *buf = udc_buf_get(udc_get_ep_cfg(dev, ep));

	if (buf != NULL) {
		udc_submit_ep_event(dev, buf, 0);
	}
}

/* A status buffer of the stack at the head of the endpoint's queue. */
static bool ep0_status_queued(const struct device *dev, uint8_t ep)
{
	struct net_buf *buf = udc_buf_peek(udc_get_ep_cfg(dev, ep));

	return buf != NULL && udc_get_buf_info(buf)->status;
}

/* Status stage with the stack's status buffer queued: STA_ACK, complete. */
static void ep0_status_done(const struct device *dev, uint8_t ep)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	priv->status_irq = false;
	priv->ep0_state = EP0_IDLE;
	WR8(EP0_CTRL, EP0_CTRL_STATUS_ACK);
	ep0_complete(dev, ep);
	if (priv->replay != 0U) {
		ep0_submit_work(priv);
	}
}

static void ep0_stall_now(const struct device *dev, uint8_t ctrl)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	WR8(EP0_CTRL, ctrl);
	priv->ep0_stall = false;
	priv->status_irq = false;
	priv->ep0_state = EP0_IDLE;
	if (priv->replay != 0U) {
		ep0_submit_work(priv);
	}
}

static void ep0_setup_irq(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	/* A SETUP ends any transfer: its data and status bits are stale. */
	WR8(EP0_IRQ_STATUS, EP0_IRQ_ALL);
	clear_parent(IRQ_EP0_SETUP);
	clear_parent(IRQ_EP0_DATA);
	clear_parent(IRQ_EP0_STATUS);

	WR8(EP0_PTR, 0U);
	for (int i = 0; i < 8; i++) {
		priv->setup[i] = RD8(EP0_DATA);
	}
	WR8(EP0_PTR, 0U);

	priv->ep0_state = EP0_SETUP;
	priv->ep0_stall = false;
	priv->status_irq = false;
	ep0_submit_work(priv);
}

static void ep0_data_irq(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	struct net_buf *buf;

	WR8(EP0_IRQ_STATUS, EP0_IRQ_DATA);
	clear_parent(IRQ_EP0_DATA);

	if (priv->ep0_stall &&
	    (priv->ep0_state == EP0_DATA_IN || priv->ep0_state == EP0_DATA_OUT)) {
		ep0_stall_now(dev, EP0_CTRL_DATA_STALL);
		return;
	}

	switch (priv->ep0_state) {
	case EP0_DATA_IN:
		/* The host took the packet. */
		buf = udc_buf_peek(udc_get_ep_cfg(dev, USB_CONTROL_EP_IN));
		if (buf == NULL) {
			ep0_ack_empty();
			priv->ep0_state = EP0_STATUS_OUT;
			break;
		}
		net_buf_pull(buf, priv->in_len);
		priv->in_len = 0U;
		if (buf->len != 0U) {
			ep0_write_in(priv, buf);
		} else if (udc_ep_buf_has_zlp(buf)) {
			udc_ep_buf_clear_zlp(buf);
			ep0_ack_empty();
		} else {
			ep0_complete(dev, USB_CONTROL_EP_IN);
			ep0_ack_empty();
			priv->ep0_state = EP0_STATUS_OUT;
		}
		break;
	case EP0_DATA_OUT: {
		/*
		 * The packet size follows from wLength: full packets of 8
		 * bytes, then the rest. The EP0 pointer is not known to hold
		 * the received count.
		 */
		uint8_t n = (uint8_t)MIN(priv->out_left, EP0_MPS);

		buf = udc_buf_peek(udc_get_ep_cfg(dev, USB_CONTROL_EP_OUT));
		priv->out_left -= n;
		WR8(EP0_PTR, 0U);
		fifo_read(EP0_DATA, buf, n);
		if (priv->out_left != 0U) {
			WR8(EP0_CTRL, EP0_CTRL_DATA_ACK);
			break;
		}
		/* The stack's status buffer or halt answers the last packet. */
		priv->ep0_state = EP0_DATA_OUT_END;
		ep0_complete(dev, USB_CONTROL_EP_OUT);
		break;
	}
	case EP0_STATUS_OUT:
		/* The host took the empty packet armed after the data. */
		ep0_ack_empty();
		break;
	default:
		break;
	}
}

static void ep0_status_irq(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	WR8(EP0_IRQ_STATUS, EP0_IRQ_STATUS_STAGE);
	clear_parent(IRQ_EP0_STATUS);

	if (priv->ep0_stall) {
		ep0_stall_now(dev, EP0_CTRL_STATUS_STALL);
		return;
	}

	switch (priv->ep0_state) {
	case EP0_DATA_IN:
		/* The host ended the data stage early. */
		ep0_complete(dev, USB_CONTROL_EP_IN);
		priv->ep0_state = EP0_STATUS_OUT;
		__fallthrough;
	case EP0_STATUS_OUT:
		if (ep0_status_queued(dev, USB_CONTROL_EP_OUT)) {
			ep0_status_done(dev, USB_CONTROL_EP_OUT);
		} else {
			priv->status_irq = true;
		}
		break;
	case EP0_STATUS_IN:
		if (ep0_status_queued(dev, USB_CONTROL_EP_IN)) {
			ep0_status_done(dev, USB_CONTROL_EP_IN);
		} else {
			priv->status_irq = true;
		}
		break;
	default:
		break;
	}
}

static void ctrl_service(const struct device *dev)
{
	uint8_t sta = RD8(EP0_IRQ_STATUS);

	if ((sta & EP0_IRQ_SETUP) != 0U) {
		ep0_setup_irq(dev);
		return;
	}
	if ((sta & EP0_IRQ_DATA) != 0U) {
		ep0_data_irq(dev);
	}
	if ((sta & EP0_IRQ_STATUS_STAGE) != 0U) {
		ep0_status_irq(dev);
	}
}

/* The stack queued a control buffer (thread context, interrupts locked). */
static void ep0_enqueue(const struct device *dev, struct udc_ep_config *const cfg,
			struct net_buf *buf)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	struct udc_buf_info *bi = udc_get_buf_info(buf);

	if (priv->ep0_state == EP0_REPLAY && bi->status) {
		/* The controller already answered the replayed request. */
		priv->ep0_state = EP0_IDLE;
		udc_submit_ep_event(dev, buf, 0);
		if (priv->replay != 0U) {
			ep0_submit_work(priv);
		}
		return;
	}

	udc_buf_put(cfg, buf);

	if (bi->setup) {
		/* The stack is ready for the next SETUP. */
		if (priv->replay != 0U) {
			ep0_submit_work(priv);
		}
		return;
	}

	switch (priv->ep0_state) {
	case EP0_WAIT:
		if (cfg->addr == USB_CONTROL_EP_IN && bi->data) {
			ep0_write_in(priv, buf);
			priv->ep0_state = EP0_DATA_IN;
		} else if (cfg->addr == USB_CONTROL_EP_OUT && bi->data) {
			priv->out_left = sys_get_le16(&priv->setup[6]);
			WR8(EP0_CTRL, EP0_CTRL_DATA_ACK);
			priv->ep0_state = EP0_DATA_OUT;
		} else if (cfg->addr == USB_CONTROL_EP_IN && bi->status) {
			/* A request without data: status stage next. */
			WR8(EP0_CTRL, EP0_CTRL_DATA_ACK);
			priv->ep0_state = EP0_STATUS_IN;
		}
		break;
	case EP0_DATA_OUT_END:
		if (cfg->addr == USB_CONTROL_EP_IN && bi->status) {
			WR8(EP0_CTRL, EP0_CTRL_DATA_ACK);
			priv->ep0_state = EP0_STATUS_IN;
		}
		break;
	case EP0_STATUS_OUT:
		if (cfg->addr == USB_CONTROL_EP_OUT && bi->status && priv->status_irq) {
			ep0_status_done(dev, USB_CONTROL_EP_OUT);
		}
		break;
	default:
		/* A buffer of a transfer a newer SETUP replaced: cancelled with it. */
		break;
	}
}

static void ep0_set_halt(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	struct udc_data *data = dev->data;

	if (data->setup_pending) {
		/* For a transfer the cached newer SETUP replaces. */
		return;
	}

	switch (priv->ep0_state) {
	case EP0_WAIT:
	case EP0_DATA_OUT_END:
		ep0_stall_now(dev, EP0_CTRL_DATA_STALL);
		break;
	case EP0_REPLAY:
		/* The stack refused a replayed request: nothing on the bus. */
		priv->ep0_state = EP0_IDLE;
		if (priv->replay != 0U) {
			ep0_submit_work(priv);
		}
		break;
	case EP0_DATA_IN:
	case EP0_DATA_OUT:
	case EP0_STATUS_OUT:
	case EP0_STATUS_IN:
		if (priv->status_irq) {
			ep0_stall_now(dev, EP0_CTRL_STATUS_STALL);
		} else {
			priv->ep0_stall = true; /* applied by the next stage */
		}
		break;
	default:
		break;
	}
}

/*
 * Hands SETUPs to the stack in thread context: first the requests the
 * controller answered (only between transfers or before a new SETUP, and
 * only with the stack's SETUP buffer queued, since each one takes it), then
 * the SETUP the host sent. The driver mutex keeps the stack's answers to an
 * earlier request from arriving between the state change and the hand-over.
 */
static void ep0_work_handler(struct k_work *work)
{
	struct udc_tlsr_data *priv = CONTAINER_OF(work, struct udc_tlsr_data, work);
	const struct device *dev = priv->dev;

	udc_lock_internal(dev, K_FOREVER);

	while (true) {
		struct net_buf *head = udc_buf_peek(udc_get_ep_cfg(dev, USB_CONTROL_EP_OUT));
		uint8_t setup[8] = {0};
		bool deliver = false;
		unsigned int key = irq_lock();

		if (priv->replay != 0U &&
		    (priv->ep0_state == EP0_IDLE || priv->ep0_state == EP0_SETUP) &&
		    head != NULL && udc_get_buf_info(head)->setup) {
			/* bmRequestType 0: standard, to the device */
			if ((priv->replay & REPLAY_ADDRESS) != 0U) {
				priv->replay &= ~REPLAY_ADDRESS;
				setup[1] = USB_SREQ_SET_ADDRESS;
				setup[2] = 1U; /* the controller holds the real one */
			} else if ((priv->replay & REPLAY_RWUP_SET) != 0U) {
				priv->replay &= ~REPLAY_RWUP_SET;
				setup[1] = USB_SREQ_SET_FEATURE;
				setup[2] = USB_SFS_REMOTE_WAKEUP;
			} else {
				priv->replay &= ~REPLAY_RWUP_CLEAR;
				setup[1] = USB_SREQ_CLEAR_FEATURE;
				setup[2] = USB_SFS_REMOTE_WAKEUP;
			}
			if (priv->ep0_state == EP0_IDLE) {
				priv->ep0_state = EP0_REPLAY;
			}
			deliver = true;
		} else if (priv->ep0_state == EP0_SETUP) {
			memcpy(setup, priv->setup, sizeof(setup));
			priv->ep0_state = EP0_WAIT;
			deliver = true;
		}
		irq_unlock(key);

		if (!deliver) {
			break;
		}
		LOG_DBG("SETUP %02x %02x %02x %02x", setup[0], setup[1], setup[2], setup[3]);
		udc_setup_received(dev, setup);
	}

	udc_unlock_internal(dev);
}

/* ------------------------------------------------------- data endpoints */

/* Arms a slot with its next data PID. */
static void slot_arm(struct tlsr_slot *s, uint8_t slot)
{
	WR8(EP_CTRL(slot), EP_CTRL_BUSY | (s->toggle ? EP_CTRL_DATA1 : EP_CTRL_DATA0));
	s->toggle = !s->toggle;
	s->armed = true;
}

/* Disarms a slot; a packet armed and not taken gives its PID back. */
static void slot_disarm(struct tlsr_slot *s, uint8_t slot, uint8_t ctrl)
{
	if (s->armed && (RD8(EP_CTRL(slot)) & EP_CTRL_BUSY) != 0U) {
		s->toggle = !s->toggle;
	}
	WR8(EP_CTRL(slot), ctrl);
	s->armed = false;
}

static void in_write(struct tlsr_slot *s, uint8_t slot, struct net_buf *buf)
{
	uint8_t n = (uint8_t)MIN(buf->len, udc_mps_ep_size(s->cfg));

	WR8(EP_PTR(slot), 0U);
	for (uint8_t i = 0; i < n; i++) {
		WR8(EP_DATA(slot), buf->data[i]);
	}
	s->len = n;
	slot_arm(s, slot);
}

/* Starts the transfer at the head of the queue (interrupts locked). */
static void slot_start(struct udc_tlsr_data *priv, uint8_t slot)
{
	struct tlsr_slot *s = &priv->slot[slot];
	struct udc_ep_config *cfg = s->cfg;
	struct net_buf *buf;

	if (cfg == NULL || !cfg->stat.enabled || cfg->stat.halted || udc_ep_is_busy(cfg)) {
		return;
	}
	buf = udc_buf_peek(cfg);
	if (buf == NULL) {
		return;
	}
	udc_ep_set_busy(cfg, true);
	if (slot_is_in(slot)) {
		in_write(s, slot, buf);
	} else {
		WR8(EP_PTR(slot), 0U);
		slot_arm(s, slot);
	}
}

static void slot_finish(const struct device *dev, uint8_t slot)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	struct tlsr_slot *s = &priv->slot[slot];
	struct net_buf *buf = udc_buf_get(s->cfg);

	udc_ep_set_busy(s->cfg, false);
	if (buf != NULL) {
		udc_submit_ep_event(dev, buf, 0);
	}
	slot_start(priv, slot);
}

/* An IN packet was taken when the busy bit is clear again. */
static void in_done(const struct device *dev, uint8_t slot)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	struct tlsr_slot *s = &priv->slot[slot];
	struct net_buf *buf;

	if (!s->armed || (RD8(EP_CTRL(slot)) & EP_CTRL_BUSY) != 0U) {
		return;
	}
	s->armed = false;
	buf = udc_buf_peek(s->cfg);
	if (buf == NULL) {
		udc_ep_set_busy(s->cfg, false);
		return;
	}
	net_buf_pull(buf, s->len);
	s->len = 0U;
	if (buf->len != 0U) {
		in_write(s, slot, buf);
	} else if (udc_ep_buf_has_zlp(buf)) {
		udc_ep_buf_clear_zlp(buf);
		in_write(s, slot, buf);
	} else {
		slot_finish(dev, slot);
	}
}

/* An OUT packet arrived (the slot's bit in EP_IRQ). */
static void out_done(const struct device *dev, uint8_t slot)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	struct tlsr_slot *s = &priv->slot[slot];
	struct net_buf *buf;
	uint8_t count;

	if (!s->armed) {
		return;
	}
	s->armed = false;
	buf = udc_buf_peek(s->cfg);
	count = RD8(EP_PTR(slot)); /* bytes received */
	WR8(EP_PTR(slot), 0U);
	fifo_read(EP_DATA(slot), buf, count);
	if (buf == NULL) {
		udc_ep_set_busy(s->cfg, false);
		return;
	}
	if (count == udc_mps_ep_size(s->cfg) && net_buf_tailroom(buf) != 0U) {
		WR8(EP_PTR(slot), 0U);
		slot_arm(s, slot);
	} else {
		slot_finish(dev, slot);
	}
}

static void ep_service(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	uint8_t pending = RD8(EP_IRQ);
	uint8_t irq = pending & RD8(EP_IRQ_MASK);

	/*
	 * Write one to clear: every pending bit, not only the masked ones, so
	 * the bit of a slot the driver does not use cannot hold the line.
	 */
	WR8(EP_IRQ, pending);
	clear_parent(IRQ_EP_DATA);
	for (uint8_t slot = 0; slot < NUM_EP_SLOTS; slot++) {
		if ((irq & BIT(slot)) == 0U || priv->slot[slot].cfg == NULL ||
		    !priv->slot[slot].cfg->stat.enabled) {
			continue;
		}
		if (slot_is_in(slot)) {
			in_done(dev, slot);
		} else {
			out_done(dev, slot);
		}
	}
}

/* ------------------------------------------------------- bus events */

static void reset_service(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	if ((RD8(IRQ_SOURCE_B2) & IRQ_SOURCE_B2_USB_RESET) == 0U) {
		return;
	}
	WR8(IRQ_SOURCE_B2, IRQ_SOURCE_B2_USB_RESET);
	clear_parent(IRQ_USB_RESET);

	for (uint8_t slot = 0; slot < NUM_EP_SLOTS; slot++) {
		struct tlsr_slot *s = &priv->slot[slot];

		WR8(EP_CTRL(slot), 0U);
		s->toggle = false;
		s->armed = false;
		if (s->cfg != NULL) {
			udc_ep_set_busy(s->cfg, false);
		}
	}
	priv->ep0_state = EP0_IDLE;
	priv->ep0_stall = false;
	priv->status_irq = false;
	/* The stack forgets remote wakeup at a reset; so does the controller. */
	priv->replay &= ~(REPLAY_RWUP_SET | REPLAY_RWUP_CLEAR);
	priv->rwup = false;
	if (priv->suspended) {
		priv->suspended = false;
		udc_set_suspended(dev, false);
	}
	udc_submit_event(dev, UDC_EVT_RESET, 0);
}

static void suspend_check(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	bool suspended = (RD8(IRQ_SOURCE_B0) & IRQ_SOURCE_B0_USB_PWDN) != 0U;

	if (suspended != priv->suspended) {
		priv->suspended = suspended;
		udc_set_suspended(dev, suspended);
		udc_submit_event(dev, suspended ? UDC_EVT_SUSPEND : UDC_EVT_RESUME, 0);
	}
}

/* The controller's remote wakeup flag, handed to the stack when it changes. */
static void rwup_check(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	bool rwup = (RD8(USB_REG_0A) & USB_REG_0A_RWUP) != 0U;

	if (rwup != priv->rwup) {
		priv->rwup = rwup;
		priv->replay &= ~(REPLAY_RWUP_SET | REPLAY_RWUP_CLEAR);
		priv->replay |= rwup ? REPLAY_RWUP_SET : REPLAY_RWUP_CLEAR;
		ep0_submit_work(priv);
	}
}

/* ------------------------------------------------- interrupts and poll */

static void ctrl_isr(const void *arg)
{
	ctrl_service(arg);
}

static void ep_data_isr(const void *arg)
{
	ep_service(arg);
}

static void reset_isr(const void *arg)
{
	reset_service(arg);
}

/* The five interrupt lines, with every pending status bit and source flag cleared first. */
static void irq_lines(bool on)
{
	static const unsigned int lines[] = {
		IRQ_EP0_SETUP, IRQ_EP0_DATA, IRQ_EP0_STATUS, IRQ_EP_DATA, IRQ_USB_RESET,
	};

	if (!IS_ENABLED(CONFIG_UDC_TELINK_B87_IRQ_LINES)) {
		return;
	}
	if (on) {
		WR8(EP0_IRQ_STATUS, EP0_IRQ_ALL);
		WR8(EP_IRQ, 0xffU);
		WR8(IRQ_SOURCE_B2, IRQ_SOURCE_B2_USB_RESET);
	}
	for (size_t i = 0; i < ARRAY_SIZE(lines); i++) {
		if (on) {
			clear_parent(lines[i]);
			irq_enable(lines[i]);
		} else {
			irq_disable(lines[i]);
		}
	}
}

/*
 * Every handler, as the interrupt lines would run them, plus the IN packets
 * whose data interrupt did not come and the bus state the poll alone sees.
 */
static void usb_poll(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	unsigned int key = irq_lock();

	if ((RD8(EP0_IRQ_STATUS) & EP0_IRQ_ALL) != 0U) {
		ctrl_service(dev);
	}
	reset_service(dev);
	suspend_check(dev);
	if ((RD8(EP_IRQ) & RD8(EP_IRQ_MASK)) != 0U) {
		ep_service(dev);
	}
	for (uint8_t slot = 0; slot < NUM_EP_SLOTS; slot++) {
		if (slot_is_in(slot) && priv->slot[slot].cfg != NULL &&
		    priv->slot[slot].cfg->stat.enabled) {
			in_done(dev, slot);
		}
	}
	rwup_check(dev);
	irq_unlock(key);
}

#define UDC_DEV DEVICE_DT_INST_GET(0)

#if defined(CONFIG_UDC_TELINK_B87_EXTERNAL_POLL)
/*
 * The board polls every millisecond (udc_b87_poll()); the poll timer is
 * a fallback that polls only when the board has not polled since its last
 * expiry, so that USB still works if the board's timer interrupt stops.
 *
 * The flags are plain volatile bytes: a byte store is one instruction, and
 * the two interrupts that share board_polled (the board's timer and the
 * system timer) do not nest on the TC32.
 */
#define POLL_PERIOD K_MSEC(CONFIG_UDC_TELINK_B87_FALLBACK_POLL_MS)

static volatile bool board_polled;
/*
 * The board's poll acts from udc_enable() until udc_disable(), and not while
 * the chip is in suspend. Set in thread context, read in interrupts.
 */
static volatile bool board_poll_on;

void udc_b87_poll(void)
{
	if (board_poll_on) {
		board_polled = true;
		usb_poll(UDC_DEV);
	}
}

static void poll_handler(struct k_timer *timer)
{
	if (!board_polled) {
		usb_poll(k_timer_user_data_get(timer));
	}
	board_polled = false;
}
#else
#define POLL_PERIOD K_MSEC(1)

static void poll_handler(struct k_timer *timer)
{
	usb_poll(k_timer_user_data_get(timer));
}
#endif

#if defined(CONFIG_UDC_TELINK_B87_SLOW_POLL)
/*
 * A board's low-power state, while no host uses the bus, polls every
 * UDC_TELINK_B87_SLOW_POLL_MS instead of every millisecond
 * (udc_b87_slow_poll()). Set and read in thread context.
 */
static bool slow_poll;
/* Set while udc_b87_chip_suspend() has the poll stopped. */
static bool poll_stopped;
#define CUR_PERIOD (slow_poll ? K_MSEC(CONFIG_UDC_TELINK_B87_SLOW_POLL_MS) : POLL_PERIOD)

void udc_b87_slow_poll(bool slow)
{
	struct udc_tlsr_data *priv = priv_of(UDC_DEV);
	unsigned int key = irq_lock();

	if (slow != slow_poll) {
		slow_poll = slow;
		if (priv->enabled && !poll_stopped) {
			k_timer_start(&priv->poll_timer, CUR_PERIOD, CUR_PERIOD);
		}
	}
	irq_unlock(key);
}
#else
#define CUR_PERIOD POLL_PERIOD
#endif

/*
 * Around the chip's suspend: the poll stops, and polls once at the wake. A
 * periodic timer would otherwise fire once for every millisecond the chip
 * slept, all at the wake.
 */
void udc_b87_chip_suspend(bool entering)
{
	struct udc_tlsr_data *priv = priv_of(UDC_DEV);
	unsigned int key;

	if (!priv->enabled) {
		return;
	}
	key = irq_lock();
	if (entering) {
#if defined(CONFIG_UDC_TELINK_B87_EXTERNAL_POLL)
		board_poll_on = false;
#endif
#if defined(CONFIG_UDC_TELINK_B87_SLOW_POLL)
		poll_stopped = true;
#endif
		k_timer_stop(&priv->poll_timer);
	} else {
#if defined(CONFIG_UDC_TELINK_B87_EXTERNAL_POLL)
		/* The first poll after the wake is the board's or the fallback's. */
		board_polled = false;
		board_poll_on = true;
		k_timer_start(&priv->poll_timer, POLL_PERIOD, POLL_PERIOD);
#else
		/* The first poll at once, from the timer's interrupt. */
		k_timer_start(&priv->poll_timer, K_NO_WAIT, CUR_PERIOD);
#endif
#if defined(CONFIG_UDC_TELINK_B87_SLOW_POLL)
		poll_stopped = false;
#endif
	}
	irq_unlock(key);
}

/* ------------------------------------------------------------- UDC API */

static int udc_tlsr_ep_enqueue(const struct device *dev, struct udc_ep_config *const cfg,
			       struct net_buf *buf)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	unsigned int key = irq_lock();

	if (USB_EP_GET_IDX(cfg->addr) == 0U) {
		ep0_enqueue(dev, cfg, buf);
	} else {
		udc_buf_put(cfg, buf);
		slot_start(priv, ep_slot(cfg->addr));
	}
	irq_unlock(key);
	return 0;
}

static int udc_tlsr_ep_dequeue(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	unsigned int key = irq_lock();

	if (USB_EP_GET_IDX(cfg->addr) != 0U) {
		uint8_t slot = ep_slot(cfg->addr);

		slot_disarm(&priv->slot[slot], slot, 0U);
	}
	udc_ep_set_busy(cfg, false);
	udc_ep_cancel_queued(dev, cfg);
	irq_unlock(key);
	return 0;
}

static int udc_tlsr_ep_enable(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	uint8_t slot = ep_slot(cfg->addr);
	struct tlsr_slot *s = &priv->slot[slot];
	uint8_t units = DIV_ROUND_UP(udc_mps_ep_size(cfg), USB_RAM_UNIT);
	unsigned int key;
	int ram;

	if (USB_EP_GET_IDX(cfg->addr) == 0U) {
		return 0;
	}

	key = irq_lock();
	ram = ram_alloc(priv, units);
	if (ram < 0) {
		irq_unlock(key);
		LOG_ERR("USB RAM exhausted for ep 0x%02x", cfg->addr);
		return -ENOMEM;
	}
	s->cfg = cfg;
	s->ram = (uint8_t)ram;
	s->units = units;
	s->toggle = false; /* DATA0 after SET_CONFIGURATION and SET_INTERFACE */
	s->armed = false;
	s->len = 0U;
	WR8(EP_BUF_ADDR(slot), s->ram);
	WR8(EP_CTRL(slot), 0U);
	WR8(EP_PTR(slot), 0U);
	SET8(EP_ENABLE, BIT(slot));
	SET8(EP_IRQ_MASK, BIT(slot));
	udc_ep_set_busy(cfg, false);
	irq_unlock(key);

	LOG_DBG("Enable ep 0x%02x, RAM 0x%02x", cfg->addr, s->ram);
	return 0;
}

static int udc_tlsr_ep_disable(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	uint8_t slot = ep_slot(cfg->addr);
	struct tlsr_slot *s = &priv->slot[slot];
	unsigned int key;

	if (USB_EP_GET_IDX(cfg->addr) == 0U) {
		return 0;
	}

	key = irq_lock();
	CLR8(EP_IRQ_MASK, BIT(slot));
	CLR8(EP_ENABLE, BIT(slot));
	slot_disarm(s, slot, 0U);
	if (s->cfg == cfg) {
		ram_free(priv, s->ram, s->units);
		s->units = 0U;
	}
	udc_ep_set_busy(cfg, false);
	irq_unlock(key);

	LOG_DBG("Disable ep 0x%02x", cfg->addr);
	return 0;
}

static int udc_tlsr_ep_set_halt(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	unsigned int key = irq_lock();

	if (USB_EP_GET_IDX(cfg->addr) == 0U) {
		ep0_set_halt(dev);
	} else {
		uint8_t slot = ep_slot(cfg->addr);

		/* A transfer in flight restarts from its head at clear halt. */
		slot_disarm(&priv->slot[slot], slot, EP_CTRL_STALL);
		udc_ep_set_busy(cfg, false);
		cfg->stat.halted = true;
	}
	irq_unlock(key);
	LOG_DBG("Set halt ep 0x%02x", cfg->addr);
	return 0;
}

static int udc_tlsr_ep_clear_halt(const struct device *dev, struct udc_ep_config *const cfg)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	uint8_t slot = ep_slot(cfg->addr);
	unsigned int key;

	if (USB_EP_GET_IDX(cfg->addr) == 0U) {
		return 0;
	}

	key = irq_lock();
	/* CLEAR_FEATURE(ENDPOINT_HALT) resets the data toggle. */
	WR8(EP_CTRL(slot), 0U);
	priv->slot[slot].armed = false;
	priv->slot[slot].toggle = false;
	cfg->stat.halted = false;
	udc_ep_set_busy(cfg, false);
	slot_start(priv, slot);
	irq_unlock(key);
	LOG_DBG("Clear halt ep 0x%02x", cfg->addr);
	return 0;
}

static int udc_tlsr_set_address(const struct device *dev, const uint8_t addr)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	/*
	 * The controller answers SET_ADDRESS itself (see the top of this
	 * file). The stack sets address 0 when it has handled a bus reset:
	 * then it is told of the address the host gives next.
	 */
	if (addr == 0U) {
		unsigned int key = irq_lock();

		priv->replay |= REPLAY_ADDRESS;
		ep0_submit_work(priv);
		irq_unlock(key);
	}
	return 0;
}

/*
 * Remote wakeup: -EACCES unless the host enabled it (0x10a bit 2, set by the
 * controller); then, while usb_pwdn is set, 0x6e = bit 6 (the chip sends the
 * USB resume signal, DS-TLSR8278 Table 2-7), then bit 2 (USB wakeup enable).
 */
static int udc_tlsr_host_wakeup(const struct device *dev)
{
	unsigned int key;

	ARG_UNUSED(dev);
	if ((RD8(USB_REG_0A) & USB_REG_0A_RWUP) == 0U) {
		return -EACCES;
	}
	key = irq_lock();
	if ((RD8(IRQ_SOURCE_B0) & IRQ_SOURCE_B0_USB_PWDN) != 0U) {
		WR8(WAKEUP_EN, WAKEUP_EN_USB_RESUME);
		WR8(WAKEUP_EN, WAKEUP_EN_USB);
	}
	irq_unlock(key);
	return 0;
}

static enum udc_bus_speed udc_tlsr_device_speed(const struct device *dev)
{
	ARG_UNUSED(dev);
	return UDC_BUS_SPEED_FS;
}

static int udc_tlsr_enable(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);
	unsigned int key = irq_lock();

	priv->ep0_state = EP0_IDLE;
	priv->ep0_stall = false;
	priv->status_irq = false;
	priv->replay = 0U;
	priv->rwup = false;
	priv->suspended = false;
	irq_unlock(key);

	irq_lines(true);
	dp_pullup(true);
	priv->enabled = true;
#if defined(CONFIG_UDC_TELINK_B87_EXTERNAL_POLL)
	board_polled = false; /* the fallback's first expiry polls unless the board has */
#endif
	k_timer_start(&priv->poll_timer, CUR_PERIOD, CUR_PERIOD);
#if defined(CONFIG_UDC_TELINK_B87_EXTERNAL_POLL)
	board_poll_on = true;
#endif
	LOG_DBG("Enable device %p", dev);
	return 0;
}

static int udc_tlsr_disable(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);

#if defined(CONFIG_UDC_TELINK_B87_EXTERNAL_POLL)
	board_poll_on = false;
#endif
	dp_pullup(false);
	irq_lines(false);
	k_timer_stop(&priv->poll_timer);
	priv->enabled = false;
	(void)k_work_cancel(&priv->work);
	LOG_DBG("Disable device %p", dev);
	return 0;
}

static int udc_tlsr_init(const struct device *dev)
{
	struct udc_tlsr_data *priv = priv_of(dev);

	/*
	 * USB power (analog 0x34 bit 1, not documented), clock (0x63 bit 3)
	 * and pins (PA5, PA6: GPIO function off, input on).
	 */
	analog_write(ANA_USB_POWER, analog_read(ANA_USB_POWER) & (uint8_t)~BIT(1));
	SET8(CLKEN0, CLKEN0_USB);
	CLR8(GPIO_PA_GPIO, USB_PINS_PA);
	SET8(GPIO_PA_IE, USB_PINS_PA);

	/* Standard, descriptor and configuration requests to software. */
	CLR8(EP0_IRQ_MODE, EP0_AUTO_STD | EP0_AUTO_DESC | EP0_AUTO_CFG);
	WR8(EP_MAX_SIZE, EP_MPS >> 3);
	WR8(EP_ENABLE, 0U);
	WR8(EP_IRQ_MASK, 0U);
	priv->ram_used = 0U;

	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_OUT, USB_EP_TYPE_CONTROL, EP0_MPS, 0)) {
		LOG_ERR("Failed to enable control endpoint");
		return -EIO;
	}
	if (udc_ep_enable_internal(dev, USB_CONTROL_EP_IN, USB_EP_TYPE_CONTROL, EP0_MPS, 0)) {
		LOG_ERR("Failed to enable control endpoint");
		return -EIO;
	}
	return 0;
}

static int udc_tlsr_shutdown(const struct device *dev)
{
	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_OUT)) {
		LOG_ERR("Failed to disable control endpoint");
		return -EIO;
	}
	if (udc_ep_disable_internal(dev, USB_CONTROL_EP_IN)) {
		LOG_ERR("Failed to disable control endpoint");
		return -EIO;
	}
	return 0;
}

static void udc_tlsr_lock(const struct device *dev)
{
	udc_lock_internal(dev, K_FOREVER);
}

static void udc_tlsr_unlock(const struct device *dev)
{
	udc_unlock_internal(dev);
}

static int udc_tlsr_driver_preinit(const struct device *dev)
{
	const struct udc_tlsr_config *config = dev->config;
	struct udc_tlsr_data *priv = priv_of(dev);
	struct udc_data *data = dev->data;
	int err;

	k_mutex_init(&data->mutex);
	priv->dev = dev;
	k_work_init(&priv->work, ep0_work_handler);
	k_timer_init(&priv->poll_timer, poll_handler, NULL);
	k_timer_user_data_set(&priv->poll_timer, (void *)dev);

	data->caps.rwup = true;
	data->caps.mps0 = UDC_MPS0_8;

	for (size_t i = 0; i < NUM_OUT_EPS; i++) {
		struct udc_ep_config *cfg = &config->ep_cfg_out[i];

		cfg->caps.out = 1;
		if (i == 0U) {
			cfg->caps.control = 1;
			cfg->caps.mps = EP0_MPS;
		} else {
			cfg->caps.bulk = 1;
			cfg->caps.interrupt = 1;
			cfg->caps.mps = EP_MPS;
		}
		cfg->addr = out_eps[i];
		err = udc_register_ep(dev, cfg);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	for (size_t i = 0; i < NUM_IN_EPS; i++) {
		struct udc_ep_config *cfg = &config->ep_cfg_in[i];

		cfg->caps.in = 1;
		if (i == 0U) {
			cfg->caps.control = 1;
			cfg->caps.mps = EP0_MPS;
		} else {
			cfg->caps.bulk = 1;
			cfg->caps.interrupt = 1;
			cfg->caps.mps = EP_MPS;
		}
		cfg->addr = in_eps[i];
		err = udc_register_ep(dev, cfg);
		if (err != 0) {
			LOG_ERR("Failed to register endpoint");
			return err;
		}
	}

	IRQ_CONNECT(IRQ_EP0_SETUP, 0, ctrl_isr, DEVICE_DT_INST_GET(0), 0);
	IRQ_CONNECT(IRQ_EP0_DATA, 0, ctrl_isr, DEVICE_DT_INST_GET(0), 0);
	IRQ_CONNECT(IRQ_EP0_STATUS, 0, ctrl_isr, DEVICE_DT_INST_GET(0), 0);
	IRQ_CONNECT(IRQ_EP_DATA, 0, ep_data_isr, DEVICE_DT_INST_GET(0), 0);
	IRQ_CONNECT(IRQ_USB_RESET, 0, reset_isr, DEVICE_DT_INST_GET(0), 0);

	return 0;
}

static const struct udc_api udc_tlsr_api = {
	.lock = udc_tlsr_lock,
	.unlock = udc_tlsr_unlock,
	.device_speed = udc_tlsr_device_speed,
	.init = udc_tlsr_init,
	.enable = udc_tlsr_enable,
	.disable = udc_tlsr_disable,
	.shutdown = udc_tlsr_shutdown,
	.set_address = udc_tlsr_set_address,
	.host_wakeup = udc_tlsr_host_wakeup,
	.ep_enable = udc_tlsr_ep_enable,
	.ep_disable = udc_tlsr_ep_disable,
	.ep_set_halt = udc_tlsr_ep_set_halt,
	.ep_clear_halt = udc_tlsr_ep_clear_halt,
	.ep_enqueue = udc_tlsr_ep_enqueue,
	.ep_dequeue = udc_tlsr_ep_dequeue,
};

/* One controller: the register addresses above are instance 0's. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "the TLSR8278 has one USB device controller");

static struct udc_ep_config ep_cfg_out[NUM_OUT_EPS];
static struct udc_ep_config ep_cfg_in[NUM_IN_EPS];

static const struct udc_tlsr_config udc_tlsr_config_0 = {
	.ep_cfg_in = ep_cfg_in,
	.ep_cfg_out = ep_cfg_out,
};

static struct udc_tlsr_data udc_priv_0;

static struct udc_data udc_data_0 = {
	.mutex = Z_MUTEX_INITIALIZER(udc_data_0.mutex),
	.priv = &udc_priv_0,
};

DEVICE_DT_INST_DEFINE(0, udc_tlsr_driver_preinit, NULL, &udc_data_0, &udc_tlsr_config_0,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &udc_tlsr_api);
