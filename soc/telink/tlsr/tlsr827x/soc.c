/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <ksched.h>

#define TLSR827X_PWDNEN           (*(volatile uint8_t *)0x0080006fu)
#define TLSR827X_PWDNEN_RESET_ALL BIT(5)

#define TLSR8278_REG8(addr) (*(volatile uint8_t *)(addr))
#define TLSR8278_ANALOG_PORT_ADDR  TLSR8278_REG8(0x008000b8u)
#define TLSR8278_ANALOG_PORT_DATA  TLSR8278_REG8(0x008000b9u)
#define TLSR8278_ANALOG_PORT_CTRL  TLSR8278_REG8(0x008000bau)
#define TLSR8278_ANALOG_PORT_BUSY  BIT(0)
#define TLSR8278_ANALOG_PORT_WRITE BIT(5)
#define TLSR8278_ANALOG_PORT_START BIT(6)

#define TLSR8278_REG32(addr) (*(volatile uint32_t *)(addr))
#define TLSR8278_REG_748            TLSR8278_REG8(0x00800748u)
#define TLSR8278_REG_748_BIT2       BIT(2)
#define TLSR8278_STIMER_CTRL        TLSR8278_REG8(0x0080074au)
#define TLSR8278_STIMER_CTRL_EN     BIT(1)
#define TLSR8278_STIMER_CTRL_CAL_EN BIT(3)

/*
 * Polls of analog 0x88 bit 7 and of analog 0xcf (not documented): bounded,
 * the watchdog is not running yet.
 */
#define TLSR8278_ANALOG88_TRIES 20000u
#define TLSR8278_ANALOG_CF_TRIES     20000u
/*
 * The analog port (0xb8-0xba, not documented): after each access the code
 * waits for 0xba bit 0 to clear. 20000 polls is over a millisecond at any
 * CPU clock. A port that never clears it would hang the boot before the
 * watchdog runs, so the wait is bounded and gives up by rebooting, as the
 * wait on analog 0x88 does.
 */
#define TLSR8278_ANALOG_PORT_TRIES   20000u

/* Timer2 as watchdog (DS-TLSR8278 5.1.6): reset when tick[31:18] reaches the capture. */
#define TLSR8278_TIMER_CTRL             TLSR8278_REG32(0x00800620u)
#define TLSR8278_TIMER2_TICK            TLSR8278_REG32(0x00800638u)
#define TLSR8278_TIMER2_EN              BIT(6)
#define TLSR8278_TIMER2_MODE            (BIT(7) | BIT(8))
#define TLSR8278_WATCHDOG_CAPTURE_SHIFT 9
#define TLSR8278_WATCHDOG_CAPTURE       GENMASK(22, 9)
#define TLSR8278_WATCHDOG_EN            BIT(23)
#define TLSR8278_TIMER_STATUS_ALL       GENMASK(31, 24)
#define TLSR8278_WATCHDOG_CAPTURE_MAX   0x3fffu

/*
 * This setup writes analog registers, among them analog 0x0c, and switches
 * the CPU clock, so it runs from .ram_code: the low flash region the boot ROM
 * copies to SRAM and reset.S locks into the instruction cache, with no flash
 * fetch while it runs.
 */
#define TLSR8278_RAM_CODE __attribute__((noinline, section(".ram_code")))

/* Reboot now (0x6f bit 5, reset all, DS-TLSR8278 2.4, as sys_arch_reboot()); never returns. */
TLSR8278_RAM_CODE static void tlsr8278_reboot_now(void)
{
	TLSR8278_REG8(0x0080006fu) = BIT(5);
	for (;;) {
	}
}

/* Waits for the analog port; reboots if it stays busy (see TLSR8278_ANALOG_PORT_TRIES). */
TLSR8278_RAM_CODE static void tlsr8278_analog_wait(void)
{
	uint32_t i;

	for (i = 0u; i < TLSR8278_ANALOG_PORT_TRIES; i++) {
		if ((TLSR8278_ANALOG_PORT_CTRL & TLSR8278_ANALOG_PORT_BUSY) == 0u) {
			return;
		}
	}
	tlsr8278_reboot_now();
}

