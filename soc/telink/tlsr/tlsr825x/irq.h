/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR8258 Interrupt Mapping
 *
 * IRQ  Mask bit   Name          Type    Clear owner
 * ---  --------  ----          ----    ----------
 *  0   0x000001  TMR0          level   arch timer: parent src, reg_tmr_sta bit0
 *  1   0x000002  TMR1          level   arch timer: parent src, reg_tmr_sta bit1
 *  2   0x000004  TMR2          level   arch timer: parent src, reg_tmr_sta bit2
 *  3   0x000008  USB_PWDN      level   USB driver
 *  4   0x000010  DMA           level   DMA driver
 *  5   0x000020  DFIFO         level   DMA FIFO driver
 *  6   0x000040  UART          level   UART driver
 *  7   0x000080  MIX_CMD       level   RF/mix driver
 *  8   0x000100  EP0_SETUP     level   USB driver
 *  9   0x000200  EP0_DAT       level   USB driver
 * 10   0x000400  EP0_STA       level   USB driver
 * 11   0x000800  SET_INTF      level   USB driver
 * 12   0x001000  EP_DATA       level   USB driver
 * 13   0x002000  ZB_RT         level   RF driver
 * 14   0x004000  SW_PWM        level   PWM driver
 * --   0x008000  (reserved)    --     --
 * 16   0x010000  USB_250US     edge    USB driver
 * 17   0x020000  USB_RST       edge    USB driver
 * 18   0x040000  GPIO          edge    GPIO driver
 * 19   0x080000  PM            edge    PM driver
 * 20   0x100000  SYSTEM_TIMER  edge    stimer driver: parent src
 * 21   0x200000  GPIO_RISC0    edge    GPIO driver
 * 22   0x400000  GPIO_RISC1    edge    GPIO driver
 * --   0x800000  (reserved)    --     --
 *
 * Clear ownership:
 * - The arch dispatcher owns only TMR0..TMR2 during this first-stage port.
 * - SYSTEM_TIMER, GPIO, GPIO_RISC and all peripheral IRQ sources are owned by
 *   their drivers/smoke tests. The dispatcher must not clear those sources.
 * - "Parent src" means the IRQSRC bit at 0x648..0x64a. For edge sources,
 *   datasheet section 6.2.3 describes clearing through IRQSRC_2 (0x64a);
 *   vendor SDK uses a 24-bit reg_irq_src write for bits 16..22, so Zephyr
 *   does the same through tlsr8258_irq_clear_parent().
 *
 * Clear sequence per vendor SDK (irq_handler.c):
 *   TMR0/TMR1: reg_irq_src = FLD_IRQ_TMRx, reg_tmr_sta = FLD_TMR_STA_TMRx, then handler
 *
 * Datasheet section 6.2.3 says edge flags are cleared through IRQSRC_2
 * (0x64a). Vendor SDK writes the corresponding 24-bit reg_irq_src bit for
 * system timer/GPIO/RISC sources; Zephyr follows the vendor sequence.
 *
 * Register addresses (from vendor datasheet):
 *   0x800640: reg_irq_mask (24-bit mask/source bits)
 *   0x800643: reg_irq_en (global enable, bit 0)
 *   0x800648: reg_irq_src (interrupt source flags)
 *   0x80064a: reg_irq_clear (write 1 to clear edge flags)
 *   0x800623: reg_tmr_sta (timer status, bits 0-2 for TMR0-2)
 */

#ifndef SOC_TELINK_TLSR825X_IRQ_H_
#define SOC_TELINK_TLSR825X_IRQ_H_

#include <zephyr/sys/util.h>

