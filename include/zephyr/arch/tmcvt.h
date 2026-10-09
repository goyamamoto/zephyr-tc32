/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The architecture's divisions for the time unit conversions
 *
 * <zephyr/sys/time_units.h> includes this header when the architecture
 * selects CONFIG_ARCH_HAS_TMCVT_DIV. The architecture's header defines
 * arch_tmcvt_div64(n, d) and arch_tmcvt_mod64(n, d), the quotient and the
 * remainder of a uint64_t n by d, and arch_tmcvt_div32(v, d), the quotient of
 * a v known to lie in 0 to 2^32 - 1. Each gives the result of the / or %
 * operator for every value and is a constant expression when its operands
 * are.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TMCVT_H_
#define ZEPHYR_INCLUDE_ARCH_TMCVT_H_

#if defined(CONFIG_TC32)
#include <zephyr/arch/tc32/tmcvt.h>
#else
#error "CONFIG_ARCH_HAS_TMCVT_DIV is selected, but the architecture is missing from this header"
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_TMCVT_H_ */