TLSR8278_RAM_CODE static uint8_t tlsr8278_analog_read(uint8_t addr)
{
	uint8_t data;

	TLSR8278_ANALOG_PORT_ADDR = addr;
	TLSR8278_ANALOG_PORT_CTRL = TLSR8278_ANALOG_PORT_START;
	tlsr8278_analog_wait();
	data = TLSR8278_ANALOG_PORT_DATA;
	TLSR8278_ANALOG_PORT_CTRL = 0u;
	return data;
}

TLSR8278_RAM_CODE static void tlsr8278_analog_write(uint8_t addr, uint8_t value)
{
	TLSR8278_ANALOG_PORT_ADDR = addr;
	TLSR8278_ANALOG_PORT_DATA = value;
	TLSR8278_ANALOG_PORT_CTRL = TLSR8278_ANALOG_PORT_START | TLSR8278_ANALOG_PORT_WRITE;
	tlsr8278_analog_wait();
	TLSR8278_ANALOG_PORT_CTRL = 0u;
}

#if CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS > 0
/*
 * The watchdog from the first instructions of the SoC setup: a boot that
 * hangs before whoever owns the watchdog later (a boot guard at POST_KERNEL)
 * is reset instead of staying stuck until a power cycle or an SWS probe. The
 * period is computed for the 48 MHz clock that the setup's clock_init() sets;
 * until then the CPU runs slower and the period is longer. Whoever takes the
 * watchdog over rewrites 0x620 and feeds it; nobody feeds it before that, so
 * the period must exceed the boot time to that point (milliseconds).
 */
#define TLSR8278_EARLY_WD_SCLK_HZ 48000000u

TLSR8278_RAM_CODE static void tlsr8278_early_watchdog_start(void)
{
	uint32_t capture = ((uint64_t)CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS *
			    (TLSR8278_EARLY_WD_SCLK_HZ / 1000u)) >> 18;
	uint32_t ctrl;

	if (capture < 1u) {
		capture = 1u;
	} else if (capture > TLSR8278_WATCHDOG_CAPTURE_MAX) {
		capture = TLSR8278_WATCHDOG_CAPTURE_MAX;
	}
	/* Timer2 in system clock mode; leave the write-one-to-clear status byte alone. */
	ctrl = TLSR8278_TIMER_CTRL &
	       ~(TLSR8278_TIMER2_MODE | TLSR8278_WATCHDOG_CAPTURE | TLSR8278_TIMER_STATUS_ALL);
	TLSR8278_TIMER2_TICK = 0u;
	TLSR8278_TIMER_CTRL = ctrl | TLSR8278_TIMER2_EN |
				(capture << TLSR8278_WATCHDOG_CAPTURE_SHIFT) | TLSR8278_WATCHDOG_EN;
}
#endif

/*
 * After the wait on analog 0x88 bit 7: up to three checks of analog 0x88
 * bit 5, writing 0x4e and then 0x4b to analog 0x01 while it stays clear,
 * with analog 0x81 bit 6 set meanwhile. These analog registers are not
 * documented.
 */
TLSR8278_RAM_CODE static __maybe_unused void tlsr8278_analog88_check(void)
{
	uint8_t v81 = tlsr8278_analog_read(0x81);

	tlsr8278_analog_write(0x81, v81 | 0x40);
	for (uint32_t n = 0u; n < 3u; n++) {
		for (volatile uint32_t d = 0u; d < 0x1eu; d++) {
		}
		if ((tlsr8278_analog_read(0x88) & 0x20) != 0u) {
			break;
		}
		if (n == 0u) {
			tlsr8278_analog_write(0x01, 0x4e);
		} else if (n == 1u) {
			tlsr8278_analog_write(0x01, 0x4b);
		}
	}
	tlsr8278_analog_write(0x81, v81 & (uint8_t)~0x40);
}

