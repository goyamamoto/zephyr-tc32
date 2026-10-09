/*
 * Telink TLSR8278 pin functions for the devicetree (DS-TLSR8278 7.1.1).
 *
 * A pinmux value names the port, the pin, the function and, for the four
 * pins whose function serves the SPI or the I2C (PA3, PA4, PB6, PD7, Table
 * 7-3), which of the two. The function is the 2-bit "Register" value of
 * Table 7-1, or TLSR8278_FUNC_GPIO to give the pin back to the GPIO
 * function. The pin controller node's pull and input properties go in the
 * upper bits (pinctrl_soc.h).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_B87_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_B87_PINCTRL_H_

#define TLSR8278_PORT_A 0
#define TLSR8278_PORT_B 1
#define TLSR8278_PORT_C 2
#define TLSR8278_PORT_D 3

#define TLSR8278_FUNC_0    0
#define TLSR8278_FUNC_1    1
#define TLSR8278_FUNC_2    2
#define TLSR8278_FUNC_3    3
#define TLSR8278_FUNC_GPIO 7

#define TLSR8278_BUS_NONE 0
#define TLSR8278_BUS_SPI  1
#define TLSR8278_BUS_I2C  2

#define TLSR8278_FUNC_POS 0
#define TLSR8278_FUNC_MSK 0x7
#define TLSR8278_PIN_POS  4
#define TLSR8278_PIN_MSK  0x7
#define TLSR8278_PORT_POS 8
#define TLSR8278_PORT_MSK 0x3
#define TLSR8278_BUS_POS  12
#define TLSR8278_BUS_MSK  0x3

#define TLSR8278_PINMUX(port, pin, func, bus)                                                      \
	((((port) & TLSR8278_PORT_MSK) << TLSR8278_PORT_POS) |                                     \
	 (((pin) & TLSR8278_PIN_MSK) << TLSR8278_PIN_POS) |                                        \
	 (((func) & TLSR8278_FUNC_MSK) << TLSR8278_FUNC_POS) |                                     \
	 (((bus) & TLSR8278_BUS_MSK) << TLSR8278_BUS_POS))

/* The SPI master's pins (Table 7-1; Table 7-3 for the SPI/I2C ones). */
#define TLSR8278_PA2_SPI_DO TLSR8278_PINMUX(TLSR8278_PORT_A, 2, TLSR8278_FUNC_0, TLSR8278_BUS_NONE)
#define TLSR8278_PA3_SPI_DI TLSR8278_PINMUX(TLSR8278_PORT_A, 3, TLSR8278_FUNC_0, TLSR8278_BUS_SPI)
#define TLSR8278_PA4_SPI_CK TLSR8278_PINMUX(TLSR8278_PORT_A, 4, TLSR8278_FUNC_0, TLSR8278_BUS_SPI)
#define TLSR8278_PB6_SPI_DI TLSR8278_PINMUX(TLSR8278_PORT_B, 6, TLSR8278_FUNC_1, TLSR8278_BUS_SPI)
#define TLSR8278_PB7_SPI_DO TLSR8278_PINMUX(TLSR8278_PORT_B, 7, TLSR8278_FUNC_1, TLSR8278_BUS_NONE)
#define TLSR8278_PD2_SPI_CN TLSR8278_PINMUX(TLSR8278_PORT_D, 2, TLSR8278_FUNC_0, TLSR8278_BUS_NONE)
#define TLSR8278_PD7_SPI_CK TLSR8278_PINMUX(TLSR8278_PORT_D, 7, TLSR8278_FUNC_0, TLSR8278_BUS_SPI)

/* A pin as GPIO again (a sleep state, for example). */
#define TLSR8278_GPIO(port, pin)                                                                   \
	TLSR8278_PINMUX(port, pin, TLSR8278_FUNC_GPIO, TLSR8278_BUS_NONE)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_B87_PINCTRL_H_ */
