/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The conversions with the divisions of time_units.h done by the / and %
 * operators, as on every other architecture: the 64-bit ones go to
 * __aeabi_uldivmod.
 */

#include <zephyr/kernel.h>
#include "conv.h"

#undef z_tmcvt_div64
#undef z_tmcvt_mod64
#undef z_tmcvt_div32
#define z_tmcvt_div64(__n, __d) ((__n) / (__d))
#define z_tmcvt_mod64(__n, __d) ((__n) % (__d))
#define z_tmcvt_div32(__v, __d) ((__v) / (__d))

#define CONV_TABLE conv_old
#define DIV_TABLE  div_old
#define DIV(n, d)  ((uint64_t)(n) / (d))
#define MOD(n, d)  ((uint64_t)(n) % (d))

#include "conv_body.inc"
