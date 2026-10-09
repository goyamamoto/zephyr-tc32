/*
 * TLSR8278 radio: BLE 1M and the private 2M mode. DS-TLSR8278 does not
 * document the radio's registers (0x400-0x4ff, 0xc08-0xc0f, 0xf00-0xf2f,
 * 0x1220-0x1245); the comments give what the code writes, reads or waits for.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR_RADIO_H_
#define ZEPHYR_SOC_TELINK_TLSR_RADIO_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>

/* The system timer: 16 MHz, free running (DS-TLSR8278 5.3). */
#define TLSR_RADIO_TICKS_PER_US 16U

/* RF interrupt status bits (u16 at 0xf20; the code clears a bit by writing it as 1). */
#define TLSR_RF_IRQ_RX  0x0001U
#define TLSR_RF_IRQ_TX  0x0002U
#define TLSR_RF_IRQ_ALL 0xffffU
/* The bits the code takes as the end of a connection event, cleared together:
 * bits 2, 4, 5, 6 and 10.
 */
#define TLSR_RF_IRQ_EVENT_END 0x0474U

/* The advertising access address and CRC initial value (Core Vol 6 Part B 2.1.2). */
#define TLSR_BLE_ADV_ACCESS_ADDRESS 0x8e89bed6U
#define TLSR_BLE_ADV_CRC_INIT       0x555555U

/* The baseband interrupt, irq_zb_rt: bit 13 of the interrupt mask (0x640) and
 * source (0x648) words (DS-TLSR8278 6.2), from the radio's devicetree node.
 */
#define TLSR_RADIO_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(telink_b87_radio)
#define TLSR_RADIO_IRQ  DT_IRQN(TLSR_RADIO_NODE)

/* A TX buffer for the RF DMA: a u32 DMA length (the PDU length), then the PDU
 * (header, length, payload). An RX buffer entry has the same u32 and header,
 * then the payload; the code reads entry[len + 16] as its status byte, bit 0
 * set meaning a CRC error.
 */
#define TLSR_RADIO_DMA_HDR 4U

/* The code takes an RX entry as valid when its DMA length is the PDU length
 * + 13 and bit 0 of its status byte is clear.
 */
#define TLSR_RADIO_RX_EXTRA 13U
#define TLSR_RADIO_RX_STATUS(len) ((len) + 16U)

/* The code takes the u32 at 0x450 minus this as the anchor, the start of the
 * received packet.
 */
#define TLSR_RADIO_TIMESTAMP_OFFSET 0x500U

uint32_t tlsr_radio_now(void);
void tlsr_radio_init_ble_1m(void);
void tlsr_radio_set_power_level(uint8_t level);
/* After a chip suspend, which resets the radio's settings: set them up again, the TX power too. */
void tlsr_radio_resume(uint8_t level);
void tlsr_radio_set_ble_channel(uint8_t chn);
void tlsr_radio_off(void);
void tlsr_radio_set_access_address(uint32_t aa);
void tlsr_radio_set_crc_init(uint32_t crc_init);
void tlsr_radio_set_rx_buffer(void *buf, uint16_t size);
void tlsr_radio_start_tx_then_rx(const void *pkt, uint32_t tick);
/* After tlsr_radio_start_tx_then_rx(), while a packet arrives: when its RX
 * status rises (within window ticks of t0), a single TX of pkt starting ahead
 * ticks after the tick read with the status (in *tick); false if it did not
 * rise in the wait. From RAM; the caller has the chip's interrupts off.
 */
bool tlsr_radio_tx_on_rx(const void *pkt, uint32_t ahead, uint32_t t0, uint32_t window, uint32_t *tick);
/* The chip's interrupts off (0x643 = 0) around the radio's timing; returns the value before, for
 * tlsr_radio_irq_restore(). No kernel call may come in between.
 */
uint8_t tlsr_radio_irq_off(void);
void tlsr_radio_irq_restore(uint8_t irq_en);
void tlsr_radio_stop(void);
uint16_t tlsr_radio_irq_status(void);
/* A packet is being received (0x448 bit 5, not documented in DS-TLSR8278). */
bool tlsr_radio_receiving(void);
void tlsr_radio_clear_irq(uint16_t bits);
void tlsr_radio_adv_timing(void);

/* Connection events, called in this order: */
/* returns the TX FIFO read pointer after emptying the FIFO */
uint8_t tlsr_radio_conn_setup(void);
/* The TX FIFO emptied (0xc2a = 0x10): at a connection's start and end, so
 * that no packet of a connection is left in it while advertising. Returns
 * the read pointer as read after it: no document says the pointers go back
 * to 0.
 */
uint8_t tlsr_radio_tx_fifo_clear(void);
void tlsr_radio_conn_event_prepare(uint8_t chn, uint32_t aa, uint32_t crc_init);
void tlsr_radio_set_sn_nesn(uint8_t bits);
uint8_t tlsr_radio_tx_fifo_restore(uint8_t rptr);
void tlsr_radio_tx_fifo_push(const void *pkt);
void tlsr_radio_conn_timing(bool event_timeout, uint32_t event_timeout_us);
void tlsr_radio_start_conn_event(const void *pkt, uint32_t tick);
/* after an RX or at the end of an event */
uint8_t tlsr_radio_sn_nesn(void);
uint8_t tlsr_radio_tx_fifo_rptr(void);
uint32_t tlsr_radio_rx_timestamp(void);
void tlsr_radio_set_rx_dma(const void *buf);
void tlsr_radio_set_irq_mask(uint16_t mask);

/* The private 2M mode (the p24 functions), for a proprietary 2.4 GHz link of the application */
#define TLSR_RF_IRQ_RX_TIMEOUT 0x0400U
void tlsr_radio_init_p24(void *rx_buf, uint16_t rx_size);
/* first: 0x71 while searching and pairing, 0x77 once linked */
void tlsr_radio_p24_access(uint32_t code, uint8_t first);
void tlsr_radio_p24_settle(bool fast);
void tlsr_radio_p24_channel(uint16_t mhz);
/* 0x85 at tick; on its TX interrupt, tlsr_radio_p24_listen() opens the reply window */
void tlsr_radio_p24_send(const void *pkt, uint32_t tick);
void tlsr_radio_p24_listen(uint16_t window_us, uint32_t tick);
void tlsr_radio_p24_send_listen(const void *pkt, uint32_t tick, uint16_t window_us);
void tlsr_radio_p24_ack_irq(uint16_t bit);

#endif /* ZEPHYR_SOC_TELINK_TLSR_RADIO_H_ */
