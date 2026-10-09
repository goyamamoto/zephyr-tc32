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
 * The count loops (tadd, tcmp, tjle: 3 instructions per count) take a fixed
 * number of instructions; their time in microseconds is not measured.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
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
	tj __reset
	.short 0
	.word 0
	.org 0x8
	.word 0x544c4e4b	/* "KNLT" */
	.word 0x0088000d
	.org 0x10
	tj __irq
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
	tmov r2, #0
	tmov r3, #110
1:
	tadd r2, #1
	tcmp r2, r3
	tjle 1b
	tloadr r0, .Lld_rst
	tloadr r1, .Lld_rst_val
	tstorer r1, [r0, #0]		/* 0x800060-62 (resets) = 0, 0x800063 = 0xff */
	tshftr r1, r1, #24
	tstorerb r1, [r0, #4]		/* 0x800064 = 0xff (clock enables) */
	tstorerb r1, [r0, #5]		/* 0x800065 = 0xff */
	/* Analog 0x7f: 0x7f to 0x8000b8, 0x40 to 0x8000ba, a wait until
	 * 0x8000ba bit 0 reads 0, the byte from 0x8000b9. */
	tloadr r0, .Lld_a7f
	tloadr r1, .Lld_ana
	tmov r2, r0
	tstorerb r2, [r1, #0]
	tmov r2, #64
	tstorerb r2, [r1, #2]
1:
	tloadrb r2, [r1, #2]
	tshftl r2, r2, #31
	tshftr r2, r2, #31
	tcmp r2, #1
	tjeq 1b				/* until 0x8000ba bit 0 reads 0 */
	tloadrb r2, [r1, #1]
	tmov r3, #1
	tand r3, r2
	tcmp r3, #1
	tjne .Lcstartup			/* analog 0x7f bit 0 = 0: no loads */
	/* For each 16 bytes from 0xd0 to _tc32_boot_copy_size_align_256_:
	 * 0x800608 = (1 << 24) + the offset, then a wait until 0x800608's top
	 * byte (tasr #24) reads 0. The RAM code is expected in SRAM after it. */
	tloadr r0, .Lld_load
	tloadr r1, .Lld_start
	tloadr r2, .Lld_end
	tmov r4, #1
	tshftl r4, r4, #24
.Lld_next:
	tcmp r1, r2
	tjge .Lcstartup
	tmov r5, r4
	tadd r5, r5, r1
	tstorer r5, [r0, #0]		/* 0x800608 = (1 << 24) + r1 */
1:
	tloadr r3, [r0, #0]
	tasr r3, r3, #24
	tcmp r3, #0
	tjne 1b				/* until the top byte reads 0 */
	tadd r1, #16
	tj .Lld_next
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
	tmov r2, #0
	tmov r3, #110
1:
	tadd r2, #1
	tcmp r2, r3
	tjle 1b
	tloadr r0, .Lstub_rst
	tmov r1, #0xff
	tshftl r1, r1, #24
	tstorer r1, [r0]
	tshftr r1, r1, #24
	tstorerb r1, [r0, #4]
	tstorerb r1, [r0, #5]
	tloadr r1, .Lstub_tmr
	tmov r0, #0
	tstorer r0, [r1, #0x18]
	tloadrb r0, [r1]
	tmov r2, #0x40
	tor r0, r2
	tstorerb r0, [r1]
	tmov r0, #TLSR8278_STARTUP_WD_CAPT_LO
	tstorerb r0, [r1, #1]
	tmov r0, #TLSR8278_STARTUP_WD_CAPT_HI_EN
	tstorerb r0, [r1, #2]
#endif
	tj tlsr8278_startup
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
	tmov r2, #20
	tloadrb r1, [r2, #0]
	tmov r0, #90
	tcmp r0, r1
	tjne .Lflash_wake		/* 0x14 is not 0x5a */
	tmov r2, #21
	tloadrb r1, [r2, #0]
	tmov r0, #165
	tcmp r0, r1
	tjne .Lflash_wake		/* 0x15 is not 0xa5 */
	tmov r2, #22
	tloadrb r1, [r2, #0]
	tloadr r0, .Lcs_boot_sel
	tstorerb r1, [r0, #0]
	tmov r2, #23
	tloadrb r1, [r2, #0]
	tstorerb r1, [r0, #1]
.Lflash_wake:
	/* Flash command 0xab (the flash chip's release from deep power-down):
	 * 0x80000d = 0, 0xab to 0x80000c, a count to 7 (0x80000d bit 4 is not
	 * tested here), 0x80000d = 1. */
	tloadr r0, .Lcs_mspi
	tmov r1, #0
	tstorerb r1, [r0, #1]
	tmov r1, #171
	tstorerb r1, [r0, #0]
	tmov r2, #0
	tmov r3, #6
1:
	tadd r2, #1
	tcmp r2, r3
	tjle 1b
	tmov r1, #1
	tstorerb r1, [r0, #1]
	/* 0xffffffff into the first 32 bytes of .data (the .data copy below
	 * then writes them). */
	tloadr r0, .Lcs_fill
	tloadr r1, .Lcs_fill_start
	tloadr r2, .Lcs_fill_end
1:
	tcmp r1, r2
	tjge 2f
	tstorer r0, [r1, #0]
	tadd r1, #4
	tj 1b
2:
	/* CPSR = 0x12 (IRQ mode) and its sp, then CPSR = 0x13 (SVC mode) and
	 * its sp: both sp values are the same address, the top of
	 * z_interrupt_stacks. */
	tloadr r0, .Lcs_irq_mode
	tmcsr r0
	tloadr r0, .Lcs_irq_sp
	tmov sp, r0
	tloadr r0, .Lcs_svc_mode
	tmcsr r0
	tloadr r0, .Lcs_svc_sp
	tmov sp, r0
	/* .bss = 0. */
	tmov r0, #0
	tloadr r1, .Lcs_bss
	tloadr r2, .Lcs_bss_end
1:
	tcmp r1, r2
	tjge 2f
	tstorer r0, [r1, #0]
	tadd r1, #4
	tj 1b
2:
	/* _ictag_start_ to _ictag_end_ = 0 (the linker script's cache tag
	 * area). */
	tloadr r1, .Lcs_tag
	tloadr r2, .Lcs_tag_end
1:
	tcmp r1, r2
	tjge 2f
	tstorer r0, [r1, #0]
	tadd r1, #4
	tj 1b
2:
	/* 0x80060c = _tc32_icache_lines_ (the RAM code's size in 256-byte
	 * units), 0x80060d = that + 1. */
	tloadr r1, .Lcs_cache
	tloadr r0, .Lcs_lines
	tstorerb r0, [r1, #0]
	tadd r0, #1
	tstorerb r0, [r1, #1]
	/* .data from the flash. */
	tloadr r1, .Lcs_data_lma
	tloadr r2, .Lcs_data
	tloadr r3, .Lcs_data_end
1:
	tcmp r2, r3
	tjge 2f
	tloadr r0, [r1, #0]
	tstorer r0, [r2, #0]
	tadd r1, #4
	tadd r2, #4
	tj 1b
2:
	tjl z_cstart
1:
	tj 1b				/* z_cstart does not return */
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
