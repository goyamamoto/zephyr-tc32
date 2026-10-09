/*
 * TLSR8278 analog register port, digital registers 0xb8-0xba (not
 * documented): the analog register number to 0xb8; a read writes
 * 0xba = 0x40, waits until 0xba bit 0 reads 0 and reads the value from 0xb9;
 * a write puts the value in 0xb9, writes 0xba = 0x60 and waits the same;
 * both end with 0xba = 0. Callers hold interrupts off around a whole
 * read-modify-write: interrupt handlers use the port too.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR_ANALOG_H_
#define ZEPHYR_SOC_TELINK_TLSR_ANALOG_H_

#include <stdint.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define TLSR_ANA_ADDR 0x008000b8U
#define TLSR_ANA_DATA 0x008000b9U
#define TLSR_ANA_CTRL 0x008000baU

static inline uint8_t tlsr_analog_read(uint8_t addr)
{
	sys_write8(addr, TLSR_ANA_ADDR);
	sys_write8(BIT(6), TLSR_ANA_CTRL);
	while ((sys_read8(TLSR_ANA_CTRL) & BIT(0)) != 0U) {
	}
	uint8_t v = sys_read8(TLSR_ANA_DATA);

	sys_write8(0U, TLSR_ANA_CTRL);
	return v;
}

static inline void tlsr_analog_write(uint8_t addr, uint8_t value)
{
	sys_write8(addr, TLSR_ANA_ADDR);
	sys_write8(value, TLSR_ANA_DATA);
	sys_write8(BIT(6) | BIT(5), TLSR_ANA_CTRL);
	while ((sys_read8(TLSR_ANA_CTRL) & BIT(0)) != 0U) {
	}
	sys_write8(0U, TLSR_ANA_CTRL);
}

#endif /* ZEPHYR_SOC_TELINK_TLSR_ANALOG_H_ */
