/*
 * The TLSR8278 module clock driver on tlsr8278_generic, in the emulator: the
 * enable bits it sets and clears (DS-TLSR8278 Table 4-1) and the rate it
 * gives for each system clock selection (4.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/b87-clock.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

static const struct device *const clk = DEVICE_DT_GET(DT_NODELABEL(clk));

#define CLKEN0 0x00800063U
#define CLKEN1 0x00800064U
#define CLKEN2 0x00800065U
#define CLKSEL 0x00800066U
#define CLKSEH 0x00800070U

static clock_control_subsys_t id(uintptr_t n)
{
	return (clock_control_subsys_t)n;
}

ZTEST(clock_tlsr8278, test_on_off)
{
	uint8_t en0 = sys_read8(CLKEN0), en2 = sys_read8(CLKEN2);

	zassert_true(device_is_ready(clk));
	zassert_ok(clock_control_off(clk, id(TLSR8278_CLK_SPI)));
	zassert_equal(sys_read8(CLKEN0), en0 & (uint8_t)~BIT(0), "0x63 0x%02x", sys_read8(CLKEN0));
	zassert_equal(clock_control_get_status(clk, id(TLSR8278_CLK_SPI)),
		      CLOCK_CONTROL_STATUS_OFF);
	zassert_ok(clock_control_on(clk, id(TLSR8278_CLK_SPI)));
	zassert_equal(sys_read8(CLKEN0), en0 | BIT(0));
	zassert_equal(clock_control_get_status(clk, id(TLSR8278_CLK_SPI)),
		      CLOCK_CONTROL_STATUS_ON);

	/* The DFIFO is bit 2 of the third register; the other bits stay. */
	zassert_ok(clock_control_off(clk, id(TLSR8278_CLK_DFIFO)));
	zassert_equal(sys_read8(CLKEN2), en2 & (uint8_t)~BIT(2));
	zassert_ok(clock_control_on(clk, id(TLSR8278_CLK_DFIFO)));
	zassert_equal(sys_read8(CLKEN2), en2 | BIT(2));

	zassert_equal(clock_control_on(clk, id(24)), -EINVAL);
	zassert_equal(clock_control_get_status(clk, id(24)), CLOCK_CONTROL_STATUS_UNKNOWN);
}

static uint32_t rate_with(uint8_t sel, uint8_t seh)
{
	uint32_t rate = 0U;
	unsigned int key = irq_lock();
	uint8_t sel0 = sys_read8(CLKSEL), seh0 = sys_read8(CLKSEH);

	sys_write8(seh, CLKSEH);
	sys_write8(sel, CLKSEL);
	(void)clock_control_get_rate(clk, id(TLSR8278_CLK_SPI), &rate);
	sys_write8(sel0, CLKSEL);
	sys_write8(seh0, CLKSEH);
	irq_unlock(key);
	return rate;
}

ZTEST(clock_tlsr8278, test_rate)
{
	uint32_t rate = 0U;

	/* The SoC setup's selection: FHS, the 48 MHz from the crystal. */
	zassert_ok(clock_control_get_rate(clk, id(TLSR8278_CLK_SPI), &rate));
	zassert_equal(rate, 48000000U, "rate %u", rate);
	zassert_equal(rate_with(0x00U, 0x00U), 24000000U, "RC 24 MHz");
	zassert_equal(rate_with(0x20U, 0x00U), 48000000U, "FHS 48 MHz");
	zassert_equal(rate_with(0xa0U, 0x00U), 24000000U, "FHS from the RC oscillator");
	zassert_equal(rate_with(0x20U, 0x01U), 24000000U, "FHS from the crystal");
	zassert_equal(rate_with(0x43U, 0x00U), 16000000U, "48 MHz / 3");
	zassert_equal(rate_with(0x60U, 0x00U), 32000000U, "32 MHz");
	zassert_equal(clock_control_get_rate(clk, id(30), &rate), -EINVAL);
}

ZTEST_SUITE(clock_tlsr8278, NULL, NULL, NULL, NULL, NULL);