#define TLSR8258_IRQ_TMR0           0
#define TLSR8258_IRQ_TMR1           1
#define TLSR8258_IRQ_TMR2           2
#define TLSR8258_IRQ_USB_PWDN       3
#define TLSR8258_IRQ_DMA            4
#define TLSR8258_IRQ_DFIFO          5
#define TLSR8258_IRQ_UART          6
#define TLSR8258_IRQ_MIX_CMD        7
#define TLSR8258_IRQ_EP0_SETUP      8
#define TLSR8258_IRQ_EP0_DAT        9
#define TLSR8258_IRQ_EP0_STA       10
#define TLSR8258_IRQ_SET_INTF      11
#define TLSR8258_IRQ_EP_DATA      12
#define TLSR8258_IRQ_ZB_RT         13
#define TLSR8258_IRQ_SW_PWM         14
/* IRQ 15 reserved */
#define TLSR8258_IRQ_USB_250US    16
#define TLSR8258_IRQ_USB_RST       17
#define TLSR8258_IRQ_GPIO          18
#define TLSR8258_IRQ_PM            19
#define TLSR8258_IRQ_SYSTEM_TIMER 20
#define TLSR8258_IRQ_GPIO_RISC0   21
#define TLSR8258_IRQ_GPIO_RISC1   22
/* IRQ 23 reserved */

#define TLSR8258_NUM_IRQS          24

#define TLSR8258_IRQ_RESERVED_MASK (BIT(15) | BIT(23))
#define TLSR8258_IRQ_TIMER_MASK    (BIT(TLSR8258_IRQ_TMR0) | \
				    BIT(TLSR8258_IRQ_TMR1) | \
				    BIT(TLSR8258_IRQ_TMR2))
#define TLSR8258_IRQ_LEVEL_MASK    GENMASK(15, 0)
#define TLSR8258_IRQ_EDGE_MASK     GENMASK(23, 16)
#define TLSR8258_IRQ_VALID_MASK    (~TLSR8258_IRQ_RESERVED_MASK & GENMASK(23, 0))

#define TLSR8258_REG_IRQ_MASK  ((volatile uint8_t *)0x00800640u)
#define TLSR8258_REG_IRQ_EN   ((volatile uint8_t *)0x00800643u)
#define TLSR8258_REG_IRQ_SRC ((volatile uint32_t *)0x00800648u)

#define TLSR8258_REG_TMR_STA ((volatile uint8_t *)0x00800623u)

#ifndef _ASMLANGUAGE
/* reg_irq_mask is 24-bit and is adjacent to the global enable byte. */
static inline uint32_t tlsr8258_irq_mask_read(void)
{
	return (uint32_t)TLSR8258_REG_IRQ_MASK[0] |
		((uint32_t)TLSR8258_REG_IRQ_MASK[1] << 8) |
		((uint32_t)TLSR8258_REG_IRQ_MASK[2] << 16);
}

static inline void tlsr8258_irq_mask_write(uint32_t mask)
{
	TLSR8258_REG_IRQ_MASK[0] = (uint8_t)mask;
	TLSR8258_REG_IRQ_MASK[1] = (uint8_t)(mask >> 8);
	TLSR8258_REG_IRQ_MASK[2] = (uint8_t)(mask >> 16);
}

static inline uint32_t tlsr8258_irq_bit(unsigned int irq)
{
	return irq < TLSR8258_NUM_IRQS ? BIT(irq) : 0u;
}

static inline bool tlsr8258_irq_is_valid(unsigned int irq)
{
	return (tlsr8258_irq_bit(irq) & TLSR8258_IRQ_VALID_MASK) != 0u;
}

static inline bool tlsr8258_irq_is_edge(unsigned int irq)
{
	return (tlsr8258_irq_bit(irq) & TLSR8258_IRQ_EDGE_MASK) != 0u;
}

static inline void tlsr8258_irq_clear_parent(unsigned int irq)
{
	uint32_t bit = tlsr8258_irq_bit(irq);

	if ((bit & TLSR8258_IRQ_VALID_MASK) != 0u) {
		*TLSR8258_REG_IRQ_SRC = bit;
	}
}

static inline void tlsr8258_irq_clear_edge(unsigned int irq)
{
	if (tlsr8258_irq_is_edge(irq)) {
		tlsr8258_irq_clear_parent(irq);
	}
}
#endif /* _ASMLANGUAGE */

#endif /* SOC_TELINK_TLSR825X_IRQ_H_ */
