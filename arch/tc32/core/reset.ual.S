/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The standard-syntax twin of reset.S: the same code and data in unified
 * Thumb syntax, the TC32-only instructions as .inst.n with the Thumb value
 * that stands for each (the Telink instruction in the comment). The direct
 * path (TC32_DIRECT) assembles this file with clang -mcpu=tc32; the Thumb
 * path assembles reset.S through tc32asm2thumb.py. Every build assembles
 * both with the same flags and compares the objects (asm_twin_check.py).
 */
	.syntax unified
	.thumb

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
/*
 * On the outgoing thread's stack the frame gets one more word: the thread's
 * own lr. It is banked, so the IRQ frame does not hold it, and the thread
 * resumes through .Lirq_unwind with lr loaded from its switch state.
 */
#define TC32_THREAD_FRAME_SIZE (TC32_IRQ_FRAME_SIZE + 4)

GTEXT(__irq)
GTEXT(z_tc32_irq_entry)
GTEXT(z_tc32_irq_unwind)
GTEXT(z_tc32_handle_irqs)
GTEXT(z_tc32_switch)
GDATA(z_tc32_switch_from)

/*
 * The vectors and the code from reset to z_cstart are in
 * soc/telink/tlsr/tlsr827x/setup/tlsr8278_startup.S; this file holds the
 * interrupt entry.
 */

SECTION_FUNC(boot_ram_mirror, __irq)
z_tc32_irq_entry:
	push {r14}
	push {r0-r7}
	.inst.n 0xbbd8 @ tmrss r0

	mov r1, r8
	mov r2, r9
	mov r3, r10
	mov r4, r11
	mov r5, r12
	push {r0-r5}

	/* bl sets lr to the next instruction. */
	bl z_tc32_handle_irqs

	/*
	 * r0 is the thread to run, or NULL to resume the interrupted one.
	 * Every low register is dead here: the interrupted values are in the
	 * frame below and the C dispatcher has clobbered the rest.
	 */
	cmp r0, #0
	nop
	nop
	beq .Lno_irq_reschedule

	/*
	 * The frame sits on the banked IRQ stack, which the next interrupt
	 * reuses, so it has to move onto the outgoing thread's own stack
	 * before anyone else runs. Reaching that stack means borrowing SVC
	 * mode to read and lower its sp, and to save its banked lr, which
	 * the thread may still need (a leaf function's return address).
	 */
	mov r4, r13

	movs r5, #TC32_MODE_SVC
	.inst.n 0xbbc5 @ tmcsr r5
	mov r5, r13
	movs r6, #TC32_THREAD_FRAME_SIZE
	subs r5, r5, r6
	mov r13, r5
	mov r6, r14
	str r6, [r5, #TC32_IRQ_FRAME_SIZE]
	movs r6, #TC32_MODE_IRQ
	.inst.n 0xbbc6 @ tmcsr r6

	adds r3, r5, #0
	movs r6, #TC32_IRQ_FRAME_WORDS
.Lirq_frame_out:
	ldr r7, [r4]
	str r7, [r5]
	movs r2, #4
	adds r4, r4, r2
	adds r5, r5, r2
	movs r2, #1
	subs r6, r6, r2
	cmp r6, #0
	nop
	nop
	bne .Lirq_frame_out

	mov r4, r13
	movs r5, #TC32_IRQ_FRAME_SIZE
	adds r4, r4, r5
	mov r13, r4

	/* Park the outgoing thread on .Lirq_unwind with the relocated frame. */
	ldr r1, .Lswitch_from
	nop
	nop
	ldr r1, [r1]
	str r3, [r1, #_thread_offset_to_sp]
	ldr r2, .Lirq_unwind_ptr
	nop
	nop
	str r2, [r1, #_thread_offset_to_lr]

	/*
	 * Hand over the way every other switch in this port does: with
	 * the interrupt enable (0x643) shut, so whatever the incoming thread resumes into
	 * reopens the gate itself (arch_irq_unlock() in do_swap(),
	 * z_tc32_thread_start(), or .Lirq_unwind below). Shutting it here also
	 * covers the register load, which runs with the I bit already dropped
	 * because a thread must not be entered with it set.
	 */
	ldr r1, .Lresched_irq_en
	nop
	nop
	movs r2, #0
	strb r2, [r1]
	nop
	nop

	/* Become the incoming thread; its stack is an SVC-mode stack. */
	movs r1, #TC32_MODE_SVC_IRQ_ON
	.inst.n 0xbbc1 @ tmcsr r1

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

/*
 * A thread preempted inside an interrupt resumes here, in SVC mode, with the
 * frame on its own stack. Put it back where treti (.inst.n 0xb900) expects to find it.
 */
z_tc32_irq_unwind:
.Lirq_unwind:
	mov r4, r13
	ldr r5, [r4, #TC32_IRQ_FRAME_SIZE]
	nop
	nop
	mov r14, r5
	movs r5, #TC32_THREAD_FRAME_SIZE
	adds r5, r4, r5
	mov r13, r5

	movs r5, #TC32_MODE_IRQ
	.inst.n 0xbbc5 @ tmcsr r5
	mov r5, r13
	movs r6, #TC32_IRQ_FRAME_SIZE
	subs r5, r5, r6
	mov r13, r5

	movs r6, #TC32_IRQ_FRAME_WORDS
.Lirq_frame_in:
	ldr r7, [r4]
	str r7, [r5]
	movs r2, #4
	adds r4, r4, r2
	adds r5, r5, r2
	movs r2, #1
	subs r6, r6, r2
	cmp r6, #0
	nop
	nop
	bne .Lirq_frame_in

	/*
	 * Same reason z_tc32_thread_start publishes the gate: this path does
	 * not return into do_swap(), so nothing else will reopen the interrupt
	 * enable (0x643) for the thread. A thread only ever takes an interrupt with the gate
	 * open, so 1 is the state it was interrupted in. The store is safe
	 * here and nowhere earlier: IRQ mode still holds the I bit, so the
	 * frame below sp cannot be reused until treti drops it.
	 */
	ldr r4, .Lresched_irq_en
	nop
	nop
	movs r5, #1
	strb r5, [r4]

.Lno_irq_reschedule:

.Lirq_restore:
	pop {r0-r5}
	mov r8, r1
	mov r9, r2
	mov r10, r3
	mov r11, r4
	mov r12, r5

	.inst.n 0xbbd0 @ tmssr r0
	pop {r0-r7}
	.inst.n 0xb900 @ treti {r15}

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
