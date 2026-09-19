/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_ARCH_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_ARCH_H_

#include <zephyr/irq.h>

#include <zephyr/devicetree.h>

#if !defined(_ASMLANGUAGE) && !defined(__ASSEMBLER__)
#include <zephyr/arch/common/ffs.h>
#include <zephyr/arch/common/sys_bitops.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/arch/tc32/exception.h>
#include <zephyr/arch/tc32/irq.h>
#include <zephyr/arch/tc32/thread.h>
#include <zephyr/sw_isr_table.h>
#include <zephyr/sys/util.h>

#define ARCH_STACK_PTR_ALIGN 8
#define TC32_BOOT_RAM_MIRROR_CODE \
	__attribute__((noinline, section(".boot_ram_mirror")))

#ifdef __cplusplus
extern "C" {
#endif

static ALWAYS_INLINE void arch_nop(void)
{
	__asm__ volatile ("nop");
}

extern uint32_t sys_clock_cycle_get_32(void);

static inline uint32_t arch_k_cycle_get_32(void)
{
	return sys_clock_cycle_get_32();
}

extern uint64_t sys_clock_cycle_get_64(void);

static inline uint64_t arch_k_cycle_get_64(void)
{
	return sys_clock_cycle_get_64();
}

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_ARCH_H_ */
