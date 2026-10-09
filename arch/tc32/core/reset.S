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
	tpush {r14}
	tpush {r0-r7}
	tmrss r0

	tmov r1, r8
	tmov r2, r9
	tmov r3, r10
	tmov r4, r11
	tmov r5, r12
	tpush {r0-r5}

	/* tjl sets lr to the next instruction. */
	tjl z_tc32_handle_irqs

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
	 * mode to read and lower its sp, and to save its banked lr, which
	 * the thread may still need (a leaf function's return address).
	 */
	tmov r4, r13

	tmov r5, #TC32_MODE_SVC
	tmcsr r5
	tmov r5, r13
	tmov r6, #TC32_THREAD_FRAME_SIZE
	tsub r5, r5, r6
	tmov r13, r5
	tmov r6, r14
	tstorer r6, [r5, #TC32_IRQ_FRAME_SIZE]
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
	 * the interrupt enable (0x643) shut, so whatever the incoming thread resumes into
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
z_tc32_irq_unwind:
.Lirq_unwind:
	tmov r4, r13
	tloadr r5, [r4, #TC32_IRQ_FRAME_SIZE]
	nop
	nop
	tmov r14, r5
	tmov r5, #TC32_THREAD_FRAME_SIZE
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
	 * not return into do_swap(), so nothing else will reopen the interrupt
	 * enable (0x643) for the thread. A thread only ever takes an interrupt with the gate
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
