/*
 * The TLSR8278 hwinfo driver on tlsr8278_generic, in TC32-devtools'
 * emulator, whose flash answers the unique ID command 0x4b with the octets
 * 0xa0-0xaf and whose watchdog reset sets 0x72 bit 0.
 *
 * Two boots: the first reads the ID and finds a power-on, then arms Timer2
 * as watchdog (DS-TLSR8278 5.1.6) and waits for it; the second finds the
 * watchdog reset, clears it and finds no cause after that. The emulator
 * keeps the SRAM across the reset, as the chip does, so the stage survives
 * in a noinit variable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#define MAGIC        0x48574946U
#define STAGE_ARMED  1U
#define STAGE_DONE   2U

#define RESET_STATUS 0x00800072U
#define TMR_CTRL     0x00800620U
#define TMR2_TICK    0x00800638U
#define MSPI_CTRL    0x0080000dU

static __noinit volatile uint32_t magic;
static __noinit volatile uint32_t stage;

static bool second_boot(void)
{
	return magic == MAGIC && stage == STAGE_ARMED;
}

ZTEST(hwinfo_tlsr8278, test_1_device_id)
{
	uint8_t id[20];
	uint8_t want[16];

	for (int i = 0; i < 16; i++) {
		want[i] = (uint8_t)(0xa0 + i);
	}
	memset(id, 0x55, sizeof(id));
	zassert_equal(hwinfo_get_device_id(id, sizeof(id)), 16);
	zassert_mem_equal(id, want, 16);
	zassert_equal(id[16], 0x55, "written past the ID");

	/* A shorter buffer gets the ID's first octets. */
	memset(id, 0x55, sizeof(id));
	zassert_equal(hwinfo_get_device_id(id, 5), 5);
	zassert_mem_equal(id, want, 5);
	zassert_equal(id[5], 0x55, "written past the length");

	/* The flash's chip select is high again: the transaction is closed. */
	zassert_equal(sys_read8(MSPI_CTRL) & BIT(0), BIT(0), "chip select low");
}

ZTEST(hwinfo_tlsr8278, test_2_reset_cause)
{
	uint32_t cause = 0xdeadbeefU, supported = 0U;

	zassert_ok(hwinfo_get_supported_reset_cause(&supported));
	zassert_equal(supported, RESET_POR | RESET_WATCHDOG | RESET_LOW_POWER_WAKE);
	zassert_ok(hwinfo_get_reset_cause(&cause));
	if (!second_boot()) {
		zassert_equal(cause, RESET_POR, "first boot: cause 0x%x", cause);
		return;
	}
	zassert_equal(cause, RESET_WATCHDOG, "after the watchdog: cause 0x%x", cause);
	zassert_equal(sys_read8(RESET_STATUS) & BIT(0), BIT(0));
	zassert_ok(hwinfo_clear_reset_cause());
	zassert_equal(sys_read8(RESET_STATUS) & BIT(0), 0U, "0x72 bit 0 not cleared");
	zassert_ok(hwinfo_get_reset_cause(&cause));
	zassert_equal(cause, 0U, "after the clear: cause 0x%x", cause);
}

ZTEST(hwinfo_tlsr8278, test_3_watchdog_reset)
{
	if (second_boot()) {
		stage = STAGE_DONE;
		magic = 0U;
		return;
	}
	magic = MAGIC;
	stage = STAGE_ARMED;
	TC_PRINT("arming the watchdog (capture 1: 2^18 system clock cycles)\n");
	(void)irq_lock();
	sys_write32(0U, TMR2_TICK);
	sys_write32((sys_read32(TMR_CTRL) & 0x3fU) | BIT(6) | (1U << 9) | BIT(23), TMR_CTRL);
	for (;;) {
	}
}

ZTEST_SUITE(hwinfo_tlsr8278, NULL, NULL, NULL, NULL, NULL);
