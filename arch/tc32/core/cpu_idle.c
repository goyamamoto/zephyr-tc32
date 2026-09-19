/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/tracing/tracing.h>

#include <tlsr825x/irq.h>

/*
 * Both entry points must return only once an interrupt has been taken.
 * Unlocking and returning straight away, as this used to do, breaks every
 * caller that treats k_cpu_idle() as "block until something happens" and
 * leaves the idle loop spinning through the scheduler on every pass.
 *
 * This still burns the cycles rather than stopping the core. The chip can
 * stall on reg_pwdn_ctrl bit 7, but it only resumes from the sources armed
 * in reg_mcu_wakeup_mask (0x78) - timers, GPIO, comparator - and the radio
 * is not among them; the suspend path even masks ZB_RT off before stalling.
 * Wiring that into the idle path would let the node sleep through an RF
 * interrupt, so it needs its wake coverage established on hardware first.
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

	tc32_wait_for_interrupt(key);

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif
