/*
 * The TLSR8278 flash driver on tlsr8278_generic, in TC32-devtools' emulator,
 * in the storage partition: erase, writes across a page boundary from SRAM
 * and from the flash itself, reads back, the refusals, the parameters and
 * the layout, the image's first octets as the CPU fetches them, and the
 * flash's unique ID read between the writes. With the emulator's program
 * and erase times set (TC32EMU_FLASH_TPP_US, TC32EMU_FLASH_TSE_US) a fetch
 * from the flash while it is busy stops the run, so the same test shows
 * that nothing runs from the flash during a program or an erase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static const struct device *const flash = DEVICE_DT_GET(DT_NODELABEL(flash_ctrl));

#define AREA   DT_REG_ADDR(DT_NODELABEL(storage_partition))
#define SECTOR 4096U

/* Kept in the flash: a write from it goes through the driver's copy. */
static const uint8_t from_flash[100] = {
	0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, [50] = 0x5a, [99] = 0xa5};

static uint8_t buf[600];

static void erased(off_t off, size_t len)
{
	zassert_ok(flash_read(flash, off, buf, len));
	for (size_t i = 0; i < len; i++) {
		zassert_equal(buf[i], 0xff, "0x%x not erased", (unsigned int)(off + i));
	}
}

ZTEST(flash_tlsr8278, test_parameters)
{
	const struct flash_parameters *p = flash_get_parameters(flash);
	uint64_t size = 0;

	zassert_true(device_is_ready(flash));
	zassert_equal(p->write_block_size, 1U);
	zassert_equal(p->erase_value, 0xff);
	zassert_ok(flash_get_size(flash, &size));
	zassert_equal(size, 0x100000U);
	zassert_equal(flash_get_page_count(flash), 256U);
}

ZTEST(flash_tlsr8278, test_erase_write_read)
{
	uint8_t pattern[300];

	for (size_t i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)(i * 7U + 3U);
	}
	zassert_ok(flash_erase(flash, AREA, 2 * SECTOR));
	erased(AREA, sizeof(buf));
	erased(AREA + 2 * SECTOR - sizeof(buf), sizeof(buf));

	/* 300 octets from SRAM at 0x...f0: across the page boundary at 0x...100. */
	zassert_ok(flash_write(flash, AREA + 0xf0, pattern, sizeof(pattern)));
	zassert_ok(flash_read(flash, AREA + 0xf0, buf, sizeof(pattern)));
	zassert_mem_equal(buf, pattern, sizeof(pattern));
	erased(AREA, 0xf0);
	erased(AREA + 0xf0 + sizeof(pattern), 64);

	/* 100 octets from the flash, through the copy, across a page boundary as well. */
	zassert_ok(flash_write(flash, AREA + 0x4e0, from_flash, sizeof(from_flash)));
	zassert_ok(flash_read(flash, AREA + 0x4e0, buf, sizeof(from_flash)));
	zassert_mem_equal(buf, from_flash, sizeof(from_flash));

	/* A program clears bits only. */
	zassert_ok(flash_write(flash, AREA + 0xf0, (uint8_t[]){0x0f}, 1));
	zassert_ok(flash_read(flash, AREA + 0xf0, buf, 1));
	zassert_equal(buf[0], pattern[0] & 0x0f);

	/* The second sector alone erased again; the first keeps its data. */
	zassert_ok(flash_write(flash, AREA + SECTOR, pattern, 16));
	zassert_ok(flash_erase(flash, AREA + SECTOR, SECTOR));
	erased(AREA + SECTOR, 64);
	zassert_ok(flash_read(flash, AREA + 0x4e0, buf, sizeof(from_flash)));
	zassert_mem_equal(buf, from_flash, sizeof(from_flash));
}

ZTEST(flash_tlsr8278, test_image_and_id)
{
	uint8_t id[16], want[16];

	/* What the driver reads at 0 is what the CPU fetches there (the image in slot A). */
	zassert_ok(flash_read(flash, 0, buf, 64));
	zassert_mem_equal(buf, (const void *)0, 64);

	for (int i = 0; i < 16; i++) {
		want[i] = (uint8_t)(0xa0 + i);
	}
	zassert_equal(hwinfo_get_device_id(id, sizeof(id)), 16);
	zassert_mem_equal(id, want, sizeof(id));
}

ZTEST(flash_tlsr8278, test_refusals)
{
	zassert_equal(flash_erase(flash, AREA + 1, SECTOR), -EINVAL, "unaligned offset");
	zassert_equal(flash_erase(flash, AREA, SECTOR / 2), -EINVAL, "part of a sector");
	zassert_equal(flash_erase(flash, 0x100000, SECTOR), -EINVAL, "beyond the flash");
	zassert_equal(flash_read(flash, 0xfffff, buf, 2), -EINVAL, "read past the end");
	zassert_equal(flash_write(flash, -1, buf, 1), -EINVAL, "negative offset");
}

ZTEST_SUITE(flash_tlsr8278, NULL, NULL, NULL, NULL, NULL);
