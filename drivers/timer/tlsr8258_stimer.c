/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_stimer

#include <zephyr/device.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>

#define TLSR8258_REG_SYSTEM_TICK       (*(volatile uint32_t *)0x00800740u)
#define TLSR8258_REG_SYSTEM_TICK_IRQ   (*(volatile uint32_t *)0x00800744u)
#define TLSR8258_REG_SYSTEM_TICK_MODE  (*(volatile uint8_t *)0x0080074cu)
#define TLSR8258_REG_SYSTEM_TICK_CTRL  (*(volatile uint8_t *)0x0080074fu)
#define TLSR8258_REG_CLK_SEL           (*(volatile uint8_t *)0x00800066u)

#define FLD_SYSTEM_TICK_START BIT(0)
#define FLD_SYSTEM_TICK_STOP BIT(1)
#define FLD_SYSTEM_TICK_RUNNING BIT(1)
#define FLD_SYSTEM_TICK_IRQ_EN BIT(1)
#define FLD_SYSTEM_TICK_16M BIT(4)

#define TLSR8258_SYS_CLK_16M_CRYSTAL 0x43u
#define TLSR8258_XTAL_READY_US 20u
#define TLSR8258_XTAL_READY_RETRIES 10u

#define CYCLES_PER_TICK (sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#define MIN_DELAY_CYC 64u
#define MAX_TICKS ((UINT32_MAX / 2u) / CYCLES_PER_TICK)

static uint32_t last_announce_cycle;
static uint32_t next_compare_cycle;

/* tests/kernel/context masks the tick source around a timing-sensitive
 * section and needs to know which IRQ line carries it.
 */
const int32_t z_sys_timer_irq_for_test = TLSR8258_IRQ_SYSTEM_TIMER;

__weak void tlsr8258_stimer_debug_tick(void)
{
}

static uint32_t tlsr8258_stimer_align_compare(uint32_t cycles)
{
	return (cycles + 7u) & ~0x7u;
}

static uint32_t tlsr8258_stimer_elapsed_ticks(uint32_t now)
{
	return (now - last_announce_cycle) / CYCLES_PER_TICK;
}

static void tlsr8258_stimer_wait_xtal_ready(void)
{
	for (uint32_t i = 0u; i < TLSR8258_XTAL_READY_RETRIES; i++) {
		uint32_t start = TLSR8258_REG_SYSTEM_TICK;

		for (volatile uint32_t j = 0u; j <= 0x3bu; j++) {
		}

		if ((TLSR8258_REG_SYSTEM_TICK - start) >
		    (TLSR8258_XTAL_READY_US * (uint32_t)sys_clock_hw_cycles_per_sec() / USEC_PER_SEC)) {
			return;
		}
	}
}

static void tlsr8258_stimer_program_compare(uint32_t compare, uint32_t now)
{
	if ((compare - now) < MIN_DELAY_CYC) {
		compare = now + MIN_DELAY_CYC;
	}

	next_compare_cycle = compare;
	TLSR8258_REG_SYSTEM_TICK_IRQ = tlsr8258_stimer_align_compare(compare);
}

static void tlsr8258_stimer_irq(const void *unused)
{
	ARG_UNUSED(unused);

	uint32_t now = TLSR8258_REG_SYSTEM_TICK;
	uint32_t elapsed_ticks = tlsr8258_stimer_elapsed_ticks(now);

	if (elapsed_ticks == 0u) {
		elapsed_ticks = 1u;
	}

	tlsr8258_irq_clear_parent(TLSR8258_IRQ_SYSTEM_TIMER);
#if defined(CONFIG_TLSR8258_STIMER_DEBUG_HOOK)
	tlsr8258_stimer_debug_tick();
#endif
	last_announce_cycle += elapsed_ticks * CYCLES_PER_TICK;
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		tlsr8258_stimer_program_compare(last_announce_cycle + CYCLES_PER_TICK, now);
	}

	sys_clock_announce(IS_ENABLED(CONFIG_TICKLESS_KERNEL) ? elapsed_ticks : 1u);
}

void sys_clock_set_timeout(uint32_t ticks, bool idle)
{
	ARG_UNUSED(idle);

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return;
	}

	ticks = CLAMP(ticks, 1, MAX_TICKS) - 1;

	uint32_t key = irq_lock();
	uint32_t now = TLSR8258_REG_SYSTEM_TICK;
	uint32_t adjust = (now - last_announce_cycle) + (CYCLES_PER_TICK - 1u);
	uint32_t cycles = (uint32_t)ticks * CYCLES_PER_TICK;

	if (cycles <= UINT32_MAX - adjust) {
		cycles += adjust;
	} else {
		cycles = UINT32_MAX;
	}

	cycles = (cycles / CYCLES_PER_TICK) * CYCLES_PER_TICK;

	tlsr8258_irq_clear_parent(TLSR8258_IRQ_SYSTEM_TIMER);
	tlsr8258_stimer_program_compare(last_announce_cycle + cycles, now);
	TLSR8258_REG_SYSTEM_TICK_MODE |= FLD_SYSTEM_TICK_IRQ_EN;
	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() |
		BIT(TLSR8258_IRQ_SYSTEM_TIMER));

	irq_unlock(key);
}

uint32_t sys_clock_cycle_get_32(void)
{
	return TLSR8258_REG_SYSTEM_TICK;
}

uint32_t sys_clock_elapsed(void)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0;
	}

	return tlsr8258_stimer_elapsed_ticks(TLSR8258_REG_SYSTEM_TICK);
}

static int tlsr8258_stimer_init(void)
{
	IRQ_CONNECT(TLSR8258_IRQ_SYSTEM_TIMER, 0, tlsr8258_stimer_irq, NULL, 0);
	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() &
		~BIT(TLSR8258_IRQ_SYSTEM_TIMER));
	tlsr8258_irq_clear_parent(TLSR8258_IRQ_SYSTEM_TIMER);
	TLSR8258_REG_SYSTEM_TICK_CTRL = FLD_SYSTEM_TICK_STOP;
	while ((TLSR8258_REG_SYSTEM_TICK_CTRL & FLD_SYSTEM_TICK_RUNNING) != 0u) {
		;
	}
	TLSR8258_REG_SYSTEM_TICK = 0u;
	TLSR8258_REG_SYSTEM_TICK_MODE = 0u;
	TLSR8258_REG_SYSTEM_TICK_MODE = FLD_SYSTEM_TICK_16M;
	TLSR8258_REG_SYSTEM_TICK_CTRL = FLD_SYSTEM_TICK_START;
	if (TLSR8258_REG_CLK_SEL != TLSR8258_SYS_CLK_16M_CRYSTAL) {
		tlsr8258_stimer_wait_xtal_ready();
		TLSR8258_REG_CLK_SEL = TLSR8258_SYS_CLK_16M_CRYSTAL;
		TLSR8258_REG_SYSTEM_TICK = 0u;
	}
	last_announce_cycle = TLSR8258_REG_SYSTEM_TICK;
	next_compare_cycle = last_announce_cycle + CYCLES_PER_TICK;
	TLSR8258_REG_SYSTEM_TICK_IRQ = tlsr8258_stimer_align_compare(next_compare_cycle);
	TLSR8258_REG_SYSTEM_TICK_MODE |= FLD_SYSTEM_TICK_IRQ_EN;
	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() |
		BIT(TLSR8258_IRQ_SYSTEM_TIMER));

	return 0;
}

SYS_INIT(tlsr8258_stimer_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
