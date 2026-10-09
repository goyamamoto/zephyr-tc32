/*
 * Telink TLSR8278 pin controller (DS-TLSR8278 7.1).
 *
 * For each pin, in this order, each register changed with interrupts locked
 * (the GPIO driver changes the same registers):
 * 1. the pull resistor: two bits a pin in analog 0x0e-0x15, PA0 in bits 1:0
 *    of 0x0e (Table 7-5);
 * 2. the input enable: 0x581 + 8 * port, port C's in analog 0xc0 (Table 7-2);
 * 3. for PA3, PA4, PB6 and PD7, the SPI or the I2C (Table 7-3): the output
 *    select 0x5b6 bit 4 + n and the input selects 0x5b7 bit n (SPI) and
 *    bit 4 + n (I2C), n = 0-3 in that pin order;
 * 4. the function: two bits a pin from 0x5a8, PA0 in bits 1:0 (Table 7-1);
 * 5. the GPIO function bit (0x586 + 8 * port) cleared.
 * The function is set before the GPIO function is turned off: the other
 * order would give the pin its default function in between (7.1.2).
 * TLSR8278_FUNC_GPIO sets the GPIO function bit instead of 3-5 and leaves the
 * SPI/I2C selects and the function bits as they are.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_pinctrl

#include <errno.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <tlsr8278_analog.h>

/* The function and SPI/I2C select registers, 0x5a8-0x5b7. */
#define MUX_BASE      DT_INST_REG_ADDR(0)
#define PIN_MUX(p, n) (MUX_BASE + 2U * (p) + ((n) >> 2))
#define SPI_I2C_OUT   (MUX_BASE + 0x0eU)
#define SPI_I2C_IN    (MUX_BASE + 0x0fU)
/* The ports' GPIO registers (Table 7-2). */
#define GPIO_IE(p)    (0x00800581U + 8U * (p))
#define GPIO_FUNC(p)  (0x00800586U + 8U * (p))

#define ANA_PULL(p, n)   (0x0eU + 2U * (p) + ((n) >> 2))
#define ANA_PORT_C_IE    0xc0U
#define PORT_C           2U

static void reg_update(uintptr_t addr, uint8_t mask, uint8_t v)
{
	unsigned int key = irq_lock();

	sys_write8((sys_read8(addr) & (uint8_t)~mask) | (v & mask), addr);
	irq_unlock(key);
}

/* The index of a pin the SPI and the I2C share (Table 7-3), -1 for any other. */
static int shared_index(uint8_t port, uint8_t pin)
{
	static const uint8_t shared[4][2] = {{0U, 3U}, {0U, 4U}, {1U, 6U}, {3U, 7U}};

	for (size_t i = 0U; i < ARRAY_SIZE(shared); i++) {
		if (shared[i][0] == port && shared[i][1] == pin) {
			return (int)i;
		}
	}
	return -1;
}

static int pin_configure(pinctrl_soc_pin_t p)
{
	uint8_t func = (p >> TLSR8278_FUNC_POS) & TLSR8278_FUNC_MSK;
	uint8_t pin = (p >> TLSR8278_PIN_POS) & TLSR8278_PIN_MSK;
	uint8_t port = (p >> TLSR8278_PORT_POS) & TLSR8278_PORT_MSK;
	uint8_t bus = (p >> TLSR8278_BUS_POS) & TLSR8278_BUS_MSK;
	uint8_t pull = (p >> TLSR8278_PULL_POS) & TLSR8278_PULL_MSK;
	uint8_t shift = 2U * (pin & 3U);
	uint8_t bit = BIT(pin);
	uint8_t ie = (p & TLSR8278_INPUT_EN) != 0U ? bit : 0U;
	int shared = shared_index(port, pin);

	if (func > TLSR8278_FUNC_3 && func != TLSR8278_FUNC_GPIO) {
		return -EINVAL;
	}
	if (bus != TLSR8278_BUS_NONE && (shared < 0 || func == TLSR8278_FUNC_GPIO)) {
		return -EINVAL;
	}

	tlsr8278_analog_update(ANA_PULL(port, pin), (uint8_t)(0x3U << shift),
			       (uint8_t)(pull << shift));
	if (port == PORT_C) {
		tlsr8278_analog_update(ANA_PORT_C_IE, bit, ie);
	} else {
		reg_update(GPIO_IE(port), bit, ie);
	}

	if (func == TLSR8278_FUNC_GPIO) {
		reg_update(GPIO_FUNC(port), bit, bit);
		return 0;
	}

	if (bus != TLSR8278_BUS_NONE) {
		uint8_t out = BIT(4 + shared);
		uint8_t spi_in = BIT(shared), i2c_in = BIT(4 + shared);

		reg_update(SPI_I2C_OUT, out, out);
		reg_update(SPI_I2C_IN, spi_in | i2c_in, bus == TLSR8278_BUS_SPI ? spi_in : i2c_in);
	}
	reg_update(PIN_MUX(port, pin), (uint8_t)(0x3U << shift),
		   (uint8_t)(func << shift));
	reg_update(GPIO_FUNC(port), bit, 0U);
	return 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		int err = pin_configure(pins[i]);

		if (err != 0) {
			return err;
		}
	}
	return 0;
}
