/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The body new.c and old.c share. Before including it, a file defines
 * CONV_TABLE and DIV_TABLE (the names of its tables) and DIV(n, d) and
 * MOD(n, d). Every function is out of line and reached through a table, so
 * its argument is never a constant.
 */

#define CONV_FN(f, t, r, sz)                                                                       \
	static __noinline uint64_t f##_##t##_##r##sz##_a64(uint64_t v)                            \
	{                                                                                          \
		return k_##f##_to_##t##_##r##sz(v);                                                \
	}                                                                                          \
	static __noinline uint64_t f##_##t##_##r##sz##_a32(uint32_t v)                            \
	{                                                                                          \
		return k_##f##_to_##t##_##r##sz(v);                                                \
	}

#define CONV_FNS(f, t)                                                                             \
	CONV_FN(f, t, floor, 32)                                                                   \
	CONV_FN(f, t, floor, 64)                                                                   \
	CONV_FN(f, t, near, 32)                                                                    \
	CONV_FN(f, t, near, 64)                                                                    \
	CONV_FN(f, t, ceil, 32)                                                                    \
	CONV_FN(f, t, ceil, 64)

CONV_PAIRS(CONV_FNS)

#define CONV_ENTRY(f, t, r, sz)                                                                    \
	{"k_" #f "_to_" #t "_" #r #sz, f##_##t##_##r##sz##_a64, f##_##t##_##r##sz##_a32},

#define CONV_ENTRIES(f, t)                                                                         \
	CONV_ENTRY(f, t, floor, 32)                                                                \
	CONV_ENTRY(f, t, floor, 64)                                                                \
	CONV_ENTRY(f, t, near, 32)                                                                 \
	CONV_ENTRY(f, t, near, 64)                                                                 \
	CONV_ENTRY(f, t, ceil, 32)                                                                 \
	CONV_ENTRY(f, t, ceil, 64)

const struct conv_fns CONV_TABLE[CONV_COUNT] = {CONV_PAIRS(CONV_ENTRIES)};

#define DIV_FN(d)                                                                                  \
	static __noinline uint64_t div_##d(uint64_t n)                                             \
	{                                                                                          \
		return DIV(n, d##U);                                                               \
	}                                                                                          \
	static __noinline uint64_t mod_##d(uint64_t n)                                             \
	{                                                                                          \
		return MOD(n, d##U);                                                               \
	}

DIVISORS(DIV_FN)

#define DIV_ENTRY(d) {d##U, div_##d, mod_##d},

const struct div_fns DIV_TABLE[DIVISOR_COUNT] = {DIVISORS(DIV_ENTRY)};
