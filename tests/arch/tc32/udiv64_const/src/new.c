/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The conversions as time_units.h has them on the TC32, and the
 * constant-divisor division and remainder of udiv64_const.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/tc32/udiv64_const.h>
#include "conv.h"

#define DIVISOR_TAKES_MULTIPLY(d) BUILD_ASSERT(Z_TC32_UDIV64_BY_MUL(d##U));
DIVISORS(DIVISOR_TAKES_MULTIPLY)

#define CONV_TABLE conv_new
#define DIV_TABLE  div_new
#define DIV(n, d)  z_tc32_udiv64(n, d)
#define MOD(n, d)  z_tc32_umod64(n, d)

#include "conv_body.inc"