/*
 * The TLSR8278 power-on setup: analog register writes (not documented), the
 * DMA and GPIO register writes, the wait on analog 0x88 bit 7, then the
 * system timer and the CPU clock. Runs before any PRE_KERNEL device,
 * after the .data copy (see the note in reset.S), with interrupts still off.
 */
#include "setup/tlsr8278_setup.h"

/*
 * The SoC setup in setup/tlsr8278_setup.S: chip_init(0x43, 0) and
 * clock_init(0x20) (what each writes and waits for is described there). The
 * early watchdog is armed first.
 */
typedef void (*tlsr8278_setup_chip_init_t)(char mode, char cap);
typedef void (*tlsr8278_setup_clock_init_t)(char clk);

TLSR8278_RAM_CODE void soc_early_init_hook(void)
{
#if CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS > 0
	tlsr8278_early_watchdog_start();
#endif
	TLSR8278_SETUP_FN(tlsr8278_setup_chip_init_t, TLSR8278_SETUP_OFF_CHIP_INIT)(0x43, 0);
	TLSR8278_SETUP_FN(tlsr8278_setup_clock_init_t, TLSR8278_SETUP_OFF_CLOCK_INIT)(0x20);
}

#if defined(CONFIG_SOC_TLSR8278_SUSPEND)
#include "tlsr8278_suspend.h"

/*
 * Suspend: register and analog writes in a fixed order around the suspend
 * entry (0x6f = 0x81). Most of the analog bits they set are not documented.
 * Everything is in .ram_code: the CPU clock changes, and between the flash
 * commands 0xb9 and 0xab the flash is in deep power-down.
 */
#define TLSR8278_STIMER_COUNT           TLSR8278_REG32(0x00800740u)
#define TLSR8278_TIMER_32K_WRITE        TLSR8278_REG32(0x0080074cu)
#define TLSR8278_TIMER_32K_READ         TLSR8278_REG32(0x00800750u)
#define TLSR8278_TIMER_32K_CAL          TLSR8278_REG32(0x00800754u)
#define TLSR8278_STIMER_STATUS          TLSR8278_REG8(0x0080074bu)
#define TLSR8278_STIMER_STATUS_BIT5     BIT(5) /* 0x74b, not documented */
#define TLSR8278_STIMER_STATUS_32K_BUSY BIT(6) /* 0x74b: 32 kHz timer read busy */
#define TLSR8278_STIMER_STATUS_BIT3     BIT(3) /* 0x74b, not documented */
#define TLSR8278_STIMER_CTRL_BIT2       BIT(2) /* 0x74a, not documented */
#define TLSR8278_STIMER_CTRL_32K_WRITE  BIT(0) /* 0x74a: 32 kHz timer write mode */
#define TLSR8278_IRQ_ENABLE             TLSR8278_REG8(0x00800643u)
#define TLSR8278_CLKSEL                 TLSR8278_REG8(0x00800066u)
#define TLSR8278_PWDNEN                 TLSR8278_REG8(0x0080006fu)
#define TLSR8278_FLASH_SPI_DATA         TLSR8278_REG8(0x0080000cu)
#define TLSR8278_FLASH_SPI_CTRL         TLSR8278_REG8(0x0080000du)
#define TLSR8278_REG_5A1                TLSR8278_REG8(0x008005a1u)
#define TLSR8278_REG_602                TLSR8278_REG8(0x00800602u) /* not documented */

#define TLSR8278_SHORT_SLEEP     0x312fu     /* system ticks: up to this, wait instead */
#define TLSR8278_SUSPEND_EARLY   0x24b0u     /* ticks the wake-up takes */
#define TLSR8278_LONG_SLEEP      0x0ff00001u /* ticks: the 32 kHz conversion that cannot overflow */
#define TLSR8278_WAKE_SETTLE     0x12bu      /* delay loop after the wake */

