/*
 * Telink TLSR8278 module clocks for the devicetree: the bits of the clock
 * enable registers 0x63-0x65 (DS-TLSR8278 4.3, Table 4-1), numbered
 * 8 * (register - 0x63) + bit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_B87_CLOCK_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_B87_CLOCK_H_

#define TLSR8278_CLK_SPI    0
#define TLSR8278_CLK_I2C    1
#define TLSR8278_CLK_UART   2
#define TLSR8278_CLK_USB    3
#define TLSR8278_CLK_PWM    4
#define TLSR8278_CLK_QDEC   5
#define TLSR8278_CLK_IR     6
#define TLSR8278_CLK_SWIRE  7
#define TLSR8278_CLK_ZB     8
#define TLSR8278_CLK_STIMER 9
#define TLSR8278_CLK_DMA    10
#define TLSR8278_CLK_ALGM   11
#define TLSR8278_CLK_AES    12
#define TLSR8278_CLK_PKE    15
#define TLSR8278_CLK_AIF    16
#define TLSR8278_CLK_AUDIO  17
#define TLSR8278_CLK_DFIFO  18
#define TLSR8278_CLK_TRNG   19
#define TLSR8278_CLK_MC     20
#define TLSR8278_CLK_MCIC   21

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_B87_CLOCK_H_ */
