/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

GTEXT(z_tc32_switch)
GTEXT(z_tc32_thread_start)
GTEXT(z_tc32_switch_to_main_static)
GTEXT(z_tc32_switch_to_thread)
GTEXT(z_tc32_switch_to_main)
GTEXT(z_thread_entry)
GTEXT(z_thread_abort)
GTEXT(main)

/* void z_tc32_switch(struct k_thread *new, struct k_thread *old) */
SECTION_FUNC(TEXT, z_tc32_switch)
	tmov r2, r13
	tstorer r2, [r1, #_thread_offset_to_sp]
	tmov r2, r14
	tstorer r2, [r1, #_thread_offset_to_lr]
	tstorer r4, [r1, #_thread_offset_to_r4]
	tstorer r5, [r1, #_thread_offset_to_r5]
	tstorer r6, [r1, #_thread_offset_to_r6]
	tstorer r7, [r1, #_thread_offset_to_r7]
	tmov r2, r8
	tstorer r2, [r1, #_thread_offset_to_r8]
	tmov r2, r9
	tstorer r2, [r1, #_thread_offset_to_r9]
	tmov r2, r10
	tstorer r2, [r1, #_thread_offset_to_r10]
	tmov r2, r11
	tstorer r2, [r1, #_thread_offset_to_r11]
	tmov r2, r12
	tstorer r2, [r1, #_thread_offset_to_r12]

	tmov r2, #___thread_t_switch_handle_OFFSET
	tadd r2, r2, r1
	tstorer r1, [r2]

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

SECTION_FUNC(TEXT, z_tc32_thread_start)
	tmov r5, r13
	tloadr r4, [r5, #__struct_arch_esf_r0_OFFSET]
	tloadr r0, [r5, #__struct_arch_esf_r1_OFFSET]
	tloadr r1, [r5, #__struct_arch_esf_r2_OFFSET]
	tloadr r2, [r5, #__struct_arch_esf_r3_OFFSET]
	tloadr r3, [r5, #__struct_arch_esf_sr_OFFSET]
	tadd r5, r5, #__struct_arch_esf_SIZEOF
	tmov r13, r5
	/*
	 * A thread is switched to with reg_irq_en cleared, and it enters here
	 * instead of returning into do_swap(), so nothing else will restore
	 * the gate for it. Publish the state arch_new_thread() recorded in the
	 * synthetic frame, the way z_tc32_switch_to_main does for main.
	 */
	tloadr r5, .Lthread_start_irq_en
	nop
	nop
	tstorerb r3, [r5]
	tjl .Lthread_entry_call
	nop
	nop
.Lthread_finished:
	tloadr r0, .Lthread_kernel
	nop
	nop
	tloadr r0, [r0, #(___kernel_t_cpus_OFFSET + ___cpu_t_current_OFFSET)]
	tjl z_thread_abort
	nop
	nop
.Lthread_returned:
	tj .Lthread_returned
.Lthread_entry_call:
	tjex r4
	nop
	nop
	.align 2
.Lthread_kernel:
	.word _kernel
.Lthread_start_irq_en:
	.word 0x00800643

SECTION_FUNC(TEXT, z_thread_entry)
	tmov r4, r0
	tmov r0, r1
	tmov r1, r2
	tmov r2, r3
	tjl .Lz_thread_entry_call
	nop
	nop
.Lz_thread_entry_finished:
	tloadr r0, .Lz_thread_entry_kernel
	nop
	nop
	tloadr r0, [r0, #(___kernel_t_cpus_OFFSET + ___cpu_t_current_OFFSET)]
	tjl z_thread_abort
	nop
	nop
.Lz_thread_entry_returned:
	tj .Lz_thread_entry_returned
.Lz_thread_entry_call:
	tjex r4
	nop
	nop
	.align 2
.Lz_thread_entry_kernel:
	.word _kernel

/* void z_tc32_switch_to_main_static(void) */
SECTION_FUNC(TEXT, z_tc32_switch_to_main_static)
	tloadr r0, .Lmain_thread
	tloadr r1, .Lkernel
	nop
	nop
	tstorer r0, [r1, #(___kernel_t_cpus_OFFSET + ___cpu_t_current_OFFSET)]
	tloadr r2, [r0, #_thread_offset_to_sp]
	nop
	nop
	tmov r13, r2
	tjl main
.Lmain_returned:
	tj .Lmain_returned

	.align 2
.Lmain_thread:
	.word z_main_thread
.Lkernel:
	.word _kernel

/* void z_tc32_switch_to_thread(struct k_thread *thread) */
SECTION_FUNC(TEXT, z_tc32_switch_to_thread)
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

/* void z_tc32_switch_to_main(char *stack_ptr, k_thread_entry_t entry) */
SECTION_FUNC(TEXT, z_tc32_switch_to_main)
	tmov r13, r0
	tmov r0, r1
	tmov r4, #1
	tbclr r0, r4
	tmov r1, #0
	tmov r2, #0
	tmov r3, #0
	tloadr r4, .Lswitch_main_irq_en
	nop
	nop
	tmov r5, #1
	tstorerb r5, [r4]
	tjl z_thread_entry

	.align 2
.Lswitch_main_irq_en:
	.word 0x00800643
