/*
 * TLSR8278 transactions with the internal SPI flash, for the drivers that
 * talk to the flash itself (CONFIG_SOC_TLSR8278_MSPI): the flash driver and
 * hwinfo's unique ID.
 *
 * The CPU executes from the same flash (XIP), and no instruction or constant
 * can be fetched from it while a transaction is open or while a program or
 * erase is under way. So each function here runs from SRAM (.ram_code) with
 * interrupts locked from the start of its transaction until the flash is
 * idle again, and every buffer it is given must be in SRAM.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR8278_MSPI_H_
#define ZEPHYR_SOC_TELINK_TLSR8278_MSPI_H_

#include <stdbool.h>
#include <stdint.h>

/* Commands of the SPI NOR flashes these chips carry (JEDEC common set). */
#define TLSR8278_FLASH_CMD_WRITE_STATUS 0x01U
#define TLSR8278_FLASH_CMD_PAGE_PROGRAM 0x02U
#define TLSR8278_FLASH_CMD_READ         0x03U
#define TLSR8278_FLASH_CMD_READ_STATUS  0x05U
#define TLSR8278_FLASH_CMD_SECTOR_ERASE 0x20U
#define TLSR8278_FLASH_CMD_READ_UID     0x4bU
#define TLSR8278_FLASH_CMD_JEDEC_ID     0x9fU

/* The first SRAM address: below it lies the flash (DS-TLSR8278 2.1.1). */
#define TLSR8278_SRAM_BASE 0x00840000U

/*
 * One read transaction: cmd, then the 24-bit address (high octet first) when
 * with_addr, then dummy octets of 0, then len octets into buf (in SRAM).
 */
void tlsr8278_mspi_read(uint8_t cmd, uint32_t addr, bool with_addr, uint8_t dummy, uint8_t *buf,
			uint32_t len);

/*
 * Write enable (0x06), then one transaction: cmd, the 24-bit address when
 * with_addr, len octets from data (in SRAM); then the status register (0x05)
 * read until its bit 0 (write in progress) reads 0.
 *
 * Returns 0, or -ETIMEDOUT when the flash still reports a write in progress
 * after timeout_ms.
 */
int tlsr8278_mspi_write(uint8_t cmd, uint32_t addr, bool with_addr, const uint8_t *data,
			uint32_t len, uint32_t timeout_ms);

#endif /* ZEPHYR_SOC_TELINK_TLSR8278_MSPI_H_ */
