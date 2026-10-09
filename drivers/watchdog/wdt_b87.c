/*
 * Telink TLSR8278 watchdog: Timer2 as watchdog (DS-TLSR8278 5.1.6).
 *
 * Timer2 counts the system clock; the chip resets when bits 31:18 of its
 * count reach the 14-bit capture, and the watchdog status bit 0x72 bit 0 is
 * set then (hwinfo reads it). The timer registers are one word at 0x620
 * (Table 5-1): bits 5:0 Timer0's and Timer1's enables and modes, bit 6
 * Timer2's enable, bits 8:7 its mode, bits 22:9 the watchdog capture, bit 23
 * the watchdog enable; the top octet, 0x623, holds the timers' status bits,
 * written as ones to clear them, bit 3 the watchdog's. Every write of the
 * word here keeps Timer0's and Timer1's bits and writes zeros to the status
 * octet, so no other timer's status is cleared.
 *
 * A feed clears the watchdog status (0x623 bit 3) and the count (0x638).
 * One timeout, no callback: nothing happens before the reset. Timer2 stops
 * while the chip is in suspend (DS-TLSR8278 5.3), so the watchdog always
 * pauses in sleep.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_watchdog

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <tlsr8278_clock.h>

#define TMR_CTRL(base)   (base)
#define TMR_STATUS(base) ((base) + 0x03U)
#define TMR2_TICK(base)  ((base) + 0x18U)

#define TMR2_EN          BIT(6)
#define TMR2_MODE        (BIT(7) | BIT(8))
#define WD_CAPTURE_SHIFT 9
#define WD_CAPTURE       GENMASK(22, 9)
#define WD_EN            BIT(23)
#define TMR_STATUS_ALL   GENMASK(31, 24)
#define WD_STATUS        BIT(3)

#define WD_CAPTURE_MAX 0x3fffU
#define WD_UNIT_SHIFT  18U

struct wdt_tlsr8278_config {
	uintptr_t base;
};

struct wdt_tlsr8278_data {
	uint16_t capture; /* 0: no timeout installed */
	bool running;
};

/* The control word with Timer2's bits and the status octet cleared. */
static uint32_t ctrl_others(uintptr_t base)
{
	return sys_read32(TMR_CTRL(base)) & ~(TMR2_EN | TMR2_MODE | WD_CAPTURE | WD_EN |
					       TMR_STATUS_ALL);
}

static void wdt_stop(uintptr_t base)
{
	unsigned int key = irq_lock();

	sys_write32(ctrl_others(base), TMR_CTRL(base));
	irq_unlock(key);
}

static int wdt_tlsr8278_setup(const struct device *dev, uint8_t options)
{
	const struct wdt_tlsr8278_config *cfg = dev->config;
	struct wdt_tlsr8278_data *data = dev->data;
	unsigned int key;

	if ((options & WDT_OPT_PAUSE_HALTED_BY_DBG) != 0U) {
		return -ENOTSUP;
	}
	if (data->capture == 0U) {
		return -EINVAL;
	}
	if (data->running) {
		return -EBUSY;
	}

	key = irq_lock();
	sys_write32(0U, TMR2_TICK(cfg->base));
	sys_write32(ctrl_others(cfg->base) | TMR2_EN |
			    ((uint32_t)data->capture << WD_CAPTURE_SHIFT) | WD_EN,
		    TMR_CTRL(cfg->base));
	irq_unlock(key);
	data->running = true;
	return 0;
}

static int wdt_tlsr8278_disable(const struct device *dev)
{
	const struct wdt_tlsr8278_config *cfg = dev->config;
	struct wdt_tlsr8278_data *data = dev->data;

	if (!data->running && data->capture == 0U) {
		return -EFAULT;
	}
	wdt_stop(cfg->base);
	data->running = false;
	data->capture = 0U;
	return 0;
}

static int wdt_tlsr8278_install_timeout(const struct device *dev,
					const struct wdt_timeout_cfg *timeout)
{
	struct wdt_tlsr8278_data *data = dev->data;
	uint64_t units;

	if (data->running) {
		return -EBUSY;
	}
	if (data->capture != 0U) {
		return -ENOMEM;
	}
	if (timeout->callback != NULL) {
		return -ENOTSUP;
	}
	if ((timeout->flags & WDT_FLAG_RESET_MASK) == WDT_FLAG_RESET_NONE) {
		return -ENOTSUP;
	}
	if (timeout->window.min != 0U || timeout->window.max == 0U) {
		return -EINVAL;
	}

	/* The capture counts 2^18 system clock cycles; the period rounds down, but not below
	 * one.
	 */
	units = ((uint64_t)timeout->window.max * (tlsr8278_sysclk_hz() / 1000U)) >> WD_UNIT_SHIFT;
	if (units > WD_CAPTURE_MAX) {
		return -EINVAL;
	}
	data->capture = (uint16_t)MAX(units, 1U);
	return 0;
}

static int wdt_tlsr8278_feed(const struct device *dev, int channel_id)
{
	const struct wdt_tlsr8278_config *cfg = dev->config;
	struct wdt_tlsr8278_data *data = dev->data;

	if (channel_id != 0 || !data->running) {
		return -EINVAL;
	}
	sys_write8(WD_STATUS, TMR_STATUS(cfg->base));
	sys_write32(0U, TMR2_TICK(cfg->base));
	return 0;
}

static DEVICE_API(wdt, wdt_tlsr8278_api) = {
	.setup = wdt_tlsr8278_setup,
	.disable = wdt_tlsr8278_disable,
	.install_timeout = wdt_tlsr8278_install_timeout,
	.feed = wdt_tlsr8278_feed,
};

static int wdt_tlsr8278_init(const struct device *dev)
{
	const struct wdt_tlsr8278_config *cfg = dev->config;

	if (IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
		wdt_stop(cfg->base);
	}
	return 0;
}

static const struct wdt_tlsr8278_config wdt_tlsr8278_cfg = {
	.base = DT_INST_REG_ADDR(0),
};

static struct wdt_tlsr8278_data wdt_tlsr8278_data;

DEVICE_DT_INST_DEFINE(0, wdt_tlsr8278_init, NULL, &wdt_tlsr8278_data, &wdt_tlsr8278_cfg,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &wdt_tlsr8278_api);
