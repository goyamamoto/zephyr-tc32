/*
 * Telink TLSR8278 hardware information.
 *
 * - Device ID: the 128-bit unique ID the internal flash is preloaded with
 *   (DS-TLSR8278 2.1.3): the flash's command 0x4b, four octets of 0 (three
 *   address octets and a dummy one), then the 16 octets of the ID, in the
 *   order the flash sends them.
 * - Reset cause, taken once at boot, before anything can clear or set the
 *   flags: the watchdog's reset status (0x72 bit 0, DS-TLSR8278 5.1.6, write
 *   one to clear) and the wakeup status of analog register 0x44 (Table 2-6),
 *   which a wake source sets for a wake from deep sleep. Analog 0x44 is
 *   cleared by writing its bits as ones. Neither flag set reads as a power-on.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_hwinfo

#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <tlsr8278_analog.h>
#include <tlsr8278_mspi.h>

#define RESET_STATUS          DT_INST_REG_ADDR(0)
#define RESET_STATUS_WATCHDOG BIT(0)
#define ANA_WAKE_STATUS       0x44U
#define ANA_WAKE_STATUS_ALL   GENMASK(4, 0)

#define UID_DUMMY 4U
#define UID_SIZE  16U

#define SUPPORTED (RESET_POR | RESET_WATCHDOG | RESET_LOW_POWER_WAKE)

static uint32_t boot_cause;

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	uint8_t uid[UID_SIZE];

	tlsr8278_mspi_read(TLSR8278_FLASH_CMD_READ_UID, 0U, false, UID_DUMMY, uid, sizeof(uid));
	length = MIN(length, sizeof(uid));
	memcpy(buffer, uid, length);
	return (ssize_t)length;
}

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	*cause = boot_cause;
	return 0;
}

int z_impl_hwinfo_clear_reset_cause(void)
{
	sys_write8(RESET_STATUS_WATCHDOG, RESET_STATUS);
	tlsr8278_analog_write(ANA_WAKE_STATUS, ANA_WAKE_STATUS_ALL);
	boot_cause = 0U;
	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = SUPPORTED;
	return 0;
}

static int hwinfo_tlsr8278_init(void)
{
	uint32_t cause = 0U;

	if ((sys_read8(RESET_STATUS) & RESET_STATUS_WATCHDOG) != 0U) {
		cause |= RESET_WATCHDOG;
	}
	if ((tlsr8278_analog_read(ANA_WAKE_STATUS) & ANA_WAKE_STATUS_ALL) != 0U) {
		cause |= RESET_LOW_POWER_WAKE;
	}
	boot_cause = cause != 0U ? cause : RESET_POR;
	return 0;
}

SYS_INIT(hwinfo_tlsr8278_init, PRE_KERNEL_1, 0);