/* Masks 0x748 bit 2, clears 0x74a bits 0-3 (system timer off), waits for
 * 0x74b bit 5 to go from 0 to 1, then sets 0x74a bit 2 (0x748, 0x74a bit 2
 * and 0x74b bit 5 are not documented); returns the 32 kHz timer (0x750).
 */
TLSR8278_RAM_CODE static uint32_t tlsr8278_stimer_follow_32k(void)
{
	TLSR8278_REG_748 &= (uint8_t)~TLSR8278_REG_748_BIT2;
	TLSR8278_STIMER_CTRL &= 0xf0u;
	while ((TLSR8278_STIMER_STATUS & TLSR8278_STIMER_STATUS_BIT5) != 0u) {
	}
	while ((TLSR8278_STIMER_STATUS & TLSR8278_STIMER_STATUS_BIT5) == 0u) {
	}
	TLSR8278_STIMER_CTRL |= TLSR8278_STIMER_CTRL_BIT2;
	return TLSR8278_TIMER_32K_READ;
}

/* Writes the 32 kHz timer (0x74c, write mode 0x74a bit 0) once its read busy
 * bit clears, then sets 0x74b bit 3 and waits for it to clear (not
 * documented). The suspend writes its wake time this way.
 */
TLSR8278_RAM_CODE static void tlsr8278_32k_set_wakeup(uint32_t tick)
{
	TLSR8278_STIMER_CTRL |= TLSR8278_STIMER_CTRL_32K_WRITE;
	while ((TLSR8278_STIMER_STATUS & TLSR8278_STIMER_STATUS_32K_BUSY) != 0u) {
	}
	TLSR8278_TIMER_32K_WRITE = tick;
	TLSR8278_STIMER_STATUS = TLSR8278_STIMER_STATUS_BIT3;
	while ((TLSR8278_STIMER_STATUS & TLSR8278_STIMER_STATUS_BIT3) != 0u) {
	}
}

TLSR8278_RAM_CODE static void tlsr8278_flash_command(uint8_t cmd)
{
	TLSR8278_FLASH_SPI_CTRL = 0u;
	TLSR8278_FLASH_SPI_DATA = cmd;
	for (volatile uint32_t d = 0u; d < 2u; d++) {
	}
	TLSR8278_FLASH_SPI_CTRL = 1u;
}

/* Flash deep power-down, the suspend entry (0x6f = 0x81: suspend enable and
 * stall, DS-TLSR8278 Table 2-3), then, after the wake, flash release and
 * analog writes (analog 0x34, 0xe4, 0x82, 0x2c and 0x05 are not documented).
 */
TLSR8278_RAM_CODE static void tlsr8278_suspend_enter(void)
{
	uint8_t v;

	tlsr8278_analog_write(0x34, 0x85);
	tlsr8278_analog_write(0xe4, 0x10);
	tlsr8278_analog_write(0x82, 0x85);
	tlsr8278_flash_command(0xb9); /* flash deep power-down */
	TLSR8278_REG_5A1 = 0u;
	TLSR8278_PWDNEN = 0x81u;
	for (volatile uint8_t d = 0u; d < 0x30u; d++) {
	}
	TLSR8278_REG_5A1 = 0x0fu;
	tlsr8278_flash_command(0xab); /* flash release from deep power-down */
	v = tlsr8278_analog_read(0x2c);
	tlsr8278_analog_write(0x2c, v | 0x20u);
	tlsr8278_analog_write(0x2c, v & (uint8_t)~0x20u);
	v = tlsr8278_analog_read(0x05);
	tlsr8278_analog_write(0x05, v | 0x08u);
	tlsr8278_analog_write(0x05, v & (uint8_t)~0x08u);
	tlsr8278_analog_write(0x82, 0x65);
	tlsr8278_analog_write(0xe4, 0x00);
	tlsr8278_analog_write(0x34, 0x80);
	for (volatile uint32_t d = 0u; d <= TLSR8278_WAKE_SETTLE; d++) {
	}
	/* Three waits for analog 0x88 bit 7, each bounded, and a reboot if it
	 * never comes, as at power-on.
	 */
	for (uint32_t n = 0u; n < 3u; n++) {
		uint32_t i;

		for (i = 0u; i < TLSR8278_ANALOG88_TRIES; i++) {
			if ((tlsr8278_analog_read(0x88) & 0x80u) != 0u) {
				break;
			}
		}
		if (i == TLSR8278_ANALOG88_TRIES) {
			TLSR8278_PWDNEN = BIT(5);
			for (;;) {
			}
		}
	}
	tlsr8278_analog88_check();
}

