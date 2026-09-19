/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_THREAD_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>

struct _callee_saved {
	uint32_t sp;
	uint32_t lr;
	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;
	uint32_t r12;
};
typedef struct _callee_saved _callee_saved_t;

struct _thread_arch {
};
typedef struct _thread_arch _thread_arch_t;

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_THREAD_H_ */
