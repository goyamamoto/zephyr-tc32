/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <ksched.h>

void z_thread_entry(k_thread_entry_t thread, void *arg1, void *arg2, void *arg3);
void z_tc32_switch_to_main(char *stack_ptr, k_thread_entry_t entry);
void z_tc32_switch_to_main_static(void);
void z_tc32_switch_to_thread(struct k_thread *thread);

void arch_new_thread(struct k_thread *thread, k_thread_stack_t *stack,
		     char *stack_ptr, k_thread_entry_t entry,
		     void *p1, void *p2, void *p3)
{
	extern void z_tc32_thread_start(void);
	struct arch_esf *stack_init;

	stack_init = (struct arch_esf *)Z_STACK_PTR_ALIGN(
		Z_STACK_PTR_TO_FRAME(struct arch_esf, stack_ptr));

	/* The synthetic frame carries raw TC32 branch targets, not ABI-tagged pointers. */
	stack_init->r0 = (uint32_t)entry & ~1u;
	stack_init->r1 = (uint32_t)p1;
	stack_init->r2 = (uint32_t)p2;
	stack_init->r3 = (uint32_t)p3;
	stack_init->pc = (uint32_t)z_thread_entry & ~1u;
	stack_init->sr = 1u;

	thread->callee_saved.sp = (uint32_t)stack_init;
	thread->callee_saved.lr = (uint32_t)z_tc32_thread_start & ~1u;
	thread->switch_handle = thread;
}

int arch_coprocessors_disable(struct k_thread *thread)
{
	ARG_UNUSED(thread);
	return -ENOTSUP;
}

void arch_switch_to_main_thread(struct k_thread *main_thread, char *stack_ptr,
				k_thread_entry_t entry)
{
	_kernel.cpus[0].current = main_thread;
	z_tc32_switch_to_main(stack_ptr, entry);
	CODE_UNREACHABLE;
}
