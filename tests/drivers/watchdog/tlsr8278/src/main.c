/*
 * The TLSR8278 watchdog driver on tlsr8278_generic, in the emulator: the
 * capture it computes, a watchdog fed for longer than its period, the
 * refusals, and Timer0's bits and status left as they were. The reset
 * itself is tests/drivers/watchdog/wdt_basic_api's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

static const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(wdt));

#define TMR_CTRL    0x00800620U
#define TMR_STATUS  0x00800623U
#define TMR0_CAPT   0x00800624U
#define TMR0_TICK   0x00800630U

/* Timer0 in mode 0 runs to a small capture, sets its status, and is stopped. */
static void timer0_status_set(void)
{
	sys_write32(0U, TMR0_TICK);
	sys_write32(1000U, TMR0_CAPT);
	sys_write8(sys_read8(TMR_CTRL) | BIT(0), TMR_CTRL);
	k_busy_wait(100);
	sys_write8(sys_read8(TMR_CTRL) & (uint8_t)~BIT(0), TMR_CTRL);
	zassert_equal(sys_read8(TMR_STATUS) & BIT(0), BIT(0), "Timer0 status not set");
}

ZTEST(wdt_tlsr8278, test_period_and_feed)
{
	struct wdt_timeout_cfg cfg = {
		.window.max = 100U,
		.flags = WDT_FLAG_RESET_SOC,
	};
	uint32_t ctrl;

	zassert_true(device_is_ready(wdt));
	timer0_status_set();
	zassert_equal(wdt_install_timeout(wdt, &cfg), 0);
	zassert_equal(wdt_install_timeout(wdt, &cfg), -ENOMEM, "a second timeout");
	zassert_equal(wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG), -ENOTSUP);
	zassert_ok(wdt_setup(wdt, WDT_OPT_PAUSE_IN_SLEEP));

	/* 100 ms at 48 MHz: 4,800,000 cycles, 18 units of 2^18. Timer2 on, mode 0, watchdog on. */
	ctrl = sys_read32(TMR_CTRL);
	zassert_equal((ctrl >> 9) & 0x3fffU, 18U, "capture %u", (ctrl >> 9) & 0x3fffU);
	zassert_equal(ctrl & (BIT(6) | BIT(7) | BIT(8) | BIT(23)), BIT(6) | BIT(23));
	zassert_equal(sys_read8(TMR_STATUS) & BIT(0), BIT(0), "Timer0 status cleared");

	/* Fed every 40 ms for 400 ms: no reset. */
	for (int i = 0; i < 10; i++) {
		k_msleep(40);
		zassert_ok(wdt_feed(wdt, 0));
	}
	zassert_equal(wdt_feed(wdt, 1), -EINVAL);

	zassert_ok(wdt_disable(wdt));
	ctrl = sys_read32(TMR_CTRL);
	zassert_equal(ctrl & (BIT(6) | BIT(23) | (0x3fffU << 9)), 0U, "watchdog left on");
	zassert_equal(sys_read8(TMR_STATUS) & BIT(0), BIT(0), "Timer0 status cleared");
	zassert_equal(wdt_disable(wdt), -EFAULT);
	/* Not running now: 300 ms without a feed is no reset either. */
	k_msleep(300);
}

ZTEST(wdt_tlsr8278, test_refusals)
{
	struct wdt_timeout_cfg cfg = {
		.window.max = 100U,
		.flags = WDT_FLAG_RESET_SOC,
	};

	cfg.window.min = 10U;
	zassert_equal(wdt_install_timeout(wdt, &cfg), -EINVAL, "a window minimum");
	cfg.window.min = 0U;
	cfg.window.max = 0U;
	zassert_equal(wdt_install_timeout(wdt, &cfg), -EINVAL, "no maximum");
	cfg.window.max = 100000U;
	zassert_equal(wdt_install_timeout(wdt, &cfg), -EINVAL, "beyond 14 bits of 2^18 cycles");
	cfg.window.max = 100U;
	cfg.flags = WDT_FLAG_RESET_NONE;
	zassert_equal(wdt_install_timeout(wdt, &cfg), -ENOTSUP, "no reset");
	zassert_equal(wdt_setup(wdt, 0), -EINVAL, "setup without a timeout");

	/* The shortest: 1 ms rounds up to one unit (5.5 ms). */
	cfg.flags = WDT_FLAG_RESET_SOC;
	cfg.window.max = 1U;
	zassert_ok(wdt_install_timeout(wdt, &cfg));
	zassert_ok(wdt_setup(wdt, 0));
	zassert_equal((sys_read32(TMR_CTRL) >> 9) & 0x3fffU, 1U);
	zassert_ok(wdt_disable(wdt));
}

ZTEST_SUITE(wdt_tlsr8278, NULL, NULL, NULL, NULL, NULL);
