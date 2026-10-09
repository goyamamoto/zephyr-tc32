/*
 * The vectors and the startup from reset to z_cstart.
 *
 * 0x000 header: the reset vector (a jump to the entry at 0x98), "KNLT" at
 *       0x08, the word 0x0088000d at 0x0c, the interrupt vector at 0x10,
 *       zeros at 0x14-0x1f.
 * 0x020 the loader: a count, the resets and clock enables written, analog
 *       0x7f read; when its bit 0 is 1, register 0x800608 is used for each
 *       16 bytes of the image from 0xd0 up to _tc32_boot_copy_size_align_256_
 *       (the RAM code, which runs from SRAM).
 * 0x098 the entry: with the early watchdog, the loader's count and its
 *       reset and clock write, then Timer2 as the watchdog, then the loader.
 * 0x0d0 the startup: bytes read at 0x14-0x17 tested and copied to 0x80063e,
 *       flash command 0xab, a fill of the start of .data, the IRQ and SVC
 *       modes with their stack pointers, .bss and the linker script's
 *       cache tag area zeroed, 0x80060c and 0x80060d set, .data copied,
 *       then z_cstart.
 *
 * Documented (DS-TLSR8278 v1.0.4): 0x800060-0x800065 (resets and clock
 * enables), 0x800066 (system clock select; its reset value 6 selects the
 * 24 MHz RC clock), Timer2 and its watchdog use (5.1.6). Not documented: the
 * header words at 0x08 and 0x0c and what the boot ROM does with the image
 * before 0x0 runs, 0x800608, 0x80060c and 0x80060d, 0x80063e and 0x80063f,
 * the analog port (0x8000b8-0x8000ba) and analog 0x7f, the MSPI registers
 * 0x80000c and 0x80000d, and what the loads of addresses 0x14-0x17 read here (the code
 * treats them as bytes of the image header; chip_init_tail writes the same
 * bytes at SRAM 0x840014-0x840017). The code writes to or tests these as the
 * comments say.
 *
 * The count loops (adds, cmp, ble: 3 instructions per count) take a fixed
 * number of instructions; their time in microseconds is not measured.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The standard-syntax twin of tlsr8278_startup.S: the same code and data in
 * unified Thumb syntax, the TC32-only instructions as .inst.n with the Thumb
 * value that stands for each (the Telink instruction in the comment). The
 * direct path (TC32_DIRECT) assembles this file with clang -mcpu=tc32; the
 * Thumb path assembles tlsr8278_startup.S through tc32asm2thumb.py. Every
 * build assembles both with the same flags and compares the objects
 * (asm_twin_check.py).
 */
	.syntax unified
	.thumb
#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

#ifndef CONFIG_XIP
#error "the startup copies .data only: the image must run in place (CONFIG_XIP)"
#endif

#if CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS > 0
/* The chip is reset when Timer2's tick bits 31:18 reach the 14-bit capture. */
#define TLSR8278_STARTUP_WD_RAW ((CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS * 48000) >> 18)
#if TLSR8278_STARTUP_WD_RAW < 1
#define TLSR8278_STARTUP_WD_CAPT 1
#elif TLSR8278_STARTUP_WD_RAW > 0x3fff
#define TLSR8278_STARTUP_WD_CAPT 0x3fff
#else
#define TLSR8278_STARTUP_WD_CAPT TLSR8278_STARTUP_WD_RAW
#endif
/*
 * 0x621 bits 7:1 are the capture's low bits (bit 0 is Timer2's mode, 0);
 * 0x622 bits 6:0 its high bits, bit 7 the enable.
 */
#define TLSR8278_STARTUP_WD_CAPT_LO ((TLSR8278_STARTUP_WD_CAPT << 1) & 0xff)
#define TLSR8278_STARTUP_WD_CAPT_HI_EN (((TLSR8278_STARTUP_WD_CAPT >> 7) & 0x7f) | 0x80)
#endif

GTEXT(__start)
GTEXT(__reset)
GTEXT(__irq)
GTEXT(z_cstart)
GDATA(z_interrupt_stacks)

SECTION_FUNC(vectors, __start)
	.org 0x0
	b __reset
	.short 0
	.word 0
	.org 0x8
	.word 0x544c4e4b	/* "KNLT" */
	.word 0x0088000d
	.org 0x10
	b __irq
	.short 0
	.word 0			/* 0x14-0x17: tested at 0x0d0 */
	.org 0x18
	.word 0
	.word 0

/*
 * 0x020 the loader: a count to 111 first.
 */
	.org 0x20
tlsr8278_startup:
	movs r2, #0
	movs r3, #110
