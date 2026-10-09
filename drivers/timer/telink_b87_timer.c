/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_stimer

#include <zephyr/device.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <tlsr827x/irq.h>

#define TLSR827X_REG_SYSTEM_TICK       (*(volatile uint32_t *)0x00800740u)
#define TLSR827X_REG_SYSTEM_TICK_IRQ   (*(volatile uint32_t *)0x00800744u)


/*
 * TLSR8278 (DS-TLSR8278 Table 5-2): 0x74c-0x74f hold the 32 kHz timer's
 * write value and the system timer runs while 0x74a bit 1 is set (reset
 * value 0xc1: stopped; soc_early_init_hook() starts it). 0x748 bit 2 is not
 * documented; the driver sets it together with the system timer's interrupt
 * mask bit.
 */
#define STIMER_IRQ_EN_REG (*(volatile uint8_t *)0x00800748u)
#define STIMER_IRQ_EN     BIT(2)
#define STIMER_CTRL       (*(volatile uint8_t *)0x0080074au)
#define STIMER_CTRL_EN    BIT(1)


#define CYCLES_PER_TICK (sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)
/* 12 us: the least distance ahead of the tick at which the compare is written. */
#define MIN_DELAY_CYC 192u
/*
 * The compare is moved out, doubling the margin, until it is still ahead
 * after the write. 2^20 cycles (65 ms) covers any stall with interrupts off;
 * giving up earlier would leave a missed compare until the next interrupt.
 */
#define MAX_MARGIN_CYC (1u << 20)
#define MAX_TICKS ((UINT32_MAX / 2u) / CYCLES_PER_TICK)

static uint32_t last_announce_cycle;
static uint32_t next_compare_cycle;

/* tests/kernel/context masks the tick source around a timing-sensitive
 * section and needs to know which IRQ line carries it.
 */
const int32_t z_sys_timer_irq_for_test = TLSR827X_IRQ_SYSTEM_TIMER;

static uint32_t b87_stimer_align_compare(uint32_t cycles)
{
	return (cycles + 7u) & ~0x7u;
}

static uint32_t b87_stimer_elapsed_ticks(uint32_t now)
{
	return (now - last_announce_cycle) / CYCLES_PER_TICK;
}


static void b87_stimer_program_compare(uint32_t compare, uint32_t now)
{
	/*
	 * The compare only fires on a match, so it must still be ahead when it
	 * is written. The caller's "now" can be older than the time its own
	 * computation took (software divisions on TC32), so read the tick
	 * again, and compare signed: a compare already behind the tick has to
	 * move forward too.
	 */
	uint32_t margin = MIN_DELAY_CYC;

	ARG_UNUSED(now);
	for (;;) {
		now = TLSR827X_REG_SYSTEM_TICK;
		if ((int32_t)(compare - now) < (int32_t)margin) {
			compare = now + margin;
		}
		compare = b87_stimer_align_compare(compare);
		TLSR827X_REG_SYSTEM_TICK_IRQ = compare;
		/*
		 * Running from flash, a cache miss between the read and the write
		 * can take longer than the margin. If the tick is already past the
		 * compare, the match may have been missed: move it out further.
		 * An interrupt that did fire is handled by the elapsed-ticks
		 * check in the ISR.
		 */
		if ((int32_t)(compare - TLSR827X_REG_SYSTEM_TICK) > 0 || margin >= MAX_MARGIN_CYC) {
			break;
		}
		margin *= 2u;
	}
	next_compare_cycle = compare;
}

static void b87_stimer_irq(const void *unused)
{
	ARG_UNUSED(unused);

	uint32_t now = TLSR827X_REG_SYSTEM_TICK;
	uint32_t elapsed_ticks = b87_stimer_elapsed_ticks(now);

	tlsr827x_irq_clear_parent(TLSR827X_IRQ_SYSTEM_TIMER);
	if (elapsed_ticks == 0u) {
		/*
		 * Early: a compare moved in for a short timeout, or one that
		 * fired while program_compare() moved it out. Nothing to
		 * announce yet; wait for the next tick boundary. Counting a tick
		 * here would put last_announce_cycle ahead of the tick, and
		 * elapsed time would then wrap to about 268 s.
		 */
		b87_stimer_program_compare(last_announce_cycle + CYCLES_PER_TICK, now);
		return;
	}

	last_announce_cycle += elapsed_ticks * CYCLES_PER_TICK;
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		b87_stimer_program_compare(last_announce_cycle + CYCLES_PER_TICK, now);
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
	uint32_t now = TLSR827X_REG_SYSTEM_TICK;
	uint32_t adjust = (now - last_announce_cycle) + (CYCLES_PER_TICK - 1u);
	uint32_t cycles = (uint32_t)ticks * CYCLES_PER_TICK;

	if (cycles <= UINT32_MAX - adjust) {
		cycles += adjust;
	} else {
		cycles = UINT32_MAX;
	}

	cycles = (cycles / CYCLES_PER_TICK) * CYCLES_PER_TICK;

	tlsr827x_irq_clear_parent(TLSR827X_IRQ_SYSTEM_TIMER);
	b87_stimer_program_compare(last_announce_cycle + cycles, now);
	STIMER_IRQ_EN_REG |= STIMER_IRQ_EN;
	tlsr827x_irq_mask_write(tlsr827x_irq_mask_read() |
		BIT(TLSR827X_IRQ_SYSTEM_TIMER));

	irq_unlock(key);
}

uint32_t sys_clock_cycle_get_32(void)
{
	return TLSR827X_REG_SYSTEM_TICK;
}

uint32_t sys_clock_elapsed(void)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0;
	}

	return b87_stimer_elapsed_ticks(TLSR827X_REG_SYSTEM_TICK);
}

static int b87_stimer_init(void)
{
	IRQ_CONNECT(TLSR827X_IRQ_SYSTEM_TIMER, 0, b87_stimer_irq, NULL, 0);
	tlsr827x_irq_mask_write(tlsr827x_irq_mask_read() &
		~BIT(TLSR827X_IRQ_SYSTEM_TIMER));
	tlsr827x_irq_clear_parent(TLSR827X_IRQ_SYSTEM_TIMER);
	/*
	 * soc_early_init_hook() has started the timer, waited for the crystal
	 * and set the CPU clock (48 MHz).
	 */
	STIMER_IRQ_EN_REG &= (uint8_t)~STIMER_IRQ_EN;
	STIMER_CTRL |= STIMER_CTRL_EN;
	last_announce_cycle = TLSR827X_REG_SYSTEM_TICK;
	next_compare_cycle = last_announce_cycle + CYCLES_PER_TICK;
	TLSR827X_REG_SYSTEM_TICK_IRQ = b87_stimer_align_compare(next_compare_cycle);
	STIMER_IRQ_EN_REG |= STIMER_IRQ_EN;
	tlsr827x_irq_mask_write(tlsr827x_irq_mask_read() |
		BIT(TLSR827X_IRQ_SYSTEM_TIMER));

	return 0;
}

SYS_INIT(b87_stimer_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
