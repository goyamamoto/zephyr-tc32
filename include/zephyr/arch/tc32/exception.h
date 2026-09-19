/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_EXCEPTION_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_EXCEPTION_H_

#ifndef _ASMLANGUAGE
#include <stdint.h>

struct arch_esf {
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t sr;
};

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_EXCEPTION_H_ */