1:
	adds r2, #1
	cmp r2, r3
	ble 1b
	ldr r0, .Lld_rst
	ldr r1, .Lld_rst_val
	str r1, [r0, #0]	/* 0x800060-62 (resets) = 0, 0x800063 = 0xff */
	lsrs r1, r1, #24
	strb r1, [r0, #4]	/* 0x800064 = 0xff (clock enables) */
	strb r1, [r0, #5]	/* 0x800065 = 0xff */
	/* Analog 0x7f: 0x7f to 0x8000b8, 0x40 to 0x8000ba, a wait until
	 * 0x8000ba bit 0 reads 0, the byte from 0x8000b9. */
	ldr r0, .Lld_a7f
	ldr r1, .Lld_ana
	adds r2, r0, #0
	strb r2, [r1, #0]
	movs r2, #64
	strb r2, [r1, #2]
1:
	ldrb r2, [r1, #2]
	lsls r2, r2, #31
	lsrs r2, r2, #31
	cmp r2, #1
	beq 1b	/* until 0x8000ba bit 0 reads 0 */
	ldrb r2, [r1, #1]
	movs r3, #1
	ands r3, r2
	cmp r3, #1
	bne .Lcstartup	/* analog 0x7f bit 0 = 0: no loads */
	/* For each 16 bytes from 0xd0 to _tc32_boot_copy_size_align_256_:
	 * 0x800608 = (1 << 24) + the offset, then a wait until 0x800608's top
	 * byte (asrs #24) reads 0. The RAM code is expected in SRAM after it. */
	ldr r0, .Lld_load
	ldr r1, .Lld_start
	ldr r2, .Lld_end
	movs r4, #1
	lsls r4, r4, #24
.Lld_next:
	cmp r1, r2
	bge .Lcstartup
	adds r5, r4, #0
	adds r5, r5, r1
	str r5, [r0, #0]	/* 0x800608 = (1 << 24) + r1 */
1:
	ldr r3, [r0, #0]
	asrs r3, r3, #24
	cmp r3, #0
	bne 1b	/* until the top byte reads 0 */
	adds r1, #16
	b .Lld_next
	.short 0x46c0			/* pad (never executed) */
.Lld_load:
	.word 0x00800608
.Lld_start:
	.word 0x000000d0		/* the first offset */
.Lld_end:
	.word _tc32_boot_copy_size_align_256_	/* the end of the RAM code */
.Lld_a7f:
	.word 0x0000007f		/* analog 0x7f */
.Lld_ana:
	.word 0x008000b8
.Lld_rst:
	.word 0x00800060		/* the resets and clock enables */
.Lld_rst_val:
	.word 0xff000000		/* 0x60-0x62 = 0, 0x63 = 0xff */
	.word 0x0080058a		/* not loaded */
	.org 0x98
/*
 * The reset vector's target. With the early watchdog: the loader's count
 * and its reset and clock write, then Timer2 as the watchdog (DS-TLSR8278
 * 5.1.6: step 1 the tick cleared, step 2 Timer2 on, step 3 the capture and
 * the enable), then the loader, which does the count and the write again.
 * Timer2 counts the system clock, and the capture is computed for 48 MHz.
 * Until clock_init selects 48 MHz the chip runs on the 24 MHz RC clock, so
 * over that time the watchdog period is twice
 * CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS.
 */
__reset:
#if CONFIG_SOC_TLSR8278_EARLY_WATCHDOG_MS > 0
	movs r2, #0
	movs r3, #110
1:
	adds r2, #1
	cmp r2, r3
	ble 1b
	ldr r0, .Lstub_rst
	movs r1, #0xff
	lsls r1, r1, #24
	str r1, [r0]
	lsrs r1, r1, #24
	strb r1, [r0, #4]
	strb r1, [r0, #5]
	ldr r1, .Lstub_tmr
	movs r0, #0
	str r0, [r1, #0x18]
	ldrb r0, [r1]
	movs r2, #0x40
	orrs r0, r2
	strb r0, [r1]
	movs r0, #TLSR8278_STARTUP_WD_CAPT_LO
	strb r0, [r1, #1]
	movs r0, #TLSR8278_STARTUP_WD_CAPT_HI_EN
	strb r0, [r1, #2]
#endif
	b tlsr8278_startup
	.balign 4
.Lstub_rst:
	.word 0x00800060
.Lstub_tmr:
	.word 0x00800620

/*
 * 0x0d0 the startup.
 */
	.org 0xd0
.Lcstartup:
	/* When the bytes read at 0x14 and 0x15 are 0x5a and 0xa5, the bytes
	 * at 0x16 and 0x17 go to 0x80063e and 0x80063f. */
	movs r2, #20
	ldrb r1, [r2, #0]
	movs r0, #90
	cmp r0, r1
	bne .Lflash_wake	/* 0x14 is not 0x5a */
	movs r2, #21
	ldrb r1, [r2, #0]
	movs r0, #165
	cmp r0, r1
	bne .Lflash_wake	/* 0x15 is not 0xa5 */
	movs r2, #22
	ldrb r1, [r2, #0]
	ldr r0, .Lcs_boot_sel
	strb r1, [r0, #0]
	movs r2, #23
	ldrb r1, [r2, #0]
	strb r1, [r0, #1]
.Lflash_wake:
	/* Flash command 0xab (the flash chip's release from deep power-down):
	 * 0x80000d = 0, 0xab to 0x80000c, a count to 7 (0x80000d bit 4 is not
	 * tested here), 0x80000d = 1. */
	ldr r0, .Lcs_mspi
	movs r1, #0
	strb r1, [r0, #1]
	movs r1, #171
	strb r1, [r0, #0]
	movs r2, #0
	movs r3, #6
1:
	adds r2, #1
	cmp r2, r3
	ble 1b
	movs r1, #1
	strb r1, [r0, #1]
	/* 0xffffffff into the first 32 bytes of .data (the .data copy below
	 * then writes them). */
	ldr r0, .Lcs_fill
	ldr r1, .Lcs_fill_start
	ldr r2, .Lcs_fill_end
1:
	cmp r1, r2
	bge 2f
	str r0, [r1, #0]
	adds r1, #4
	b 1b
2:
	/* CPSR = 0x12 (IRQ mode) and its sp, then CPSR = 0x13 (SVC mode) and
	 * its sp: both sp values are the same address, the top of
	 * z_interrupt_stacks. */
	ldr r0, .Lcs_irq_mode
	.inst.n 0xbbc0 @ tmcsr r0
	ldr r0, .Lcs_irq_sp
	mov sp, r0
	ldr r0, .Lcs_svc_mode
	.inst.n 0xbbc0 @ tmcsr r0
	ldr r0, .Lcs_svc_sp
	mov sp, r0
	/* .bss = 0. */
	movs r0, #0
	ldr r1, .Lcs_bss
	ldr r2, .Lcs_bss_end
1:
	cmp r1, r2
	bge 2f
	str r0, [r1, #0]
	adds r1, #4
	b 1b
2:
	/* _ictag_start_ to _ictag_end_ = 0 (the linker script's cache tag
	 * area). */
	ldr r1, .Lcs_tag
	ldr r2, .Lcs_tag_end
1:
	cmp r1, r2
	bge 2f
	str r0, [r1, #0]
	adds r1, #4
	b 1b
2:
	/* 0x80060c = _tc32_icache_lines_ (the RAM code's size in 256-byte
	 * units), 0x80060d = that + 1. */
	ldr r1, .Lcs_cache
	ldr r0, .Lcs_lines
	strb r0, [r1, #0]
	adds r0, #1
	strb r0, [r1, #1]
	/* .data from the flash. */
	ldr r1, .Lcs_data_lma
	ldr r2, .Lcs_data
	ldr r3, .Lcs_data_end
1:
	cmp r2, r3
	bge 2f
	ldr r0, [r1, #0]
	str r0, [r2, #0]
	adds r1, #4
	adds r2, #4
	b 1b
2:
	bl z_cstart
1:
	b 1b	/* z_cstart does not return */
.Lcs_irq_mode:
	.word 0x00000012		/* IRQ mode */
.Lcs_svc_mode:
	.word 0x00000013		/* SVC mode */
.Lcs_irq_sp:
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF	/* IRQ mode's sp */
.Lcs_svc_sp:
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF	/* SVC mode's sp (the same) */
.Lcs_bss:
	.word __bss_start
.Lcs_bss_end:
	.word __bss_end
.Lcs_cache:
	.word 0x0080060c
.Lcs_tag:
	.word _ictag_start_
.Lcs_tag_end:
	.word _ictag_end_
.Lcs_lines:
	.word _tc32_icache_lines_	/* the RAM code's size in 256-byte units */
.Lcs_data_lma:
	.word __data_region_load_start	/* .data in the flash */
.Lcs_data:
	.word __data_region_start
.Lcs_data_end:
	.word __data_region_end
.Lcs_fill:
	.word 0xffffffff
.Lcs_fill_start:
	.word __data_region_start
.Lcs_fill_end:
	.word __data_region_start + 0x20
	.word 0x00000000		/* not loaded */
	.word 0x00000000		/* not loaded */
	.word 0x00000000		/* not loaded */
	.word 0x00000000		/* not loaded */
.Lcs_boot_sel:
	.word 0x0080063e
.Lcs_mspi:
	.word 0x0080000c		/* (0x80000d at + 1) */
	.size tlsr8278_startup, . - tlsr8278_startup