TLSR8278_RAM_CODE uint32_t tlsr8278_suspend(uint8_t wakeup_src, uint32_t wakeup_tick)
{
	uint32_t cal = TLSR8278_TIMER_32K_CAL;
	bool timer = (wakeup_src & TLSR8278_WAKEUP_TIMER) != 0u;
	bool long_sleep = false;
	uint32_t t_start, k32_start, k32_wake, k32_end, tick, st;
	uint8_t irq_en, clk;

	if (timer) {
		uint32_t span = wakeup_tick - TLSR8278_STIMER_COUNT;

		if (span > 0xe0000000u) {
			return tlsr8278_analog_read(0x44) & 0x1fu;
		}
		if (span <= TLSR8278_SHORT_SLEEP) {
			uint32_t t0 = TLSR8278_STIMER_COUNT;

			tlsr8278_analog_write(0x44, 0x1d);
			do {
				st = tlsr8278_analog_read(0x44) & 0x1du;
				if (TLSR8278_STIMER_COUNT - t0 >= span) {
					return st;
				}
			} while (st == 0u);
			return st;
		}
		long_sleep = span >= TLSR8278_LONG_SLEEP;
	}

	irq_en = TLSR8278_IRQ_ENABLE;
	TLSR8278_IRQ_ENABLE = 0u;
	t_start = TLSR8278_STIMER_COUNT + 0xf0u;
	k32_start = tlsr8278_stimer_follow_32k();

	tlsr8278_analog_write(0x26, wakeup_src);
	tlsr8278_analog_write(0x44, 0x1f);
	clk = TLSR8278_CLKSEL;
	TLSR8278_CLKSEL = 0u;
	tlsr8278_analog_write(0x04, 0x48);
	tlsr8278_analog_write(0x7e, 0x00);
	tlsr8278_analog_write(0x2b, 0x4e);
	tlsr8278_analog_write(0x2c, (wakeup_src & 0xa0u) != 0u ? 0x1e : 0x1f);
	tlsr8278_analog_write(0x06, (tlsr8278_analog_read(0x06) & 0x3fu) | 0x80u);
	TLSR8278_REG_602 = 8u;
	tlsr8278_analog_write(0x7f, 0x01);
	tlsr8278_analog_write(0x1f, 0xfd);

	uint32_t target = wakeup_tick - TLSR8278_SUSPEND_EARLY;

	if (!long_sleep) {
		k32_wake = ((target - t_start) * 16u + (cal >> 1)) / cal + k32_start;
	} else {
		k32_wake = ((target - t_start) / cal) * 16u + k32_start;
	}
	tlsr8278_32k_set_wakeup(k32_wake);
	if ((tlsr8278_analog_read(0x44) & 0x1fu) == 0u) {
		tlsr8278_suspend_enter();
	}
	k32_end = tlsr8278_stimer_follow_32k();
	if (!long_sleep) {
		tick = ((cal * (k32_end - k32_start)) >> 4) + t_start;
	} else {
		tick = cal * ((k32_end - k32_start) >> 4) + t_start;
	}
	tick += 0x140u;
	if (timer && (wakeup_tick - 0x50u - tick) > 0x40000000u) {
		tick = wakeup_tick - 0xa0u;
	}
	TLSR8278_STIMER_COUNT = tick;
	TLSR8278_STIMER_CTRL |= TLSR8278_STIMER_CTRL_EN | TLSR8278_STIMER_CTRL_CAL_EN;
	TLSR8278_REG_748 |= TLSR8278_REG_748_BIT2;
	TLSR8278_CLKSEL = clk;
	st = tlsr8278_analog_read(0x44);
	if ((st & TLSR8278_WAKEUP_STATUS_TIMER) != 0u && timer) {
		while ((TLSR8278_STIMER_COUNT - wakeup_tick) > 0x40000000u) {
		}
	}
	TLSR8278_IRQ_ENABLE = irq_en;
	return st != 0u ? (st | TLSR8278_STATUS_ENTER_SUSPEND) : 0x80u;
}

