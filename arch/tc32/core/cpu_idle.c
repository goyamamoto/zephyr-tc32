/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/tracing/tracing.h>

#include <tlsr827x/irq.h>

/*
 * arch_cpu_idle(), and arch_cpu_atomic_idle() with interrupts unlocked by
 * its key, return only once an interrupt has been taken. Unlocking and returning straight away, as this used to do, breaks every
 * caller that treats k_cpu_idle() as "block until something happens" and
 * leaves the idle loop spinning through the scheduler on every pass.
 *
 * This still burns the cycles rather than stopping the core. The chip can
 * stall the MCU with 0x6f bit 7 (DS-TLSR8278 Table 2-3); the suspend path
 * that does so sets its wake sources in 0x78 (not documented) and masks
 * ZB_RT off before stalling. Wiring that into the idle path could let the
 * node sleep through an RF interrupt, so it needs its wake coverage
 * established on hardware first.
 */
static ALWAYS_INLINE void tc32_wait_for_interrupt(unsigned int key)
{
	uint32_t taken = z_tc32_irq_count;

	arch_irq_unlock(key);

	while (z_tc32_irq_count == taken) {
	}
}

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_IDLE
void arch_cpu_idle(void)
{
	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle();
	}

	tc32_wait_for_interrupt(1);

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_ATOMIC_IDLE
void arch_cpu_atomic_idle(unsigned int key)
{
	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle();
	}

	if (arch_irq_unlocked(key)) {
		/* Unlocked as the key says, back once an interrupt was taken. */
		tc32_wait_for_interrupt(key);
	} else {
		/* Interrupts stay locked: back once a source is pending. */
		while ((*TLSR827X_REG_IRQ_SRC & tlsr827x_irq_mask_read() &
			TLSR827X_IRQ_VALID_MASK) == 0u) {
		}
	}

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif
