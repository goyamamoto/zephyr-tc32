/*
 * TLSR8278 radio: BLE 1M and the private 2M mode, through the register block
 * at 0x800000. DS-TLSR8278 does not document the radio's registers
 * (0x400-0x4ff, 0xc08-0xc2d, 0xf00-0xf2f, 0x1220-0x134f); the comments give
 * what the code writes, reads or waits for, and how it uses a value.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "tlsr_radio.h"

#define REG(off)     (0x00800000U + (off))
#define SYSTEM_TICK  0x0740U

/* 0xf00-0xf2f */
#define RADIO_CMD      0x0f00U /* 0x80 stops; 0x85, 0x87 and 0x82 start (see the callers) */
#define RADIO_0F01    0x0f01U /* 1 before every connection event */
#define RADIO_0F02   0x0f02U
#define RADIO_0F03   0x0f03U /* bit 0 set with a timeout in 0xf2c; bits 5:4 the saved SN bits */
#define RADIO_0F04   0x0f04U /* 16 bits; 0x51 in advertising, 0x52 in a connection (1M) */
#define RADIO_0F0A  0x0f0aU /* 16 bits; 0xfa in a connection */
#define RADIO_0F16   0x0f16U /* bit 2 set with the start tick in 0xf18 */
#define RADIO_START_TICK    0x0f18U
#define RADIO_IRQ_MASK    0x0f1cU
#define RADIO_IRQ_STATUS  0x0f20U
#define RADIO_0F22       0x0f22U /* bit 0 read as the last SN */
#define RADIO_0F23     0x0f23U /* bit 4 read as the NESN */
#define RADIO_0F28 0x0f28U /* 32 bits; 0x0fffffff before 0x82, window - 1 before 0x3f86 */
#define RADIO_0F2C 0x0f2cU /* 32 bits; the connection's timeout in us */
#define RADIO_0448  0x0448U /* bit 5 reads 1 while a packet is being received */
#define CMD_STOP    0x80U
#define CMD_CONN_EVENT     0x82U
#define CMD_TX          0x85U
#define CMD_TX_THEN_RX  0x87U
#define AT_START_TICK   BIT(2)
#define RST1       0x0061U /* RST1 bit 0, the baseband's reset (DS-TLSR8278 2.4) */

/* 0x400-0x4ff and 0x1220-0x1229 */
#define RADIO_0405     0x0405U /* bit 7 set after a connection's access address */
#define RADIO_ACCESS_ADDRESS 0x0408U /* the access address, most significant byte first */
#define RADIO_RX_TIME   0x0450U /* 32 bits, read as the last RX packet's time stamp */
#define RADIO_040D  0x040dU
#define RADIO_CRC_INIT    0x0424U
#define RADIO_0428   0x0428U
#define RADIO_1228    0x1228U /* bits 5:0: (MHz - 2400) / 2 - 1 */
#define RADIO_1229   0x1229U /* bits 5:2: band_bits(MHz) */
#define RADIO_1225     0x1225U /* bit 6 */
#define RADIO_1226 0x1226U /* bit 7 */
#define RADIO_1227 0x1227U /* bits 4:0 */

/* 0xc08-0xc2d: the code's RF DMA, channels 2 (RX) and 3 (TX) */
#define RADIO_DMA_RX_ADDR 0x0c08U
#define RADIO_DMA_RX_SIZE 0x0c0aU /* the RX buffer's size / 16 */
#define RADIO_DMA_0C0B 0x0c0bU /* 1 */
#define RADIO_DMA_TX_ADDR 0x0c0cU
#define DMA_ENABLE     0x0c20U
/* TX_FIFO_READ: bits 3:0 read as the TX FIFO read pointer; written: bit 4 empties, bit 6 sets */
#define TX_FIFO_READ    0x0c2aU
#define TX_FIFO_WRITE    0x0c2bU
#define TX_FIFO_PUSH    0x0c2cU /* 16 bits: a packet's address, appended to the TX FIFO */
#define TX_FIFO_CLEAR  BIT(4)
#define TX_FIFO_SET    BIT(6)
#define DMA_RADIO     (BIT(2) | BIT(3))

static inline void w8(uint32_t off, uint8_t v)
{
	sys_write8(v, REG(off));
}

static inline void w16(uint32_t off, uint16_t v)
{
	sys_write16(v, REG(off));
}

static inline void w32(uint32_t off, uint32_t v)
{
	sys_write32(v, REG(off));
}

static inline uint8_t r8(uint32_t off)
{
	return sys_read8(REG(off));
}

static inline uint16_t sram_addr(const void *p)
{
	return (uint16_t)(uintptr_t)p;
}

uint32_t tlsr_radio_now(void)
{
	return sys_read32(REG(SYSTEM_TICK));
}

/*
 * The registers set for BLE 1M, at init and again after every chip suspend:
 * in Suspend mode the RF transceiver is powered down (DS-TLSR8278 2.5.2).
 */
static void set_ble_regs(void)
{
	w16(0x12d2, 0x199b);
	w8(0x127b, 0x0e);
	w8(0x1279, 0x38);
	w16(0x124a, 0x090e);
	w32(0x1254, 0x080c090e);
	w8(0x1276, 0x50);
	w8(0x134e, 0x45);
	w8(0x134c, 0x02);
	w8(0x0401, 0x08);
	w8(0x0402, 0x46);
	w8(0x0430, 0x36);
	w32(0x0460, 0x5f4f4434);
	w16(0x0464, 0x766b);
	w8(0x0f06, 0x00);
	w8(0x0f0c, 0x50);
	w8(0x0f0e, 0x00);
	w8(0x0f10, 0x00);
}

void tlsr_radio_init_ble_1m(void)
{
	set_ble_regs();
	/* the RF RX and TX DMA channels: 0xc20 bits 2 and 3 */
	w8(DMA_ENABLE, r8(DMA_ENABLE) | DMA_RADIO);
}

void tlsr_radio_resume(uint8_t level)
{
	/* after a suspend: the BLE 1M registers, then the TX power */
	set_ble_regs();
	tlsr_radio_set_power_level(level);
}

void tlsr_radio_set_power_level(uint8_t level)
{
	/* level bit 7 to 0x1225 bit 6; level bits 5:0 to 0x1226 bit 7 (bit 0)
	 * and 0x1227 bits 4:0 (bits 5:1)
	 */
	uint8_t slices = level & 0x3fU;
	uint8_t v = r8(RADIO_1225);

	w8(RADIO_1225, (level & 0x80U) ? (v | BIT(6)) : (v & (uint8_t)~BIT(6)));
	w8(RADIO_1226, (r8(RADIO_1226) & 0x7fU) | (uint8_t)((slices & 1U) << 7));
	w8(RADIO_1227, (r8(RADIO_1227) & (uint8_t)~0x1fU) | (slices >> 1));
}

static uint16_t ble_channel_mhz(uint8_t chn)
{
	switch (chn) {
	case 37:
		return 2402;
	case 38:
		return 2426;
	case 39:
		return 2480;
	default:
		return chn <= 10 ? 2404 + 2 * chn : 2406 + 2 * chn;
	}
}

static uint8_t band_bits(uint16_t mhz)
{
	/* the value for 0x1229 bits 5:2, by frequency */
	if (mhz > 2519) {
		return 4;
	}
	if (mhz > 2494) {
		return 8;
	}
	if (mhz > 2464) {
		return 12;
	}
	if (mhz > 2434) {
		return 16;
	}
	if (mhz > 2404) {
		return 20;
	}
	return mhz > 2379 ? 24 : 28;
}

void tlsr_radio_set_ble_channel(uint8_t chn)
{
	uint16_t mhz = ble_channel_mhz(chn);

	w8(RADIO_040D, chn);
	w8(RADIO_1228, (r8(RADIO_1228) & (uint8_t)~0x3fU) | (uint8_t)((mhz - 2400) / 2 - 1));
	w8(RADIO_1229, (r8(RADIO_1229) & (uint8_t)~0x3cU) | band_bits(mhz));
}

void tlsr_radio_off(void)
{
	w8(RADIO_0F16, 0x29);
	w8(RADIO_0428, 0x80);
	w8(RADIO_0F02, 0x45);
}

void tlsr_radio_set_access_address(uint32_t aa)
{
	w32(RADIO_ACCESS_ADDRESS, __builtin_bswap32(aa));
}

void tlsr_radio_set_crc_init(uint32_t crc_init)
{
	w32(RADIO_CRC_INIT, crc_init & 0xffffffU);
}

void tlsr_radio_set_rx_buffer(void *buf, uint16_t size)
{
	/* the RX DMA address, the size / 16, and 1 in 0xc0b */
	w16(RADIO_DMA_RX_ADDR, sram_addr(buf));
	w8(RADIO_DMA_RX_SIZE, (uint8_t)(size >> 4));
	w8(RADIO_DMA_0C0B, 1);
}

void tlsr_radio_start_tx_then_rx(const void *pkt, uint32_t tick)
{
	/* the TX DMA address and the start tick, then 0x87: every operand of the
	 * transmission is written before the command, so that a flash cache miss
	 * between the two writes cannot leave the start with an earlier address
	 */
	w8(RADIO_CMD, CMD_STOP);
	w16(RADIO_DMA_TX_ADDR, sram_addr(pkt));
	w32(RADIO_START_TICK, tick);
	w8(RADIO_0F16, r8(RADIO_0F16) | AT_START_TICK);
	w8(RADIO_CMD, CMD_TX_THEN_RX);
}

/*
 * Waits until the RF status bit TLSR_RF_IRQ_RX reads 1 or the system tick is
 * more than window ticks past t0. When it rises in the wait: a single TX
 * (0x85) of pkt at the tick read in the same pass of the wait plus ahead, and
 * that start tick in *tick. A status that reads 1 already at the call starts
 * nothing, as the packet's end is not known then. The TX DMA address is
 * written before the wait. The function runs from .ram_code and calls
 * nothing, so that no flash cache miss comes between the status and the
 * command; the caller has the chip's interrupts off (tlsr_radio_irq_off()).
 */
__attribute__((noinline, section(".ram_code.tlsr_radio_tx_on_rx")))
bool tlsr_radio_tx_on_rx(const void *pkt, uint32_t ahead, uint32_t t0, uint32_t window, uint32_t *tick)
{
	uint32_t t;

	if ((sys_read16(REG(RADIO_IRQ_STATUS)) & TLSR_RF_IRQ_RX) != 0U) {
		return false;
	}
	sys_write16(sram_addr(pkt), REG(RADIO_DMA_TX_ADDR));
	do {
		t = sys_read32(REG(SYSTEM_TICK));
		if ((sys_read16(REG(RADIO_IRQ_STATUS)) & TLSR_RF_IRQ_RX) != 0U) {
			sys_write32(t + ahead, REG(RADIO_START_TICK));
			sys_write8(sys_read8(REG(RADIO_0F16)) | AT_START_TICK, REG(RADIO_0F16));
			sys_write8(CMD_TX, REG(RADIO_CMD));
			*tick = t + ahead;
			return true;
		}
	} while (t - t0 <= window);
	return false;
}

/*
 * 20 unused octets in tlsr_radio_tx_on_rx()'s section, so that the RAM code
 * grows by a whole number of 32-octet flash cache lines with the function (76
 * octets): what is linked after the RAM code keeps its offset in its cache
 * line, and its lines keep the slots they share.
 */
__asm__(".pushsection .ram_code.tlsr_radio_tx_on_rx,\"ax\",%progbits\n"
	".space 20\n"
	".popsection\n");

uint8_t tlsr_radio_irq_off(void)
{
	uint8_t en = *TLSR827X_REG_IRQ_EN;

	*TLSR827X_REG_IRQ_EN = 0;
	return en;
}

void tlsr_radio_irq_restore(uint8_t irq_en)
{
	*TLSR827X_REG_IRQ_EN = irq_en;
}

void tlsr_radio_stop(void)
{
	w8(RADIO_CMD, CMD_STOP);
}

uint16_t tlsr_radio_irq_status(void)
{
	return sys_read16(REG(RADIO_IRQ_STATUS));
}

bool tlsr_radio_receiving(void)
{
	return (r8(RADIO_0448) & BIT(5)) != 0U;
}

void tlsr_radio_clear_irq(uint16_t bits)
{
	w16(RADIO_IRQ_STATUS, bits);
}

void tlsr_radio_adv_timing(void)
{
	/* before each advertising event: 0xf04 = 0x51 and every RF interrupt
	 * masked (the advertising code polls the status)
	 */
	w16(RADIO_0F04, 0x51);
	w16(RADIO_IRQ_MASK, 0);
}

uint8_t tlsr_radio_tx_fifo_clear(void)
{
	w8(TX_FIFO_READ, TX_FIFO_CLEAR);
	return r8(TX_FIFO_READ) & 0x0fU;
}

uint8_t tlsr_radio_conn_setup(void)
{
	/* after a CONNECT_IND: the TX FIFO emptied, 0xf03 cleared, 0xf01 = 1 */
	uint8_t rptr = tlsr_radio_tx_fifo_clear();

	w8(RADIO_0F03, 0);
	w8(RADIO_0F01, 1);
	return rptr;
}

void tlsr_radio_conn_event_prepare(uint8_t chn, uint32_t aa, uint32_t crc_init)
{
	/* before each connection event, up to the TX FIFO */
	w16(RADIO_IRQ_STATUS, TLSR_RF_IRQ_ALL);
	tlsr_radio_off();
	w8(RADIO_CMD, CMD_STOP);
	w8(RADIO_0F01, 1);
	w16(RADIO_IRQ_MASK, 0x0477);
	w16(RADIO_IRQ_STATUS, TLSR_RF_IRQ_ALL);
	w8(RST1, 1);
	w8(RST1, 0);
	tlsr_radio_set_ble_channel(chn);
	tlsr_radio_set_access_address(aa);
	w8(RADIO_0405, r8(RADIO_0405) | BIT(7));
	tlsr_radio_set_crc_init(crc_init);
}

void tlsr_radio_set_sn_nesn(uint8_t bits)
{
	/* the saved NESN and last SN into 0xf03 bits 5:4 */
	w8(RADIO_0F03, r8(RADIO_0F03) & 0xcfU);
	w8(RADIO_0F03, r8(RADIO_0F03) | (bits & 0x30U));
}

uint8_t tlsr_radio_tx_fifo_restore(uint8_t rptr)
{
	/* The read pointer as saved after the last RX; returns the number of
	 * packets still in the FIFO.
	 */
	w8(TX_FIFO_READ, (rptr & 0x0fU) | TX_FIFO_SET);
	uint8_t wptr = r8(TX_FIFO_WRITE);

	return (uint8_t)((wptr - r8(TX_FIFO_READ)) & 0x0fU);
}

void tlsr_radio_tx_fifo_push(const void *pkt)
{
	w16(TX_FIFO_PUSH, sram_addr(pkt));
}

void tlsr_radio_conn_timing(bool event_timeout, uint32_t event_timeout_us)
{
	/* after the TX FIFO: 0xf04 = 0x52, 0xf0a = 0xfa, and, once the
	 * connection is established, 0xf03 bit 0 with the timeout in 0xf2c
	 */
	w16(RADIO_0F04, 0x52);
	w16(RADIO_0F0A, 0xfa);
	if (event_timeout) {
		w8(RADIO_0F03, r8(RADIO_0F03) | BIT(0));
		w32(RADIO_0F2C, event_timeout_us);
	} else {
		w8(RADIO_0F03, r8(RADIO_0F03) & (uint8_t)~BIT(0));
	}
}

void tlsr_radio_start_conn_event(const void *pkt, uint32_t tick)
{
	/* pkt to the TX DMA address (the caller gives the packet for an empty TX
	 * FIFO), the start tick, then 0x82: every operand before the command
	 */
	w32(RADIO_0F28, 0x0fffffffU);
	w16(RADIO_DMA_TX_ADDR, sram_addr(pkt));
	w32(RADIO_START_TICK, tick);
	w8(RADIO_0F16, r8(RADIO_0F16) | AT_START_TICK);
	w8(RADIO_CMD, CMD_CONN_EVENT);
}

uint8_t tlsr_radio_sn_nesn(void)
{
	/* 0xf22 bit 0 and 0xf23 bit 4, as tlsr_radio_set_sn_nesn() takes them */
	return (uint8_t)(((r8(RADIO_0F22) & 1U) << 4) | ((r8(RADIO_0F23) & 0x10U) << 1));
}

uint8_t tlsr_radio_tx_fifo_rptr(void)
{
	return r8(TX_FIFO_READ) & 0x0fU;
}

uint32_t tlsr_radio_rx_timestamp(void)
{
	return sys_read32(REG(RADIO_RX_TIME));
}

void tlsr_radio_set_rx_dma(const void *buf)
{
	w16(RADIO_DMA_RX_ADDR, sram_addr(buf));
}

void tlsr_radio_set_irq_mask(uint16_t mask)
{
	w16(RADIO_IRQ_MASK, mask);
}

/*
 * The private 2M mode, for a proprietary 2.4 GHz link of the application.
 */

/* 0x1284 bits 6:3 and 0x12b0-0x12bb */
static void p24_rx_table_a(void)
{
	w8(0x1284, (r8(0x1284) & (uint8_t)~0x78U) | 0x30U);
	w8(0x12b0, 0x00);
	w8(0x12b1, r8(0x12b1) & (uint8_t)~1U);
	w8(0x12b2, 0x10);
	w8(0x12b3, r8(0x12b3) & (uint8_t)~1U);
	w8(0x12b4, 0x10);
	w8(0x12b5, r8(0x12b5) & (uint8_t)~1U);
	w8(0x12b6, 0x32);
	w8(0x12b7, r8(0x12b7) & (uint8_t)~1U);
	w8(0x12b8, 0x58);
	w8(0x12b9, r8(0x12b9) & (uint8_t)~1U);
	w8(0x12ba, 0x58);
	w8(0x12bb, r8(0x12bb) & (uint8_t)~1U);
}

/* 0x1284 bits 2:0, 0x12a4-0x12af and 0x12bc-0x12bd */
static void p24_rx_table_b(void)
{
	w8(0x1284, (r8(0x1284) & (uint8_t)~7U) | 2U);
	w8(0x12a4, 0x00);
	w8(0x12a5, r8(0x12a5) & (uint8_t)~1U);
	w8(0x12a6, 0x10);
	w8(0x12a7, r8(0x12a7) & (uint8_t)~1U);
	w8(0x12a8, 0x62);
	w8(0x12a9, r8(0x12a9) & (uint8_t)~1U);
	w8(0x12aa, 0x64);
	w8(0x12ab, r8(0x12ab) & (uint8_t)~1U);
	w8(0x12ac, 0x6a);
	w8(0x12ad, r8(0x12ad) & (uint8_t)~1U);
	w8(0x12ae, 0x6a);
	w8(0x12af, r8(0x12af) & (uint8_t)~1U);
	w8(0x12bc, 0x62);
	w8(0x12bd, r8(0x12bd) & (uint8_t)~1U);
}

void tlsr_radio_init_p24(void *rx_buf, uint16_t rx_size)
{
	w8(0x12d2, 0x9b);
	w8(0x12d3, 0x19);
	w8(0x127b, 0x0e);
	w8(0x1279, 0x38);
	w8(0x124a, 0x0e);
	w8(0x124b, 0x09);
	w8(0x124e, 0x09);
	w8(0x124f, 0x0f);
	w8(0x1254, 0x0e);
	w8(0x1255, 0x09);
	w8(0x1256, 0x0c);
	w8(0x1257, 0x08);
	w8(0x1258, 0x09);
	w8(0x1259, 0x0f);
	w8(0x1276, 0x50);
	w8(0x1277, 0x73);
	w8(0x134e, 0x45);
	w8(0x134c, 0x02);
	w8(0x1220, 0x04);
	w8(0x1221, 0x2a);
	w8(0x1222, 0x43);
	w8(0x1223, 0x06);
	w8(0x1254, 0x0e);
	w8(0x1255, 0x09);
	w8(0x1256, 0x0c);
	w8(0x1257, 0x08);
	w8(0x1258, 0x09);
	w8(0x1259, 0x0f);
	w8(0x0400, 0x1f);
	w8(0x0402, 0x42);
	w8(0x0404, 0xea);
	w8(0x0421, 0xa1);
	w8(0x0430, 0x3c);
	w8(0x0460, 0x36);
	w8(0x0461, 0x46);
	w8(0x0462, 0x51);
	w8(0x0463, 0x61);
	w8(0x0464, 0x6d);
	w8(0x0465, 0x78);
	w8(0x122a, 0x90);
	w8(0x122c, 0x38);
	w8(0x123d, 0x00);
	w8(0x0420, 0x1e);
	w8(0x0405, 0x04);
	w8(0x0422, 0x04);
	w8(0x0408, 0xc9);
	w8(0x0409, 0x8a);
	w8(0x040a, 0x11);
	w8(0x040b, 0xf8);
	w8(0x0407, 0x01);
	w8(0x0403, 0x44);
	w16(0x0f06, 0x0000);
	w8(0x0f0c, 0x54);
	w16(0x0f0e, 0x0000);
	w8(RADIO_0F04, 0x70);
	w8(0x0f10, 0x00);
	p24_rx_table_a();
	w8(RADIO_1229, r8(RADIO_1229) & (uint8_t)~1U);
	p24_rx_table_b();
	w8(RADIO_1229, r8(RADIO_1229) & (uint8_t)~2U);
	w8(DMA_ENABLE, r8(DMA_ENABLE) | DMA_RADIO);
	tlsr_radio_set_rx_buffer(rx_buf, rx_size);
	w8(RADIO_0405, (r8(RADIO_0405) & (uint8_t)~7U) | 5U);
	w8(0x0420, 0x23);
	tlsr_radio_off();
	w8(0x0407, (r8(0x0407) & (uint8_t)~0x3fU) | 1U);
	w8(0x0f15, r8(0x0f15) & (uint8_t)~7U);
	/* The RF status cleared and its mask set; the caller enables the
	 * interrupt line (0x640 bit 13, TLSR_RADIO_IRQ) with irq_enable().
	 */
	w16(RADIO_IRQ_STATUS, 0xffff);
	w16(RADIO_IRQ_MASK, sys_read16(REG(RADIO_IRQ_MASK)) & 0xe000U);
	w16(RADIO_IRQ_MASK, sys_read16(REG(RADIO_IRQ_MASK)) | 7U);
}

void tlsr_radio_p24_access(uint32_t code, uint8_t first)
{
	/* 0x408: first, then the code's bits 31:8; 0x40c: its bits 7:0 */
	w32(RADIO_ACCESS_ADDRESS, (code & 0xffffff00U) | first);
	w8(0x040c, (uint8_t)code);
}

void tlsr_radio_p24_settle(bool fast)
{
	/* the caller picks fast or normal before each packet */
	if (fast) {
		w8(RADIO_0F04, 0x41);
		w8(RADIO_1229, r8(RADIO_1229) | 2U);
		w8(0x0f0c, 0x30);
		w8(RADIO_1229, r8(RADIO_1229) | 1U);
	} else {
		w8(RADIO_0F04, 0x70);
		w8(RADIO_1229, r8(RADIO_1229) & (uint8_t)~2U);
		w8(0x0f0c, 0x54);
		w8(RADIO_1229, r8(RADIO_1229) & (uint8_t)~1U);
	}
}

void tlsr_radio_p24_channel(uint16_t mhz)
{
	/* 0x1244-0x1245: mhz << 2 | 1 and mhz >> 6; 0x1229 bits 5:2: band_bits(mhz) */
	w8(0x1244, (uint8_t)((mhz << 2) | 1U));
	w8(0x1245, (r8(0x1245) & (uint8_t)~0x3fU) | (uint8_t)((mhz >> 6) & 0x3fU));
	w8(RADIO_1229, (r8(RADIO_1229) & (uint8_t)~0x3cU) | band_bits(mhz));
}

void tlsr_radio_p24_send(const void *pkt, uint32_t tick)
{
	/* the TX DMA address, the start tick, 0xf16 bit 2, 0x85, then every RF
	 * status bit cleared: every operand before the command
	 */
	w16(RADIO_DMA_TX_ADDR, sram_addr(pkt));
	w32(RADIO_START_TICK, tick);
	w8(RADIO_0F16, r8(RADIO_0F16) | AT_START_TICK);
	w8(RADIO_CMD, CMD_TX);
	w16(RADIO_IRQ_STATUS, 0xffff);
}

void tlsr_radio_p24_send_listen(const void *pkt, uint32_t tick, uint16_t window_us)
{
	/* 0xf03 bit 2, the window - 1 into 0xf0a, the TX DMA address, the start
	 * tick, 0xf16 bit 2, 0x87, then every RF status bit cleared: every
	 * operand before the command; the radio sends, then listens by itself
	 * until a frame or the window's end
	 */
	w8(RADIO_0F03, r8(RADIO_0F03) | BIT(2));
	w16(RADIO_0F0A, (uint16_t)(window_us - 1U));
	w16(RADIO_DMA_TX_ADDR, sram_addr(pkt));
	w32(RADIO_START_TICK, tick);
	w8(RADIO_0F16, r8(RADIO_0F16) | AT_START_TICK);
	w8(RADIO_CMD, CMD_TX_THEN_RX);
	w16(RADIO_IRQ_STATUS, 0xffff);
}

void tlsr_radio_p24_listen(uint16_t window_us, uint32_t tick)
{
	/* on the TX interrupt: 0x80, the window - 1 into 0xf28, the tick,
	 * 0xf16 bit 2, then 0x3f86 into 0xf00-0xf01
	 */
	w8(RADIO_CMD, CMD_STOP);
	w32(RADIO_0F28, (uint32_t)window_us - 1U);
	w32(RADIO_START_TICK, tick);
	w8(RADIO_0F16, r8(RADIO_0F16) | AT_START_TICK);
	w16(RADIO_CMD, 0x3f86);
}

void tlsr_radio_p24_ack_irq(uint16_t bit)
{
	/* the status read back with the bit added, written as 16 bits */
	w16(RADIO_IRQ_STATUS, sys_read16(REG(RADIO_IRQ_STATUS)) | bit);
}
