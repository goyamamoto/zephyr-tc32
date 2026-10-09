/*
 * TLSR8278 suspend (CONFIG_SOC_TLSR8278_SUSPEND).
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR8278_SUSPEND_H_
#define ZEPHYR_SOC_TELINK_TLSR8278_SUSPEND_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

/* Wake sources (analog 0x26, DS-TLSR8278 Table 2-6). */
#define TLSR8278_WAKEUP_PAD   BIT(3) /* pads set with tlsr8278_pad_wakeup() */
#define TLSR8278_WAKEUP_CORE  BIT(4) /* digital sources in 0x6e, e.g. USB */
#define TLSR8278_WAKEUP_TIMER BIT(5) /* the 32 kHz timer reaching wakeup_tick */

/* Wake status (analog 0x44, DS-TLSR8278 Table 2-6) in the result. */
#define TLSR8278_WAKEUP_STATUS_TIMER  BIT(1)
#define TLSR8278_WAKEUP_STATUS_CORE   BIT(2)
#define TLSR8278_WAKEUP_STATUS_PAD    BIT(3)
#define TLSR8278_STATUS_ENTER_SUSPEND BIT(30)

/*
 * Suspend until a wake source in wakeup_src fires (with TLSR8278_WAKEUP_TIMER,
 * at the latest when the system timer reaches wakeup_tick). Call with
 * interrupts locked; the system timer is restored from the 32 kHz timer, but
 * the kernel's timer compare is not: call sys_clock_set_timeout() afterwards.
 * Returns the wake status, with TLSR8278_STATUS_ENTER_SUSPEND when the chip
 * did suspend; a timer target already past or less than about 0.8 ms away is
 * waited for without suspending.
 */
uint32_t tlsr8278_suspend(uint8_t wakeup_src, uint32_t wakeup_tick);

/*
 * Deep sleep (CONFIG_SOC_TLSR8278_DEEP_SLEEP): the chip powers down until a
 * wake source in wakeup_src fires, then boots again through the boot ROM;
 * RAM is lost, the analog buffer registers 0x3a-0x3c are kept. Reboots at
 * once when a source already fires. Never returns.
 */
FUNC_NORETURN void tlsr8278_deep_sleep(uint8_t wakeup_src);

/*
 * Suspend in the idle thread (CONFIG_SOC_TLSR8278_IDLE_SUSPEND): from now
 * on, whenever the kernel has nothing to run until a timeout far enough away
 * and allowed() (if given, called with interrupts off) says yes, the chip
 * suspends until just before that timeout or a source in wakeup_src (the
 * pads armed with tlsr8278_pad_wakeup()). resumed() (if given) is called
 * after every suspend with its wake status (TLSR8278_WAKEUP_STATUS_*), from
 * the idle thread with interrupts still locked, before any interrupt runs:
 * set up again there what the suspend reset (the radio's registers), and make
 * only ISR-safe calls. wakeup_src 0 stops it.
 */
void tlsr8278_idle_suspend(uint8_t wakeup_src, bool (*allowed)(void),
			   void (*resumed)(uint32_t status));

/*
 * Pad wake for the pins in mask of GPIO port (0 PA ... 3 PD): wake on low for
 * the pins also in low, on high for the others; enable false turns them off.
 * Polarity analog 0x21 + port (1: low), enable analog 0x27 + port
 * (DS-TLSR8278 2.6.4).
 */
void tlsr8278_pad_wakeup(uint8_t port, uint8_t mask, uint8_t low, bool enable);

#endif /* ZEPHYR_SOC_TELINK_TLSR8278_SUSPEND_H_ */
