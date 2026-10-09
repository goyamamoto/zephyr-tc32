/*
 * Telink TLSR8278 internal flash: the SPI NOR flash the CPU executes from.
 *
 * Every access is a transaction of soc/telink/tlsr/tlsr827x/tlsr8278_mspi.c,
 * from SRAM with interrupts locked until the flash is idle again, by the
 * flash's own commands: read 0x03, page program 0x02 (after write enable
 * 0x06), sector erase 0x20, then the status register 0x05 read until its
 * write-in-progress bit clears. Addresses are the flash's, whatever slot the
 * boot ROM maps to 0.
 *
 * - A read is cut into transactions of READ_CHUNK octets, so interrupts wait
 *   for one of them at most.
 * - A write is cut at the 256-octet page boundaries. Data the caller keeps in
 *   the flash itself cannot be fetched while the program command is open, so
 *   it goes through a 32-octet buffer on the stack, one page program each.
 * - An erase is 4 KB sectors, one at a time: interrupts wait for one
 *   sector's erase.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_flash_controller

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <tlsr8278_mspi.h>

#define FLASH_NODE  DT_INST_CHILD(0, flash_0)
#define FLASH_SIZE  DT_REG_SIZE(FLASH_NODE)
#define SECTOR      4096U /* what the erase command 0x20 erases */
#define PAGE        256U
#define READ_CHUNK  256U
#define COPY_PIECE  32U
BUILD_ASSERT(CONFIG_SOC_FLASH_TELINK_B87_BUSY_TIMEOUT_MS < 200000, "the system timer's range");

struct flash_tlsr8278_data {
	struct k_sem lock;
};

static const struct flash_parameters flash_tlsr8278_parameters = {
	.write_block_size = DT_PROP(FLASH_NODE, write_block_size),
	.erase_value = 0xff,
};

static bool in_range(off_t offset, size_t len)
{
	return offset >= 0 && (size_t)offset <= FLASH_SIZE && len <= FLASH_SIZE - (size_t)offset;
}

static int flash_tlsr8278_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	struct flash_tlsr8278_data *d = dev->data;
	uint8_t *out = data;

	if (!in_range(offset, len)) {
		return -EINVAL;
	}
	k_sem_take(&d->lock, K_FOREVER);
	while (len > 0U) {
		uint32_t n = MIN(len, READ_CHUNK);

		tlsr8278_mspi_read(TLSR8278_FLASH_CMD_READ, (uint32_t)offset, true, 0U, out, n);
		offset += n;
		out += n;
		len -= n;
	}
	k_sem_give(&d->lock);
	return 0;
}

static int program(uint32_t addr, const uint8_t *src, uint32_t n)
{
	return tlsr8278_mspi_write(TLSR8278_FLASH_CMD_PAGE_PROGRAM, addr, true, src, n,
				   CONFIG_SOC_FLASH_TELINK_B87_BUSY_TIMEOUT_MS);
}

static int flash_tlsr8278_write(const struct device *dev, off_t offset, const void *data,
				size_t len)
{
	struct flash_tlsr8278_data *d = dev->data;
	const uint8_t *src = data;
	const bool copy = (uintptr_t)src < TLSR8278_SRAM_BASE;
	int err = 0;

	if (!in_range(offset, len)) {
		return -EINVAL;
	}
	k_sem_take(&d->lock, K_FOREVER);
	while (err == 0 && len > 0U) {
		/* Up to the end of the page. */
		uint32_t n = MIN(len, PAGE - ((uint32_t)offset & (PAGE - 1U)));

		if (copy) {
			for (uint32_t off = 0U; err == 0 && off < n; off += COPY_PIECE) {
				uint8_t ram[COPY_PIECE];
				uint32_t k = MIN(COPY_PIECE, n - off);

				memcpy(ram, src + off, k);
				err = program((uint32_t)offset + off, ram, k);
			}
		} else {
			err = program((uint32_t)offset, src, n);
		}
		offset += n;
		src += n;
		len -= n;
	}
	k_sem_give(&d->lock);
	return err != 0 ? -EIO : 0;
}

static int flash_tlsr8278_erase(const struct device *dev, off_t offset, size_t size)
{
	struct flash_tlsr8278_data *d = dev->data;
	int err = 0;

	if (!in_range(offset, size) || ((uint32_t)offset % SECTOR) != 0U || (size % SECTOR) != 0U) {
		return -EINVAL;
	}
	k_sem_take(&d->lock, K_FOREVER);
	for (size_t off = 0U; err == 0 && off < size; off += SECTOR) {
		err = tlsr8278_mspi_write(TLSR8278_FLASH_CMD_SECTOR_ERASE, (uint32_t)offset + off,
					  true, NULL, 0U,
					  CONFIG_SOC_FLASH_TELINK_B87_BUSY_TIMEOUT_MS);
	}
	k_sem_give(&d->lock);
	return err != 0 ? -EIO : 0;
}

static const struct flash_parameters *flash_tlsr8278_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);
	return &flash_tlsr8278_parameters;
}

static int flash_tlsr8278_get_size(const struct device *dev, uint64_t *size)
{
	ARG_UNUSED(dev);
	*size = FLASH_SIZE;
	return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static const struct flash_pages_layout flash_tlsr8278_layout = {
	.pages_count = FLASH_SIZE / SECTOR,
	.pages_size = SECTOR,
};

static void flash_tlsr8278_page_layout(const struct device *dev,
				       const struct flash_pages_layout **layout,
				       size_t *layout_size)
{
	ARG_UNUSED(dev);
	*layout = &flash_tlsr8278_layout;
	*layout_size = 1U;
}
#endif

static DEVICE_API(flash, flash_tlsr8278_api) = {
	.read = flash_tlsr8278_read,
	.write = flash_tlsr8278_write,
	.erase = flash_tlsr8278_erase,
	.get_parameters = flash_tlsr8278_get_parameters,
	.get_size = flash_tlsr8278_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_tlsr8278_page_layout,
#endif
};

static int flash_tlsr8278_init(const struct device *dev)
{
	struct flash_tlsr8278_data *d = dev->data;

	k_sem_init(&d->lock, 1, 1);
	return 0;
}

static struct flash_tlsr8278_data flash_tlsr8278_data;

DEVICE_DT_INST_DEFINE(0, flash_tlsr8278_init, NULL, &flash_tlsr8278_data, NULL, POST_KERNEL,
		      CONFIG_FLASH_INIT_PRIORITY, &flash_tlsr8278_api);
