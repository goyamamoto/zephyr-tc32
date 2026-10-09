/*
 * The Telink TLSR8278 USB device controller (UDC) driver's board interface.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_USB_UDC_B87_H_
#define ZEPHYR_INCLUDE_DRIVERS_USB_UDC_B87_H_

#include <stdbool.h>

/**
 * @brief One poll of the controller, for CONFIG_UDC_TELINK_B87_EXTERNAL_POLL.
 *
 * The board calls it every millisecond from its own timer interrupt. Call it
 * from an interrupt only. It does nothing before udc_enable(), after
 * udc_disable() and while the chip is in suspend.
 */
void udc_b87_poll(void);

/**
 * @brief Slow the poll down, for CONFIG_UDC_TELINK_B87_SLOW_POLL.
 *
 * @param slow true polls every CONFIG_UDC_TELINK_B87_SLOW_POLL_MS instead of
 *             every millisecond, for a board's low-power state while no host
 *             uses the bus; false polls every millisecond again. Call it from
 *             a thread.
 */
void udc_b87_slow_poll(bool slow);

/**
 * @brief Stop and restart the poll around the chip's suspend.
 *
 * @param entering true before the chip suspends: the poll stops; false after
 *                 the wake: the poll restarts and runs at once. Call it from
 *                 a thread or with interrupts locked.
 */
void udc_b87_chip_suspend(bool entering);

#endif /* ZEPHYR_INCLUDE_DRIVERS_USB_UDC_B87_H_ */
