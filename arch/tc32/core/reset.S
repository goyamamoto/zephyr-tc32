/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

/*
 * CPSR values used to step between the banked stacks with interrupts masked,
 * and the geometry of the frame z_tc32_irq_entry pushes: r0-r7, r8-r12, the
 * saved status register and the return address.
 */
#define TC32_MODE_IRQ          0x92
#define TC32_MODE_SVC          0x93
#define TC32_MODE_SVC_IRQ_ON   0x13
#define TC32_IRQ_FRAME_WORDS   15
#define TC32_IRQ_FRAME_SIZE    (TC32_IRQ_FRAME_WORDS * 4)

GTEXT(__start)
GTEXT(__reset)
GTEXT(__irq)
GTEXT(z_tc32_irq_entry)
GTEXT(z_prep_c)
GTEXT(z_cstart)
GTEXT(tc32_flash_read)
GTEXT(z_tc32_handle_irqs)
GTEXT(z_get_next_switch_handle)
GTEXT(z_tc32_switch)
GDATA(z_interrupt_stacks)
GDATA(z_tc32_switch_from)

.macro TC32_ANALOG_WRITE addr, value
	tmov r0, #\addr
	tstorerb r0, [r1]
	nop
	nop
	tmov r0, #\value
	tstorerb r0, [r1, #1]
	nop
	nop
	tmov r0, #0x60
	tstorerb r0, [r1, #2]
	nop
	nop
1:
	tloadrb r0, [r1, #2]
	nop
	nop
	tmov r2, #1
	tand r0, r2
	tcmp r0, #0
	nop
	nop
	tjne 1b
	tmov r0, #0
	tstorerb r0, [r1, #2]
	nop
	nop
.endm

.macro TC32_COPY_FROM_SRAM_MIRROR
1:
	tcmp r2, #4
	nop
	nop
	tjlo 2f
	tloadr r3, [r0]
	nop
	nop
	tstorer r3, [r1]
	tmov r3, #4
	tsub r2, r2, r3
	tadd r0, r0, r3
	tadd r1, r1, r3
	tj 1b
2:
	tcmp r2, #0
	nop
	nop
	tjeq 3f
	tloadrb r3, [r0]
	nop
	nop
	tstorerb r3, [r1]
	tmov r3, #1
	tsub r2, r2, r3
	tadd r0, r0, r3
	tadd r1, r1, r3
	tj 2b
3:
.endm

SECTION_FUNC(vectors, __start)
	.org 0x0
	tj __reset
	.word 0

	.org 0x8
	.word 0x544c4e4b
	.word 0x00880000 + _tc32_boot_copy_size_div_16_align_256_

	.org 0x10
	tj __irq

	.org 0x18
	/*
	 * Reserved boot-header slots (vendor convention places bin_size and
	 * metadata here). z_tc32_irq_entry now jumps to z_tc32_handle_irqs
	 * directly via `tjl` and no longer reads handler/return addresses
	 * from SRAM 0x840018+, so these words can stay padded.
	 */
	.word 0
	.word 0

	.org 0x20
__reset:
	tloadr r0, .Lirq_mode
	tmcsr r0
	tloadr r0, .Lirq_stack_end
	tmov sp, r0

	tloadr r0, .Lsvc_mode
	tmcsr r0
	tloadr r0, .Lstack_end
	tmov sp, r0

	tloadr r0, .Lirq_en
	tmov r1, #0
	tstorerb r1, [r0]
	/*
	 * reg_irq_mask is 24 bits and reg_irq_en is the byte above it, so this
	 * word store spans both. It is safe only because it stores a constant
	 * zero, which is what the byte store above already put in the gate;
	 * anything that reads the mask back and writes it again has to use the
	 * byte accessors in <tlsr825x/irq.h> instead.
	 */
	tloadr r0, .Lirq_mask
	tstorer r1, [r0]
	tjl z_prep_c
.Lhang:
	tj .Lhang

	.align 2
.Lirq_mode:
	.word 0x12
.Lsvc_mode:
	.word 0x13
.Lstack_end:
	/*
	 * Reuse the interrupt stack as the early SVC boot stack until
	 * arch_switch_to_main_thread() installs the main thread stack. Large
	 * TLSR8258 builds can leave only a few hundred bytes between _end and
	 * _stack_end_, which is not enough for C startup.
	 */
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF
.Lirq_stack_end:
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF
.Lirq_en:
	.word 0x00800643
.Lirq_mask:
	.word 0x00800640

	.align 2
z_prep_c:
	/*
	 * Keep early startup in the low forward-branch area. TC32 free-run
	 * debugging has shown unstable behavior around early backward tjl/call
	 * returns before C runtime setup.
	 *
	 * Match the direct register part of vendor cpu_wakeup_init(): release
	 * peripheral resets and enable clock gates before PRE_KERNEL devices.
	 */
	tloadr r0, .Lictag_start
	tloadr r1, .Lictag_end
	nop
	nop
	tmov r2, #0
.Lictag_clear:
	tmov r3, r1
	tsub r3, r3, r0
	tcmp r3, #0
	nop
	nop
	tjeq .Lictag_done
	tstorer r2, [r0]
	tmov r3, #4
	tadd r0, r0, r3
	tj .Lictag_clear
.Lictag_done:
	tloadr r1, .Licache_ctrl
	tloadr r0, .Licache_lines
	nop
	nop
	tstorerb r0, [r1]
	tmov r2, #1
	tadd r0, r0, r2
	tstorerb r0, [r1, #1]

	tloadr r1, .Lrst_clk
	tloadr r0, .Lrst_clk_0
	nop
	nop
	tstorer r0, [r1]
	tloadr r0, .Lrst_clk_1
	nop
	nop
	tstorer r0, [r1, #4]
	tloadr r1, .Lflash_ctrl
	tmov r0, #0
	tstorerb r0, [r1, #1]
	tmov r0, #0xab
	tstorerb r0, [r1]
	tmov r3, #7
.Lflash_wakeup_delay:
	tcmp r3, #0
	nop
	nop
	tjeq .Lflash_wakeup_done
	tmov r2, #1
	tsub r3, r3, r2
	tj .Lflash_wakeup_delay
.Lflash_wakeup_done:
	tmov r0, #1
	tstorerb r0, [r1, #1]

	/*
	 * NB: the vendor cpu_wakeup_init() analog/DCDC/PLL writes (ana
	 * 0x82/0x34/0x0b/0x8c/0x02) are deliberately NOT issued here. Performing
	 * them before the .data copy perturbs the DCDC/clock state, so the
	 * flash-LMA reads in the copy loop land in a marginal timing window and
	 * silently corrupt initialised data (observed on TLSR8258: non-
	 * deterministic .data corruption of BDB/ZDO callback pointers -> random
	 * join/interview failure). The vendor cstartup_8258.S copies .data at the
	 * stable boot clock first and raises the clock only later in main(); we
	 * match that by issuing these writes in z_prep_c_flash_continue AFTER the
	 * .data/.rodata/.ramfunc copies.
	 */

	/*
	 * Match soc_prep_hook(): clear watchdog enable in reg_tmr_ctrl
	 * (0x800620 bit 23) before BSS/data setup.
	 */
	tmov r0, #1
	tshftl r0, r0, #23
	tmov r1, #1
	tshftl r1, r1, #23
	tmov r2, #6
	tshftl r2, r2, #8
	tadd r1, r1, r2
	tmov r2, #32
	tadd r1, r1, r2
	tloadr r2, [r1]
	nop
	nop
	tbclr r2, r0
	tstorer r2, [r1]

	/*
	 * The ROM entered us from the boot SRAM mirror. Switch to the canonical
	 * flash/XIP mapping before copying init sections from their LMAs.
	 */
	tloadr r0, .Lflash_prep_continue
	nop
	nop
	tjex r0

	.align 2
.Lflash_prep_continue:
	.word z_prep_c_flash_continue
.Lrst_clk:
	.word 0x00800060
.Lrst_clk_0:
	.word 0xff000000
.Lrst_clk_1:
	.word 0x0006ffff
.Lictag_start:
	.word _ictag_start_
.Lictag_end:
	.word _ictag_end_
.Licache_lines:
	.word _tc32_icache_lines_
.Licache_ctrl:
	.word 0x0080060c
.Lflash_ctrl:
	.word 0x0080000c

SECTION_FUNC(TEXT, z_prep_c_flash_continue)
	tloadr r0, .Lbss_start
	tloadr r2, .Lbss_end
	nop
	nop
	tsub r2, r2, r0
	tmov r1, #0
	/*
	 * .bss is word aligned and tens of kilobytes long, so clear it a word
	 * at a time and leave the byte loop for a trailing partial word.
	 */
.Lbss_word_loop:
	tcmp r2, #4
	nop
	nop
	tjlo .Lbss_byte_loop
	tstorer r1, [r0]
	tmov r3, #4
	tadd r0, r0, r3
	tsub r2, r2, r3
	tj .Lbss_word_loop
.Lbss_byte_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Lbss_done
	tstorerb r1, [r0]
	tmov r3, #1
	tadd r0, r0, r3
	tsub r2, r2, r3
	tj .Lbss_byte_loop
.Lbss_done:
#ifndef CONFIG_XIP
	tloadr r0, .Lrodata_lma_start
	tloadr r1, .Lrodata_start
	tloadr r2, .Lrodata_end
	nop
	nop
	tsub r2, r2, r1
	TC32_COPY_FROM_SRAM_MIRROR
#endif

	tloadr r0, .Ldata_lma_start
	tloadr r1, .Ldata_start
	tloadr r2, .Ldata_end
	nop
	nop
	tsub r2, r2, r1
	TC32_COPY_FROM_SRAM_MIRROR

	/*
	 * Copy the .ramfunc section from its flash LMA to its RAM VMA.
	 * The asm z_prep_c above replaces the C arch_data_copy(), which is
	 * where Zephyr's generic XIP layer (arch/common/xip.c) normally
	 * issues this copy via arch_early_memcpy.  Without this block, any
	 * function marked __ramfunc would execute from uninitialised RAM
	 * and the CPU would fault on first cross-region call.
	 */
	tloadr r0, .Lramfunc_load_start
	tloadr r1, .Lramfunc_region_start
	tloadr r2, .Lramfunc_end
	nop
	nop
	tsub r2, r2, r1
	TC32_COPY_FROM_SRAM_MIRROR

	/*
	 * .data/.rodata/.ramfunc are now copied at the stable boot clock. Apply
	 * the vendor cpu_wakeup_init() analog/DCDC/PLL setup here (moved from
	 * z_prep_c) so the flash-LMA copy reads above never run in a marginal
	 * timing window -- see the note in z_prep_c.
	 */
	tloadr r1, .Lanalog_base
	TC32_ANALOG_WRITE 0x82, 0x64
	TC32_ANALOG_WRITE 0x34, 0x80
	TC32_ANALOG_WRITE 0x0b, 0x38
	TC32_ANALOG_WRITE 0x8c, 0x02
	TC32_ANALOG_WRITE 0x02, 0xa2

	tjl z_cstart

.Lprep_hang:
	tj .Lprep_hang

	.align 2
.Lbss_start:
	.word __bss_start
.Lbss_end:
	.word __bss_end
.Lrodata_lma_start:
	.word __rodata_region_load_start
.Lrodata_start:
	.word __rodata_region_start
.Lrodata_end:
	.word __rodata_region_end
.Ldata_lma_start:
	.word __data_region_load_start
.Ldata_start:
	.word __data_region_start
.Ldata_end:
	.word __data_region_end
.Lramfunc_load_start:
	.word __ramfunc_load_start
.Lramfunc_region_start:
	.word __ramfunc_region_start
.Lramfunc_end:
	.word __ramfunc_end
.Lanalog_base:
	.word 0x008000b8

SECTION_FUNC(boot_ram_mirror, __irq)
z_tc32_irq_entry:
	tpush {r14}
	tpush {r0-r7}
	tmrss r0

	tmov r1, r8
	tmov r2, r9
	tmov r3, r10
	tmov r4, r11
	tmov r5, r12
	tpush {r0-r5}

	/*
	 * Branch directly into z_tc32_handle_irqs. The prior absolute-address
	 * dispatch via SRAM 0x840018 / 0x840020 assumed the Telink boot ROM
	 * mirrored flash[0..0x1d00] into SRAM at boot; in practice the mirror
	 * was empty on this chip and an IRQ-time fetch from 0x840018 returned
	 * 0 -> tjex 0 -> faulted back into z_tc32_irq_entry as an IRQ storm.
	 * `tjl` sets lr to the next instruction (.Lirq_restore) automatically,
	 * matching what the absolute-address path used to load explicitly.
	 */
	tjl z_tc32_handle_irqs

#ifdef CONFIG_MULTITHREADING
	/*
	 * r0 is the thread to run, or NULL to resume the interrupted one.
	 * Every low register is dead here: the interrupted values are in the
	 * frame below and the C dispatcher has clobbered the rest.
	 */
	tcmp r0, #0
	nop
	nop
	tjeq .Lno_irq_reschedule

	/*
	 * The frame sits on the banked IRQ stack, which the next interrupt
	 * reuses, so it has to move onto the outgoing thread's own stack
	 * before anyone else runs. Reaching that stack means borrowing SVC
	 * mode for the two instructions it takes to read and lower its sp.
	 */
	tmov r4, r13

	tmov r5, #TC32_MODE_SVC
	tmcsr r5
	tmov r5, r13
	tmov r6, #TC32_IRQ_FRAME_SIZE
	tsub r5, r5, r6
	tmov r13, r5
	tmov r6, #TC32_MODE_IRQ
	tmcsr r6

	tmov r3, r5
	tmov r6, #TC32_IRQ_FRAME_WORDS
.Lirq_frame_out:
	tloadr r7, [r4]
	tstorer r7, [r5]
	tmov r2, #4
	tadd r4, r4, r2
	tadd r5, r5, r2
	tmov r2, #1
	tsub r6, r6, r2
	tcmp r6, #0
	nop
	nop
	tjne .Lirq_frame_out

	tmov r4, r13
	tmov r5, #TC32_IRQ_FRAME_SIZE
	tadd r4, r4, r5
	tmov r13, r4

	/* Park the outgoing thread on .Lirq_unwind with the relocated frame. */
	tloadr r1, .Lswitch_from
	nop
	nop
	tloadr r1, [r1]
	tstorer r3, [r1, #_thread_offset_to_sp]
	tloadr r2, .Lirq_unwind_ptr
	nop
	nop
	tstorer r2, [r1, #_thread_offset_to_lr]

	/*
	 * Hand over the way every other switch in this port does: with
	 * reg_irq_en shut, so whatever the incoming thread resumes into
	 * reopens the gate itself (arch_irq_unlock() in do_swap(),
	 * z_tc32_thread_start(), or .Lirq_unwind below). Shutting it here also
	 * covers the register load, which runs with the I bit already dropped
	 * because a thread must not be entered with it set.
	 */
	tloadr r1, .Lresched_irq_en
	nop
	nop
	tmov r2, #0
	tstorerb r2, [r1]
	nop
	nop

	/* Become the incoming thread; its stack is an SVC-mode stack. */
	tmov r1, #TC32_MODE_SVC_IRQ_ON
	tmcsr r1

	tloadr r2, [r0, #_thread_offset_to_sp]
	nop
	nop
	tmov r13, r2
	tloadr r2, [r0, #_thread_offset_to_lr]
	nop
	nop
	tmov r14, r2
	tloadr r4, [r0, #_thread_offset_to_r4]
	tloadr r5, [r0, #_thread_offset_to_r5]
	tloadr r6, [r0, #_thread_offset_to_r6]
	tloadr r7, [r0, #_thread_offset_to_r7]
	tloadr r2, [r0, #_thread_offset_to_r8]
	nop
	nop
	tmov r8, r2
	tloadr r2, [r0, #_thread_offset_to_r9]
	nop
	nop
	tmov r9, r2
	tloadr r2, [r0, #_thread_offset_to_r10]
	nop
	nop
	tmov r10, r2
	tloadr r2, [r0, #_thread_offset_to_r11]
	nop
	nop
	tmov r11, r2
	tloadr r2, [r0, #_thread_offset_to_r12]
	nop
	nop
	tmov r12, r2
	tjex lr

/*
 * A thread preempted inside an interrupt resumes here, in SVC mode, with the
 * frame on its own stack. Put it back where treti expects to find it.
 */
.Lirq_unwind:
	tmov r4, r13
	tmov r5, #TC32_IRQ_FRAME_SIZE
	tadd r5, r4, r5
	tmov r13, r5

	tmov r5, #TC32_MODE_IRQ
	tmcsr r5
	tmov r5, r13
	tmov r6, #TC32_IRQ_FRAME_SIZE
	tsub r5, r5, r6
	tmov r13, r5

	tmov r6, #TC32_IRQ_FRAME_WORDS
.Lirq_frame_in:
	tloadr r7, [r4]
	tstorer r7, [r5]
	tmov r2, #4
	tadd r4, r4, r2
	tadd r5, r5, r2
	tmov r2, #1
	tsub r6, r6, r2
	tcmp r6, #0
	nop
	nop
	tjne .Lirq_frame_in

	/*
	 * Same reason z_tc32_thread_start publishes the gate: this path does
	 * not return into do_swap(), so nothing else will reopen reg_irq_en
	 * for the thread. A thread only ever takes an interrupt with the gate
	 * open, so 1 is the state it was interrupted in. The store is safe
	 * here and nowhere earlier: IRQ mode still holds the I bit, so the
	 * frame below sp cannot be reused until treti drops it.
	 */
	tloadr r4, .Lresched_irq_en
	nop
	nop
	tmov r5, #1
	tstorerb r5, [r4]

.Lno_irq_reschedule:
#endif

.Lirq_restore:
	tpop {r0-r5}
	tmov r8, r1
	tmov r9, r2
	tmov r10, r3
	tmov r11, r4
	tmov r12, r5

	tmssr r0
	tpop {r0-r7}
	treti {r15}

#ifdef CONFIG_MULTITHREADING
	.align 2
.Lkernel:
	.word _kernel
.Lirq_restore_ptr:
	.word .Lirq_restore
.Lswitch_from:
	.word z_tc32_switch_from
.Lirq_unwind_ptr:
	.word .Lirq_unwind
.Lresched_irq_en:
	.word 0x00800643
#endif
