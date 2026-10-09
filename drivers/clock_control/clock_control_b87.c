/*
 * Telink TLSR8278 module clocks (DS-TLSR8278 4.3).
 *
 * A clock is a bit of the clock enable registers 0x63-0x65 (Table 4-1),
 * numbered 8 * (register - 0x63) + bit; 1 runs the module's clock. Each
 * change is a read-modify-write with interrupts locked. The rate of every
 * module clock is the system clock's (tlsr8278_clock.h).
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_clock

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <tlsr8278_clock.h>

#define CLKEN_BASE  DT_INST_REG_ADDR(0)
#define CLOCK_COUNT 24U

static int clock_id(clock_control_subsys_t sys, uintptr_t *reg, uint8_t *bit)
{
	uintptr_t id = (uintptr_t)sys;

	if (id >= CLOCK_COUNT) {
		return -EINVAL;
	}
	*reg = CLKEN_BASE + (id >> 3);
	*bit = BIT(id & 7U);
	return 0;
}

static int clock_set(clock_control_subsys_t sys, bool on)
{
	uintptr_t reg;
	uint8_t bit;
	unsigned int key;
	int err = clock_id(sys, &reg, &bit);

	if (err != 0) {
		return err;
	}
	key = irq_lock();
	sys_write8(on ? (sys_read8(reg) | bit) : (sys_read8(reg) & (uint8_t)~bit), reg);
	irq_unlock(key);
	return 0;
}

static int clock_tlsr8278_on(const struct device *dev, clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	return clock_set(sys, true);
}

static int clock_tlsr8278_off(const struct device *dev, clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	return clock_set(sys, false);
}

static enum clock_control_status clock_tlsr8278_get_status(const struct device *dev,
							   clock_control_subsys_t sys)
{
	uintptr_t reg;
	uint8_t bit;

	ARG_UNUSED(dev);
	if (clock_id(sys, &reg, &bit) != 0) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}
	return (sys_read8(reg) & bit) != 0U ? CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
}

static int clock_tlsr8278_get_rate(const struct device *dev, clock_control_subsys_t sys,
				   uint32_t *rate)
{
	uintptr_t reg;
	uint8_t bit;
	int err = clock_id(sys, &reg, &bit);

	ARG_UNUSED(dev);
	if (err != 0) {
		return err;
	}
	*rate = tlsr8278_sysclk_hz();
	return 0;
}

static DEVICE_API(clock_control, clock_tlsr8278_api) = {
	.on = clock_tlsr8278_on,
	.off = clock_tlsr8278_off,
	.get_status = clock_tlsr8278_get_status,
	.get_rate = clock_tlsr8278_get_rate,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &clock_tlsr8278_api);
