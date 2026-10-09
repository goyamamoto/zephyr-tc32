/*
 * TLSR8278 system clock, as the clock select registers set it (DS-TLSR8278
 * 4.2 and Table 4-1):
 * - 0x66 bits 6:5, the source: 0 the 24 MHz RC oscillator, 1 the high speed
 *   clock FHS, 2 FHS divided by 0x66 bits 4:0, 3 32 MHz (48 MHz * 2/3);
 * - {0x70 bit 0, 0x66 bit 7}, FHS: 0 48 MHz (the 24 MHz crystal doubled),
 *   1 the 24 MHz RC oscillator, 2 and 3 the 24 MHz crystal.
 * The SoC setup selects FHS at 48 MHz. Timer0-2, the SPI master and the
 * flash's SPI run on this clock; the system timer runs at 16 MHz whatever it
 * is (4.3.1).
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR8278_CLOCK_H_
#define ZEPHYR_SOC_TELINK_TLSR8278_CLOCK_H_

#include <stdint.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define TLSR8278_CLKSEL     0x00800066U
#define TLSR8278_CLKSEL_HS  0x00800070U
#define TLSR8278_HZ_48M     48000000U
#define TLSR8278_HZ_32M     32000000U
#define TLSR8278_HZ_24M     24000000U

static inline uint32_t tlsr8278_sysclk_hz(void)
{
	uint8_t sel = sys_read8(TLSR8278_CLKSEL);
	uint8_t fhs_sel = (uint8_t)(((sys_read8(TLSR8278_CLKSEL_HS) & BIT(0)) << 1) | (sel >> 7));
	uint32_t fhs = fhs_sel == 0U ? TLSR8278_HZ_48M : TLSR8278_HZ_24M;
	uint8_t div = sel & 0x1fU;

	switch ((sel >> 5) & 0x3U) {
	case 0U:
		return TLSR8278_HZ_24M;
	case 1U:
		return fhs;
	case 2U:
		/* A divider below 2 is not allowed (Table 4-1). */
		return div >= 2U ? fhs / div : fhs;
	default:
		return TLSR8278_HZ_32M;
	}
}

#endif /* ZEPHYR_SOC_TELINK_TLSR8278_CLOCK_H_ */
