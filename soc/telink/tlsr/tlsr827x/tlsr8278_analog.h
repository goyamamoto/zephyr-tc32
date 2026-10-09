/*
 * TLSR8278 analog registers (the "afe_" registers of DS-TLSR8278) for the
 * drivers: one register read or written through the analog port, digital
 * registers 0x8000b8-0x8000ba, which DS-TLSR8278 does not document. The
 * register number goes to 0xb8; a read starts with 0xba = 0x40 and takes the
 * value from 0xb9, a write puts the value in 0xb9 first and starts with
 * 0xba = 0x60; both wait while 0xba bit 0 reads 1 and end with 0xba = 0.
 *
 * Interrupt handlers use the port too, so each access runs with interrupts
 * locked; a caller that reads, changes and writes a register back holds them
 * locked across the three (tlsr8278_analog_update()).
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR8278_ANALOG_H_
#define ZEPHYR_SOC_TELINK_TLSR8278_ANALOG_H_

#include <stdint.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define TLSR8278_ANALOG_ADDR  0x008000b8U
#define TLSR8278_ANALOG_DATA  0x008000b9U
#define TLSR8278_ANALOG_CTRL  0x008000baU
#define TLSR8278_ANALOG_BUSY  BIT(0)
#define TLSR8278_ANALOG_WRITE BIT(5)
#define TLSR8278_ANALOG_START BIT(6)

/* With interrupts locked by the caller. */
static ALWAYS_INLINE uint8_t tlsr8278_analog_read_locked(uint8_t reg)
{
	uint8_t v;

	sys_write8(reg, TLSR8278_ANALOG_ADDR);
	sys_write8(TLSR8278_ANALOG_START, TLSR8278_ANALOG_CTRL);
	while ((sys_read8(TLSR8278_ANALOG_CTRL) & TLSR8278_ANALOG_BUSY) != 0U) {
	}
	v = sys_read8(TLSR8278_ANALOG_DATA);
	sys_write8(0U, TLSR8278_ANALOG_CTRL);
	return v;
}

/* With interrupts locked by the caller. */
static ALWAYS_INLINE void tlsr8278_analog_write_locked(uint8_t reg, uint8_t v)
{
	sys_write8(reg, TLSR8278_ANALOG_ADDR);
	sys_write8(v, TLSR8278_ANALOG_DATA);
	sys_write8(TLSR8278_ANALOG_START | TLSR8278_ANALOG_WRITE, TLSR8278_ANALOG_CTRL);
	while ((sys_read8(TLSR8278_ANALOG_CTRL) & TLSR8278_ANALOG_BUSY) != 0U) {
	}
	sys_write8(0U, TLSR8278_ANALOG_CTRL);
}

static inline uint8_t tlsr8278_analog_read(uint8_t reg)
{
	unsigned int key = irq_lock();
	uint8_t v = tlsr8278_analog_read_locked(reg);

	irq_unlock(key);
	return v;
}

static inline void tlsr8278_analog_write(uint8_t reg, uint8_t v)
{
	unsigned int key = irq_lock();

	tlsr8278_analog_write_locked(reg, v);
	irq_unlock(key);
}

/* reg = (reg & ~mask) | (v & mask), with no other access in between. */
static inline void tlsr8278_analog_update(uint8_t reg, uint8_t mask, uint8_t v)
{
	unsigned int key = irq_lock();
	uint8_t old = tlsr8278_analog_read_locked(reg);

	tlsr8278_analog_write_locked(reg, (uint8_t)((old & (uint8_t)~mask) | (v & mask)));
	irq_unlock(key);
}

#endif /* ZEPHYR_SOC_TELINK_TLSR8278_ANALOG_H_ */
