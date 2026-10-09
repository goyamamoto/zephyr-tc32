/*
 * The TLSR8278 pin controller on tlsr8278_generic, in the emulator: the
 * registers each state leaves (DS-TLSR8278 7.1), read back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#include <tlsr8278_analog.h>

#define TEST_NODE DT_NODELABEL(test_device)

PINCTRL_DT_DEFINE(TEST_NODE);

static const struct pinctrl_dev_config *pcfg = PINCTRL_DT_DEV_CONFIG_GET(TEST_NODE);

#define PA_IE    0x00800581U
#define PA_FUNC  0x00800586U
#define PB_IE    0x00800589U
#define PB_FUNC  0x0080058eU
#define PC_FUNC  0x00800596U
#define MUX_PA0  0x008005a8U
#define MUX_PA4  0x008005a9U
#define MUX_PB4  0x008005abU
#define MUX_PC4  0x008005adU
#define SEL_OUT  0x008005b6U
#define SEL_IN   0x008005b7U

static void preset(void)
{
	/* Every pin a GPIO, every function 3, no select, no pull: what each state must change. */
	sys_write8(0xffU, PA_FUNC);
	sys_write8(0xffU, PB_FUNC);
	sys_write8(0xffU, PC_FUNC);
	sys_write8(0xffU, MUX_PA0);
	sys_write8(0xffU, MUX_PA4);
	sys_write8(0xffU, MUX_PB4);
	sys_write8(0xffU, MUX_PC4);
	sys_write8(0U, SEL_OUT);
	sys_write8(0xffU, SEL_IN);
	sys_write8(0U, PA_IE);
	sys_write8(0U, PB_IE);
	for (uint8_t r = 0x0eU; r <= 0x15U; r++) {
		tlsr8278_analog_write(r, 0U);
	}
	tlsr8278_analog_write(0xc0U, 0U);
}

ZTEST(pinctrl_tlsr8278, test_default_state)
{
	preset();
	zassert_ok(pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT));

	/* PA2-PA4: function 0, GPIO off; the other port A pins stay GPIO. */
	zassert_equal(sys_read8(PA_FUNC), 0xe3U, "PA GPIO bits 0x%02x", sys_read8(PA_FUNC));
	zassert_equal(sys_read8(MUX_PA0) & 0xf0U, 0x00U, "PA2/PA3 functions");
	zassert_equal(sys_read8(MUX_PA0) & 0x0fU, 0x0fU, "PA0/PA1 functions changed");
	zassert_equal(sys_read8(MUX_PA4) & 0x03U, 0x00U, "PA4 function");
	zassert_equal(sys_read8(MUX_PA4) & 0xfcU, 0xfcU, "PA5-PA7 functions changed");
	/* PA3 and PA4 to the SPI: their output selects, their SPI input selects, no I2C input. */
	zassert_equal(sys_read8(SEL_OUT) & 0x30U, 0x30U, "PA3/PA4 output select");
	zassert_equal(sys_read8(SEL_IN) & 0x33U, 0x03U, "PA3/PA4 input select 0x%02x",
		      sys_read8(SEL_IN));
	/* PA3: input on, 10 kOhm pull-up (analog 0x0e bits 7:6 = 3); PA2, PA4: input off, no
	 * pull.
	 */
	zassert_equal(sys_read8(PA_IE), BIT(3), "PA input enables 0x%02x", sys_read8(PA_IE));
	zassert_equal(tlsr8278_analog_read(0x0eU), 0xc0U, "PA0-PA3 pulls");
	zassert_equal(tlsr8278_analog_read(0x0fU), 0x00U, "PA4-PA7 pulls");

	/* PB6 to the I2C: function 1, output select, I2C input select, no SPI input select. */
	zassert_equal(sys_read8(PB_FUNC), 0xbfU, "PB GPIO bits");
	zassert_equal((sys_read8(MUX_PB4) >> 4) & 0x3U, 1U, "PB6 function");
	zassert_equal(sys_read8(SEL_OUT) & BIT(6), BIT(6), "PB6 output select");
	zassert_equal(sys_read8(SEL_IN) & (BIT(2) | BIT(6)), BIT(6), "PB6 input select");
	zassert_equal(sys_read8(PB_IE), BIT(6), "PB6 input enable");

	/* PC5: function 2, input enable in analog 0xc0, 100 kOhm pull-down (0x13 bits 3:2 = 2). */
	zassert_equal(sys_read8(PC_FUNC), 0xdfU, "PC GPIO bits");
	zassert_equal((sys_read8(MUX_PC4) >> 2) & 0x3U, 2U, "PC5 function");
	zassert_equal(tlsr8278_analog_read(0xc0U), BIT(5), "PC input enables");
	zassert_equal(tlsr8278_analog_read(0x13U), 0x08U, "PC4-PC7 pulls");

	/* The selects of PD7 (bit 7 and bit 3/7 of the inputs) are left alone. */
	zassert_equal(sys_read8(SEL_OUT) & BIT(7), 0U, "PD7 output select");
	zassert_equal(sys_read8(SEL_IN) & (BIT(3) | BIT(7)), BIT(3) | BIT(7), "PD7 input select");
}

ZTEST(pinctrl_tlsr8278, test_sleep_state)
{
	preset();
	zassert_ok(pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT));
	zassert_ok(pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP));

	/* PA2-PA4 GPIO again, their functions and selects as the default state left them. */
	zassert_equal(sys_read8(PA_FUNC), 0xffU, "PA GPIO bits");
	zassert_equal(sys_read8(MUX_PA0) & 0xf0U, 0x00U, "PA2/PA3 functions");
	zassert_equal(sys_read8(PA_IE), 0U, "PA input enables");
	zassert_equal(tlsr8278_analog_read(0x0eU), 0xc0U, "PA3 pull-up kept");
}

ZTEST(pinctrl_tlsr8278, test_invalid)
{
	/* A bus select on a pin the SPI and the I2C do not share, an unknown function. */
	const pinctrl_soc_pin_t bad_bus =
		TLSR8278_PINMUX(TLSR8278_PORT_A, 2, TLSR8278_FUNC_0, TLSR8278_BUS_SPI);
	const pinctrl_soc_pin_t bad_func =
		TLSR8278_PINMUX(TLSR8278_PORT_A, 2, 5, TLSR8278_BUS_NONE);
	const pinctrl_soc_pin_t gpio_bus =
		TLSR8278_PINMUX(TLSR8278_PORT_A, 3, TLSR8278_FUNC_GPIO, TLSR8278_BUS_SPI);

	zassert_equal(pinctrl_configure_pins(&bad_bus, 1, PINCTRL_REG_NONE), -EINVAL);
	zassert_equal(pinctrl_configure_pins(&bad_func, 1, PINCTRL_REG_NONE), -EINVAL);
	zassert_equal(pinctrl_configure_pins(&gpio_bus, 1, PINCTRL_REG_NONE), -EINVAL);
}

ZTEST_SUITE(pinctrl_tlsr8278, NULL, NULL, NULL, NULL, NULL);