#if defined(CONFIG_SOC_TLSR8278_DEEP_SLEEP)
/*
 * Deep sleep with no timer wake: interrupts off, the system timer made to
 * follow the 32 kHz timer, analog 0x26 = the wake sources (the wakeup
 * enables, DS-TLSR8278 Table 2-6), analog 0x44 = 0x1f, the clock select 0,
 * then analog 0x7e = 0x30, 0x2b = 0xce, 0x2c = 0x1e or 0x1f, 0x06 bits 7:6
 * set, 0x800602 = 8, analog 0x7f = 1 and 0x1f = 0xf5 (not documented), and
 * tlsr8278_suspend_enter(). The 32 kHz timer's wake value is not written: no
 * timer wake source is enabled. The chip powers down in sleep_start; a wake
 * source starts it again through the boot ROM. If analog 0x44 already shows
 * a wake source, or the chip comes back from sleep_start, it reboots
 * (0x6f = 0x20, reset all).
 */
TLSR8278_RAM_CODE FUNC_NORETURN void tlsr8278_deep_sleep(uint8_t wakeup_src)
{
	TLSR8278_IRQ_ENABLE = 0u;
	(void)tlsr8278_stimer_follow_32k();
	tlsr8278_analog_write(0x26, wakeup_src);
	tlsr8278_analog_write(0x44, 0x1f);
	TLSR8278_CLKSEL = 0u;
	tlsr8278_analog_write(0x7e, 0x30);
	tlsr8278_analog_write(0x2b, 0xce);
	tlsr8278_analog_write(0x2c, (wakeup_src & 0xa0u) != 0u ? 0x1e : 0x1f);
	tlsr8278_analog_write(0x06, (tlsr8278_analog_read(0x06) & 0x3fu) | 0xc0u);
	TLSR8278_REG_602 = 8u;
	tlsr8278_analog_write(0x7f, 0x01);
	tlsr8278_analog_write(0x1f, 0xf5);
	if ((tlsr8278_analog_read(0x44) & 0x1fu) == 0u) {
		tlsr8278_suspend_enter();
	}
	TLSR8278_PWDNEN = BIT(5);
	for (;;) {
	}
}
#endif /* CONFIG_SOC_TLSR8278_DEEP_SLEEP */

#if defined(CONFIG_SOC_TLSR8278_IDLE_SUSPEND)
/*
 * Suspend in the idle thread (arch_cpu_idle(), ARCH_HAS_CUSTOM_CPU_IDLE),
 * while an application allows it (tlsr8278_idle_suspend()): when the system
 * timer compare (the next kernel timeout, set whenever the timeouts change:
 * the kernel is tickless) is at least the minimum residency away, the chip
 * suspends until SOC_TLSR8278_IDLE_SUSPEND_EARLY_US before it, or one of the
 * application's wake sources, and calls the application's resumed() after
 * every suspend, interrupts still locked (the RF transceiver is powered down
 * in suspend, DS-TLSR8278 2.5.2, so an application sets the radio up again
 * there). The compare is left as it is: the suspend restores the system
 * timer to no later than its wake target, so the compare still lies ahead
 * and fires as usual. Otherwise, and whenever nothing is
 * allowed, it waits for an interrupt as the arch's own idle does, at the cost
 * of one flag test: Zephyr's PM held interrupts off for its policy on every
 * idle entry, long enough on the TC32 to make BLE connection events late.
 */
