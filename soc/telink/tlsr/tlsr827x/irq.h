/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR827x interrupt mapping
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
 * - "Parent src" means the IRQSRC bit at 0x648..0x64a. Edge flags are
 *   cleared by writing 1 to their bit in IRQSRC_2 (0x64a, DS-TLSR8278
 *   6.2.3); tlsr827x_irq_clear_parent() writes the source bit to 0x648 as
 *   one 32-bit word, which reaches 0x64a for bits 16..22.
 *
 * Clear sequence for TMR0..TMR2 (level sources, cleared at the timer status,
 * DS-TLSR8278 6.2.3):
 *   source bit to 0x648, then the timer's bit to 0x623, then the handler
 *
 * Register addresses (DS-TLSR8278 5.1.1, 6.2):
 *   0x800640: interrupt mask MASK_0..MASK_2 (24 bits)
 *   0x800643: global interrupt enable, bit 0
 *   0x800648: interrupt source flags IRQSRC_0..IRQSRC_2
 *   0x80064a: IRQSRC_2 (write 1 to clear edge flags)
 *   0x800623: timer status (bits 0-2 for TMR0-2, write 1 to clear)
 */

#ifndef SOC_TELINK_TLSR827X_IRQ_H_
#define SOC_TELINK_TLSR827X_IRQ_H_

#include <zephyr/sys/util.h>

#define TLSR827X_IRQ_TMR0           0
#define TLSR827X_IRQ_TMR1           1
#define TLSR827X_IRQ_TMR2           2
#define TLSR827X_IRQ_ZB_RT         13
/* IRQ 15 reserved */
#define TLSR827X_IRQ_GPIO          18
#define TLSR827X_IRQ_SYSTEM_TIMER 20
/* IRQ 23 reserved */

#define TLSR827X_NUM_IRQS          24

#define TLSR827X_IRQ_RESERVED_MASK (BIT(15) | BIT(23))
#define TLSR827X_IRQ_TIMER_MASK    (BIT(TLSR827X_IRQ_TMR0) | \
				    BIT(TLSR827X_IRQ_TMR1) | \
				    BIT(TLSR827X_IRQ_TMR2))
#define TLSR827X_IRQ_EDGE_MASK     GENMASK(23, 16)
#define TLSR827X_IRQ_VALID_MASK    (~TLSR827X_IRQ_RESERVED_MASK & GENMASK(23, 0))

#define TLSR827X_REG_IRQ_MASK  ((volatile uint8_t *)0x00800640u)
#define TLSR827X_REG_IRQ_EN   ((volatile uint8_t *)0x00800643u)
#define TLSR827X_REG_IRQ_SRC ((volatile uint32_t *)0x00800648u)

#define TLSR827X_REG_TMR_STA ((volatile uint8_t *)0x00800623u)

#ifndef _ASMLANGUAGE
/* reg_irq_mask is 24-bit and is adjacent to the global enable byte. */
static inline uint32_t tlsr827x_irq_mask_read(void)
{
	return (uint32_t)TLSR827X_REG_IRQ_MASK[0] |
		((uint32_t)TLSR827X_REG_IRQ_MASK[1] << 8) |
		((uint32_t)TLSR827X_REG_IRQ_MASK[2] << 16);
}

static inline void tlsr827x_irq_mask_write(uint32_t mask)
{
	TLSR827X_REG_IRQ_MASK[0] = (uint8_t)mask;
	TLSR827X_REG_IRQ_MASK[1] = (uint8_t)(mask >> 8);
	TLSR827X_REG_IRQ_MASK[2] = (uint8_t)(mask >> 16);
}

static inline uint32_t tlsr827x_irq_bit(unsigned int irq)
{
	return irq < TLSR827X_NUM_IRQS ? BIT(irq) : 0u;
}

static inline bool tlsr827x_irq_is_valid(unsigned int irq)
{
	return (tlsr827x_irq_bit(irq) & TLSR827X_IRQ_VALID_MASK) != 0u;
}

static inline bool tlsr827x_irq_is_edge(unsigned int irq)
{
	return (tlsr827x_irq_bit(irq) & TLSR827X_IRQ_EDGE_MASK) != 0u;
}

static inline void tlsr827x_irq_clear_parent(unsigned int irq)
{
	uint32_t bit = tlsr827x_irq_bit(irq);

	if ((bit & TLSR827X_IRQ_VALID_MASK) != 0u) {
		*TLSR827X_REG_IRQ_SRC = bit;
	}
}

/*
 * A short critical section in a few bytes: the global interrupt enable
 * (0x643) saved and cleared, and restored. It nests with arch_irq_lock() in
 * either order (each restores what it read), but the lock-owner record of
 * arch_irq_lock() (about 130 bytes of code at each use) does not see it.
 */
static inline uint8_t tlsr827x_irq_save(void)
{
	uint8_t key = *TLSR827X_REG_IRQ_EN;

	*TLSR827X_REG_IRQ_EN = 0u;
	return key;
}

static inline void tlsr827x_irq_restore(uint8_t key)
{
	*TLSR827X_REG_IRQ_EN = key;
}

#endif /* _ASMLANGUAGE */

#endif /* SOC_TELINK_TLSR827X_IRQ_H_ */
