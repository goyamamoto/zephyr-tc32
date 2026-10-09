/*
 * TLSR8278 transactions with the internal SPI flash (tlsr8278_mspi.h).
 *
 * The flash's SPI is driven through two digital registers that DS-TLSR8278
 * does not document; the code uses them so:
 * - 0x80000c, data: a write clocks the octet out to the flash and the octet
 *   then on the flash's output in. A read gives the octet last clocked in; in
 *   the read data phase (0x80000d = 0x0a) each read also clocks the next one
 *   in.
 * - 0x80000d, control: bit 0 high puts the chip select high (no
 *   transaction), 0 starts one; 0x0a selects the read data phase; bit 4
 *   reads 1 while an octet is clocked.
 * - The system timer (0x800740, 16 ticks per microsecond, DS-TLSR8278 5.3)
 *   times the waits.
 *
 * Both functions and everything they call are in .ram_code, the SRAM copy
 * the boot ROM makes, and fetch nothing from the flash: no flash code or
 * constant between a transaction's first octet and the flash being idle
 * again. Interrupts stay locked for that long, since their handlers run
 * from the flash.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "tlsr8278_mspi.h"

#define MSPI_DATA         0x0080000cU
#define MSPI_CTRL         0x0080000dU
#define MSPI_CTRL_CS_HIGH BIT(0)
#define MSPI_CTRL_DATA_IN 0x0aU
#define MSPI_CTRL_BUSY    BIT(4)
#define STIMER            0x00800740U
#define STIMER_PER_US     16U

#define FLASH_CMD_WRITE_ENABLE 0x06U
#define FLASH_STATUS_WIP       BIT(0)

/* Before the first status read of a program or erase. */
#define WIP_FIRST_READ_US 100U

#define MSPI_RAM_CODE __attribute__((noinline, section(".ram_code.tlsr8278_mspi")))

static ALWAYS_INLINE void delay_us(uint32_t us)
{
	uint32_t t0 = sys_read32(STIMER);

	while (sys_read32(STIMER) - t0 <= us * STIMER_PER_US) {
	}
}

static ALWAYS_INLINE void octet_wait(void)
{
	while ((sys_read8(MSPI_CTRL) & MSPI_CTRL_BUSY) != 0U) {
	}
}

static ALWAYS_INLINE void octet_out(uint8_t v)
{
	sys_write8(v, MSPI_DATA);
	octet_wait();
}

/* Chip select high for a microsecond, then low, then the command octet. */
static ALWAYS_INLINE void begin(uint8_t cmd)
{
	sys_write8(MSPI_CTRL_CS_HIGH, MSPI_CTRL);
	delay_us(1U);
	sys_write8(0U, MSPI_CTRL);
	octet_out(cmd);
}

static ALWAYS_INLINE void end(void)
{
	sys_write8(MSPI_CTRL_CS_HIGH, MSPI_CTRL);
}

static ALWAYS_INLINE void address(uint32_t addr)
{
	octet_out((uint8_t)(addr >> 16));
	octet_out((uint8_t)(addr >> 8));
	octet_out((uint8_t)addr);
}

MSPI_RAM_CODE void tlsr8278_mspi_read(uint8_t cmd, uint32_t addr, bool with_addr, uint8_t dummy,
				      uint8_t *buf, uint32_t len)
{
	unsigned int key = irq_lock();

	begin(cmd);
	if (with_addr) {
		address(addr);
	}
	for (uint8_t i = 0U; i < dummy; i++) {
		octet_out(0U);
	}
	/* This octet clocks the first data octet in; each read takes one and clocks the next. */
	octet_out(0U);
	sys_write8(MSPI_CTRL_DATA_IN, MSPI_CTRL);
	octet_wait();
	for (uint32_t i = 0U; i < len; i++) {
		buf[i] = sys_read8(MSPI_DATA);
		octet_wait();
	}
	end();
	irq_unlock(key);
}

MSPI_RAM_CODE int tlsr8278_mspi_write(uint8_t cmd, uint32_t addr, bool with_addr,
				      const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
	unsigned int key = irq_lock();
	int ret = -ETIMEDOUT;
	uint32_t t0;

	begin(FLASH_CMD_WRITE_ENABLE);
	end();
	begin(cmd);
	if (with_addr) {
		address(addr);
	}
	for (uint32_t i = 0U; i < len; i++) {
		octet_out(data[i]);
	}
	end();

	/* The status register, read again and again in one transaction. */
	delay_us(WIP_FIRST_READ_US);
	begin(TLSR8278_FLASH_CMD_READ_STATUS);
	t0 = sys_read32(STIMER);
	do {
		octet_out(0U);
		if ((sys_read8(MSPI_DATA) & FLASH_STATUS_WIP) == 0U) {
			ret = 0;
			break;
		}
	} while (sys_read32(STIMER) - t0 <= timeout_ms * 1000U * STIMER_PER_US);
	end();
	irq_unlock(key);
	return ret;
}