#define TLSR8278_STIMER_IRQ_AT TLSR8278_REG32(0x00800744u)
#define TLSR8278_IDLE_MIN_TICKS                                                                    \
	((CONFIG_SOC_TLSR8278_IDLE_SUSPEND_MIN_US +                                                \
	  CONFIG_SOC_TLSR8278_IDLE_SUSPEND_EARLY_US) *                                             \
	 16u)

static volatile uint8_t tlsr8278_idle_src;
static bool (*tlsr8278_idle_allowed)(void);
static void (*tlsr8278_idle_resumed)(uint32_t status);

void tlsr8278_idle_suspend(uint8_t wakeup_src, bool (*allowed)(void),
			   void (*resumed)(uint32_t status))
{
	unsigned int key = irq_lock();

	tlsr8278_idle_allowed = allowed;
	tlsr8278_idle_resumed = resumed;
	tlsr8278_idle_src = wakeup_src & (uint8_t)~TLSR8278_WAKEUP_TIMER;
	irq_unlock(key);
}

/* As the arch's: return only once an interrupt has been taken. */
static ALWAYS_INLINE void tlsr8278_wait_for_interrupt(void)
{
	uint32_t taken = z_tc32_irq_count;

	arch_irq_unlock(1u);
	while (z_tc32_irq_count == taken) {
	}
}

/*
 * Called with interrupts locked; returns with them unlocked. After a suspend
 * it returns only once every thread the wake made ready has run: the wake
 * (a pad, or the 32 kHz timer's wake value) takes no interrupt, so no
 * interrupt exit switches threads after it, and resumed() runs with
 * interrupts locked, where a thread it readies (a semaphore given, a work item
 * submitted) is only queued, the switch being left to the lock's release.
 * Without the switch here the idle loop would suspend again at once, with the
 * thread still waiting and the pad still pressed.
 */
void arch_cpu_idle(void)
{
	uint8_t src = tlsr8278_idle_src;

	if (src != 0u) {
		uint32_t compare = TLSR8278_STIMER_IRQ_AT;

		if ((int32_t)(compare - TLSR8278_STIMER_COUNT) >=
			    (int32_t)TLSR8278_IDLE_MIN_TICKS &&
		    (tlsr8278_idle_allowed == NULL || tlsr8278_idle_allowed())) {
			void (*resumed)(uint32_t status) = tlsr8278_idle_resumed;
			uint32_t st = tlsr8278_suspend(
				src | TLSR8278_WAKEUP_TIMER,
				compare - CONFIG_SOC_TLSR8278_IDLE_SUSPEND_EARLY_US * 16u);

			/* Before any interrupt runs: what the suspend reset is set again. */
			if (resumed != NULL) {
				resumed(st);
			}
			arch_irq_unlock(1u);
			/* The highest ready thread, if it is not this one. */
			z_reschedule_unlocked();
			return;
		}
	}
	tlsr8278_wait_for_interrupt();
}
#endif /* CONFIG_SOC_TLSR8278_IDLE_SUSPEND */

void tlsr8278_pad_wakeup(uint8_t port, uint8_t mask, uint8_t low, bool enable)
{
	uint8_t pol = 0x21u + port;
	uint8_t en = 0x27u + port;
	unsigned int key = irq_lock();

	tlsr8278_analog_write(pol, (tlsr8278_analog_read(pol) & (uint8_t)~mask) | (low & mask));
	tlsr8278_analog_write(en, enable ? (tlsr8278_analog_read(en) | mask)
					 : (tlsr8278_analog_read(en) & (uint8_t)~mask));
	irq_unlock(key);
}
#endif /* CONFIG_SOC_TLSR8278_SUSPEND */

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	(void)irq_lock();
	TLSR827X_PWDNEN = TLSR827X_PWDNEN_RESET_ALL;
	for (;;) {
	}
}
