/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <tlsr825x/irq.h>

/*
 * The chip has no software-triggerable vector: reg_irq_src is write-to-clear,
 * so nothing can raise a source from code. Borrow TMR0 instead and let it
 * expire a few microseconds out. The routine then runs from the real IRQ
 * dispatcher, which is the point of the API: a routine that only got a plain
 * call on the caller's stack would see k_is_in_isr() false and prove nothing.
 *
 * TMR0 is the only one of the three free: the suspend path arms TMR1 as its
 * wakeup source (soc/telink/tlsr/tlsr825x/power.c) and the radio driver runs
 * TMR2 alongside the watchdog.
 */
#define TLSR8258_REG_TMR_CTRL  ((volatile uint32_t *)0x00800620u)
#define TLSR8258_REG_TMR0_CAPT ((volatile uint32_t *)0x00800624u)
#define TLSR8258_REG_TMR0_TICK ((volatile uint32_t *)0x00800630u)

#define FLD_TMR0_EN   BIT(0)
#define FLD_TMR0_MODE GENMASK(2, 1)

/* Mode 0 counts the system clock, so this is a handful of microseconds. */
#define TMR0_OFFLOAD_CYCLES 200u

static volatile irq_offload_routine_t offload_routine;
static const void *offload_param;

static void tmr0_stop(void)
{
	*TLSR8258_REG_TMR_CTRL &= ~FLD_TMR0_EN;
}

static void tc32_irq_offload_isr(const void *unused)
{
	irq_offload_routine_t routine = offload_routine;

	ARG_UNUSED(unused);

	/*
	 * Disarm and drop the routine before running it: a routine that
	 * faults must not be re-entered by a second expiry. The dispatcher
	 * has already cleared the TMR0 source.
	 */
	tmr0_stop();
	offload_routine = NULL;

	if (routine != NULL) {
		routine(offload_param);
	}
}

void arch_irq_offload(irq_offload_routine_t routine, const void *parameter)
{
	uint8_t key = *TLSR8258_REG_IRQ_EN;

	/* Stage the request where the ISR cannot observe it half-written. */
	*TLSR8258_REG_IRQ_EN = 0u;

	offload_param = parameter;
	offload_routine = routine;

	tmr0_stop();
	*TLSR8258_REG_TMR_CTRL &= ~FLD_TMR0_MODE;
	*TLSR8258_REG_TMR0_TICK = 0u;
	*TLSR8258_REG_TMR0_CAPT = TMR0_OFFLOAD_CYCLES;
	*TLSR8258_REG_TMR_STA = (uint8_t)BIT(TLSR8258_IRQ_TMR0);
	tlsr8258_irq_clear_parent(TLSR8258_IRQ_TMR0);
	*TLSR8258_REG_TMR_CTRL |= FLD_TMR0_EN;

	irq_enable(TLSR8258_IRQ_TMR0);

	/*
	 * The routine has to reach ISR context, so the gate is opened for the
	 * wait even when the caller held it shut, then put back as it was.
	 */
	*TLSR8258_REG_IRQ_EN = 1u;

	while (offload_routine != NULL) {
	}

	irq_disable(TLSR8258_IRQ_TMR0);
	*TLSR8258_REG_IRQ_EN = key;
}

void arch_irq_offload_init(void)
{
	IRQ_CONNECT(TLSR8258_IRQ_TMR0, 0, tc32_irq_offload_isr, NULL, 0);
	tmr0_stop();
}
