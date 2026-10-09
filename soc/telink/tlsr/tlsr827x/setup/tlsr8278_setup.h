/*
 * The SoC setup in tlsr8278_setup.S: the blob and each function's offset in it.
 * The C side calls a function as TLSR8278_SETUP_FN(type, offset); the offsets
 * are fixed by the blob's layout. Comments give each function's size and
 * whether soc_early_init_hook() reaches it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>

extern const uint8_t tlsr8278_setup_blob[2836];
#define TLSR8278_SETUP_OFF_UDIV 0x0u			/* 4 B, not reached */
#define TLSR8278_SETUP_OFF_DIV 0x4u				/* 64 B, not reached */
#define TLSR8278_SETUP_OFF_MUL_ADD 0x44u			/* 248 B, not reached */
#define TLSR8278_SETUP_OFF_ANALOG_READ 0x13cu		/* 64 B */
#define TLSR8278_SETUP_OFF_ANALOG_WRITE 0x17cu		/* 64 B */
#define TLSR8278_SETUP_OFF_CLOCK_INIT 0x1bcu		/* 40 B */
#define TLSR8278_SETUP_OFF_FLASH_SEND_ADDR 0x1e4u		/* 64 B */
#define TLSR8278_SETUP_OFF_FLASH_SEND_CMD 0x224u		/* 44 B */
#define TLSR8278_SETUP_OFF_FLASH_READ_CMD 0x250u		/* 176 B */
#define TLSR8278_SETUP_OFF_SLEEP_US 0x300u			/* 20 B */
#define TLSR8278_SETUP_OFF_SUSPEND_STALL 0x314u		/* 44 B, not reached */
#define TLSR8278_SETUP_OFF_ANALOG88_CHECK 0x340u		/* 172 B */
#define TLSR8278_SETUP_OFF_SUSPEND_ENTER 0x3ecu		/* 296 B, not reached */
#define TLSR8278_SETUP_OFF_READ_ANALOG40 0x514u		/* 76 B */
#define TLSR8278_SETUP_OFF_GET_32K_TICK 0x560u		/* 72 B */
#define TLSR8278_SETUP_OFF_SET_32K_TICK 0x5a8u		/* 92 B, not reached */
#define TLSR8278_SETUP_OFF_CHIP_INIT 0x604u			/* 524 B */
#define TLSR8278_SETUP_OFF_CALL_VIA_R3 0x810u		/* 4 B */
#define TLSR8278_SETUP_OFF_CALL_VIA_R3_B 0x814u		/* 4 B, not reached */
#define TLSR8278_SETUP_OFF_SCALE_COUNT 0x818u		/* 64 B, not reached */
#define TLSR8278_SETUP_OFF_SEQ_C7 0x858u			/* 112 B */
#define TLSR8278_SETUP_OFF_SEQ_C6 0x8c8u			/* 84 B */
#define TLSR8278_SETUP_OFF_FLASH_READ_JEDEC_ID 0x91cu	/* 36 B */
#define TLSR8278_SETUP_OFF_FLASH_IS_ZBIT 0x940u		/* 64 B */
#define TLSR8278_SETUP_OFF_PWM_PC3_10MS 0x980u		/* 96 B */
#define TLSR8278_SETUP_OFF_READ_WORD_4C 0x9e0u		/* 24 B */
#define TLSR8278_SETUP_OFF_PWM_PC3_UNTIL_MOVING 0x9f8u	/* 60 B */
#define TLSR8278_SETUP_OFF_CHIP_INIT_TAIL 0xa34u		/* 224 B */
#define TLSR8278_SETUP_FN(type, off) ((type)((uintptr_t)tlsr8278_setup_blob + (off) + 1u))
