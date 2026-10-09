/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The standard-syntax twin of switch.S: the same code and data in unified
 * Thumb syntax, the TC32-only instructions as .inst.n with the Thumb value
 * that stands for each (the Telink instruction in the comment). The direct
 * path (TC32_DIRECT) assembles this file with clang -mcpu=tc32; the Thumb
 * path assembles switch.S through tc32asm2thumb.py. Every build assembles
 * both with the same flags and compares the objects (asm_twin_check.py).
 */
	.syntax unified
	.thumb

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

GTEXT(z_tc32_switch)
GTEXT(z_tc32_thread_start)
GTEXT(z_tc32_switch_to_main)
GTEXT(z_thread_abort)

/* void z_tc32_switch(struct k_thread *new, struct k_thread *old) */
SECTION_FUNC(TEXT, z_tc32_switch)
	mov r2, r13
	str r2, [r1, #_thread_offset_to_sp]
	mov r2, r14
	str r2, [r1, #_thread_offset_to_lr]
	str r4, [r1, #_thread_offset_to_r4]
	str r5, [r1, #_thread_offset_to_r5]
	str r6, [r1, #_thread_offset_to_r6]
	str r7, [r1, #_thread_offset_to_r7]
	mov r2, r8
	str r2, [r1, #_thread_offset_to_r8]
	mov r2, r9
	str r2, [r1, #_thread_offset_to_r9]
	mov r2, r10
	str r2, [r1, #_thread_offset_to_r10]
	mov r2, r11
	str r2, [r1, #_thread_offset_to_r11]
	mov r2, r12
	str r2, [r1, #_thread_offset_to_r12]

	movs r2, #___thread_t_switch_handle_OFFSET
	adds r2, r2, r1
	str r1, [r2]

	ldr r2, [r0, #_thread_offset_to_sp]
	nop
	nop
	mov r13, r2
	ldr r2, [r0, #_thread_offset_to_lr]
	nop
	nop
	mov r14, r2
	ldr r4, [r0, #_thread_offset_to_r4]
	ldr r5, [r0, #_thread_offset_to_r5]
	ldr r6, [r0, #_thread_offset_to_r6]
	ldr r7, [r0, #_thread_offset_to_r7]
	ldr r2, [r0, #_thread_offset_to_r8]
	nop
	nop
	mov r8, r2
	ldr r2, [r0, #_thread_offset_to_r9]
	nop
	nop
	mov r9, r2
	ldr r2, [r0, #_thread_offset_to_r10]
	nop
	nop
	mov r10, r2
	ldr r2, [r0, #_thread_offset_to_r11]
	nop
	nop
	mov r11, r2
	ldr r2, [r0, #_thread_offset_to_r12]
	nop
	nop
	mov r12, r2
	bx lr

SECTION_FUNC(TEXT, z_tc32_thread_start)
	mov r5, r13
	ldr r4, [r5, #__struct_arch_esf_r0_OFFSET]
	ldr r0, [r5, #__struct_arch_esf_r1_OFFSET]
	ldr r1, [r5, #__struct_arch_esf_r2_OFFSET]
	ldr r2, [r5, #__struct_arch_esf_r3_OFFSET]
	ldr r3, [r5, #__struct_arch_esf_sr_OFFSET]
	adds r5, r5, #__struct_arch_esf_SIZEOF
	mov r13, r5
	/*
	 * A thread is switched to with the interrupt enable (0x643) cleared,
	 * and it enters here instead of returning into do_swap(), so nothing
	 * else will restore the gate for it. Publish the state arch_new_thread() recorded in the
	 * synthetic frame, the way z_tc32_switch_to_main does for main.
	 */
	ldr r5, .Lthread_start_irq_en
	nop
	nop
	strb r3, [r5]
	bl .Lthread_entry_call
	nop
	nop
.Lthread_finished:
	ldr r0, .Lthread_kernel
	nop
	nop
	ldr r0, [r0, #(___kernel_t_cpus_OFFSET + ___cpu_t_current_OFFSET)]
	bl z_thread_abort
	nop
	nop
.Lthread_returned:
	b .Lthread_returned
.Lthread_entry_call:
	bx r4
	nop
	nop
	.align 2
.Lthread_kernel:
	.word _kernel
.Lthread_start_irq_en:
	.word 0x00800643

/* void z_tc32_switch_to_main(char *stack_ptr, k_thread_entry_t entry) */
SECTION_FUNC(TEXT, z_tc32_switch_to_main)
	mov r13, r0
	adds r0, r1, #0
	movs r4, #1
	bics r0, r4
	movs r1, #0
	movs r2, #0
	movs r3, #0
	ldr r4, .Lswitch_main_irq_en
	nop
	nop
	movs r5, #1
	strb r5, [r4]
	bl z_thread_entry

	.align 2
.Lswitch_main_irq_en:
	.word 0x00800643
