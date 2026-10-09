/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The divisions of the time unit conversions on the TC32
 *
 * CONFIG_ARCH_HAS_TMCVT_DIV (<zephyr/arch/tmcvt.h>). A 64-bit division by a
 * compile-time constant goes through a multiply (udiv64_const.h), which gives
 * the same quotient and remainder as the operators for every value. A 64-bit
 * value known to lie in 0 to 2^32 - 1 is divided in 32 bits, which gives the
 * same quotient. C++ keeps the operators.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_TMCVT_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_TMCVT_H_

#ifndef __cplusplus

#include <stdint.h>
#include <zephyr/arch/tc32/udiv64_const.h>

#define arch_tmcvt_div64(__n, __d) z_tc32_udiv64(__n, __d)
#define arch_tmcvt_mod64(__n, __d) z_tc32_umod64(__n, __d)
#define arch_tmcvt_div32(__v, __d)                                                                 \
	((sizeof(__v) > sizeof(uint32_t)) ? ((uint32_t)(__v) / (__d)) : ((__v) / (__d)))

#else

#define arch_tmcvt_div64(__n, __d) ((__n) / (__d))
#define arch_tmcvt_mod64(__n, __d) ((__n) % (__d))
#define arch_tmcvt_div32(__v, __d) ((__v) / (__d))

#endif /* __cplusplus */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_TMCVT_H_ */
