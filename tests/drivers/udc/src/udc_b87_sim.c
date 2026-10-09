/*
 * Register model of the Telink TLSR8278 USB device controller for this test
 * on native_sim (CONFIG_UDC_TELINK_B87_SIM), with no host on the bus. The EP0
 * status (0x800103) and endpoint status (0x800139) registers and the
 * interrupt source flags (0x800648-0x80064a) are write-one-to-clear; the EP0
 * and endpoint data ports read and write through their pointers (the EP0
 * FIFO, and each endpoint's buffer in the 256-byte USB RAM); the analog
 * register port completes at once. Everything else is plain storage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#define REG_BASE 0x800000U
#define REG_SIZE 0x10000U

#define A_EP0_PTR          0x800100U
#define A_EP0_DATA         0x800101U
#define A_EP0_IRQ_STATUS   0x800103U
#define A_EP_PTR0          0x800110U
#define A_EP_DATA0         0x800118U
#define A_EP_BUF0          0x800128U
#define A_EP_IRQ           0x800139U
#define A_IRQ_SOURCE       0x800648U
#define A_ANALOG_PORT_ADDR 0x8000b8U
#define A_ANALOG_PORT_DATA 0x8000b9U
#define A_ANALOG_PORT_CTRL 0x8000baU

static uint8_t regs[REG_SIZE];
static uint8_t analog[256];
static uint8_t ep0_fifo[8];
static uint8_t usb_ram[256];

static uint8_t *reg(uintptr_t addr)
{
	return &regs[(addr - REG_BASE) % REG_SIZE];
}

static uint8_t *ep_ram(uint8_t slot)
{
	return &usb_ram[(uint8_t)(*reg(A_EP_BUF0 + slot) + *reg(A_EP_PTR0 + slot))];
}

uint8_t tlsr_usb_sim_rd8(uintptr_t addr)
{
	uint8_t v;

	if (addr == A_EP0_DATA) {
		v = ep0_fifo[*reg(A_EP0_PTR) & 7U];
		(*reg(A_EP0_PTR))++;
		return v;
	}
	if (addr >= A_EP_DATA0 && addr < A_EP_DATA0 + 8U) {
		uint8_t slot = (uint8_t)(addr - A_EP_DATA0);

		v = *ep_ram(slot);
		(*reg(A_EP_PTR0 + slot))++;
		return v;
	}
	if (addr == A_ANALOG_PORT_CTRL) {
		return *reg(addr) & (uint8_t)~1U; /* never busy */
	}
	return *reg(addr);
}

void tlsr_usb_sim_wr8(uintptr_t addr, uint8_t value)
{
	if (addr == A_EP0_DATA) {
		ep0_fifo[*reg(A_EP0_PTR) & 7U] = value;
		(*reg(A_EP0_PTR))++;
		return;
	}
	if (addr >= A_EP_DATA0 && addr < A_EP_DATA0 + 8U) {
		uint8_t slot = (uint8_t)(addr - A_EP_DATA0);

		*ep_ram(slot) = value;
		(*reg(A_EP_PTR0 + slot))++;
		return;
	}
	if (addr == A_EP0_IRQ_STATUS || addr == A_EP_IRQ ||
	    (addr >= A_IRQ_SOURCE && addr < A_IRQ_SOURCE + 3U)) {
		*reg(addr) &= (uint8_t)~value;
		return;
	}
	if (addr == A_ANALOG_PORT_CTRL && (value & 0x40U) != 0U) {
		if ((value & 0x20U) != 0U) {
			analog[*reg(A_ANALOG_PORT_ADDR)] = *reg(A_ANALOG_PORT_DATA);
		} else {
			*reg(A_ANALOG_PORT_DATA) = analog[*reg(A_ANALOG_PORT_ADDR)];
		}
	}
	*reg(addr) = value;
}
