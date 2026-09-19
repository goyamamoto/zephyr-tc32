/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_ARCH_INLINES_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_ARCH_INLINES_H_

#ifndef _ASMLANGUAGE

#include <zephyr/kernel_structs.h>

static ALWAYS_INLINE unsigned int arch_num_cpus(void)
{
	return CONFIG_MP_MAX_NUM_CPUS;
}

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_ARCH_INLINES_H_ */
