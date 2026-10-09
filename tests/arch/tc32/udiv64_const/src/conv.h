/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The functions new.c and old.c both define: the same code, new.c with the
 * time unit conversions as time_units.h has them (64-bit divisions by a
 * constant through the multiply), old.c with those divisions done by the /
 * and % operators.
 */

#ifndef UDIV64_CONST_TEST_CONV_H_
#define UDIV64_CONST_TEST_CONV_H_

#include <stdint.h>

/* Every conversion time_units.h defines: from, to. */
#define CONV_PAIRS(X)                                                                              \
	X(sec, cyc)                                                                                \
	X(sec, ticks)                                                                              \
	X(ms, cyc)                                                                                 \
	X(ms, ticks)                                                                               \
	X(us, cyc)                                                                                 \
	X(us, ticks)                                                                               \
	X(ns, cyc)                                                                                 \
	X(ns, ticks)                                                                               \
	X(cyc, sec)                                                                                \
	X(cyc, ms)                                                                                 \
	X(cyc, us)                                                                                 \
	X(cyc, ns)                                                                                 \
	X(cyc, ticks)                                                                              \
	X(ticks, sec)                                                                              \
	X(ticks, ms)                                                                               \
	X(ticks, us)                                                                               \
	X(ticks, ns)                                                                               \
	X(ticks, cyc)

#define CONV_COUNT (18 * 3 * 2)

/* One conversion, called with a 64-bit and with a 32-bit argument; the result widened. */
struct conv_fns {
	const char *name;
	uint64_t (*arg64)(uint64_t t);
	uint64_t (*arg32)(uint32_t t);
};

extern const struct conv_fns conv_new[CONV_COUNT];
extern const struct conv_fns conv_old[CONV_COUNT];

/* The divisors of the 64-bit divisions at 10000 ticks/s and 16 MHz, except 16 (a shift). */
#define DIVISORS(X)                                                                                \
	X(10)                                                                                      \
	X(100)                                                                                     \
	X(1600)                                                                                    \
	X(10000)                                                                                   \
	X(16000)                                                                                   \
	X(100000)                                                                                  \
	X(16000000)                                                                                \
	X(1000000000)

#define DIVISOR_COUNT 8

/* floor(n / d) and n mod d: new.c through z_tc32_udiv64() and z_tc32_umod64(), old.c by / and %. */
struct div_fns {
	uint32_t d;
	uint64_t (*div)(uint64_t n);
	uint64_t (*mod)(uint64_t n);
};

extern const struct div_fns div_new[DIVISOR_COUNT];
extern const struct div_fns div_old[DIVISOR_COUNT];

#endif /* UDIV64_CONST_TEST_CONV_H_ */
