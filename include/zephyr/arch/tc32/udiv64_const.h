/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Unsigned 64-bit division by a compile-time constant through a multiply
 *
 * The TC32 multiplies 32 x 32 -> 32 bits only, and a 64-bit division goes to
 * a bit-serial library routine. For a divisor d known at compile time,
 * q = floor(n / d) for every 64-bit n is computed as in Granlund and
 * Montgomery, "Division by Invariant Integers using Multiplication" (PLDI
 * 1994), Figure 4.1, with N = 64:
 *
 *   l  = ceil(log2(d))                         so 2^(l-1) < d < 2^l
 *   m' = floor(2^64 * (2^l - d) / d) + 1        so 0 < m' < 2^64
 *   t1 = floor(m' * n / 2^64)
 *   q  = (t1 + floor((n - t1) / 2)) >> (l - 1)
 *
 * Why q is exact. Let m = 2^64 + m' = floor(2^(64+l) / d) + 1. Writing
 * 2^(64+l) = Q d + R (0 <= R < d), m d = 2^(64+l) - R + d, so
 * 2^(64+l) < m d <= 2^(64+l) + d <= 2^(64+l) + 2^l. With m d = 2^(64+l) + e
 * (0 < e <= 2^l) and n = q d + r (0 <= r < d):
 *   m n / 2^(64+l) = n / d + e n / (d 2^(64+l)) and
 *   0 <= e n / (d 2^(64+l)) <= n / (d 2^64) < 1 / d,
 * so m n / 2^(64+l) lies in [q + r/d, q + (r+1)/d), inside [q, q + 1): its
 * floor is q. Then m n = 2^64 n + m' n gives
 * floor(m n / 2^(64+l)) = floor((n + t1) / 2^l), n being an integer. Since
 * m' < 2^64, t1 <= n, so t1 + floor((n - t1) / 2) = floor((n + t1) / 2)
 * does not overflow 64 bits, and shifting it right by l - 1 gives
 * floor((n + t1) / 2^l) = q.
 *
 * The divisors this is used for: 3 <= d <= 2^32 - 1, not a power of two (a
 * power of two is a shift already). m' is derived from d by the macros below
 * with 64-bit arithmetic only, as two steps of a long division by d in base
 * 2^32: with A = 2^l - d (0 < A < d), floor(A 2^64 / d) =
 * floor(A 2^32 / d) 2^32 + floor(((A 2^32) mod d) 2^32 / d), each step
 * fitting 64 bits because A < d < 2^32. Z_TC32_UDIV64_VALID() checks the
 * derived m' against the condition the proof uses,
 * A 2^64 < d m' <= A 2^64 + 2^l, again in 64-bit arithmetic (d m' has
 * 96 bits, taken as a high part and a low 32-bit word).
 *
 * The header needs only <stdint.h>, so a host program can include it and run
 * the same code.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_UDIV64_CONST_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_UDIV64_CONST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* l = ceil(log2(d)) for d >= 2; 1 for d = 1, so that the macros below stay defined. */
#define Z_TC32_UDIV64_L(d) (32U - (unsigned int)__builtin_clz(((uint32_t)(d) - 1U) | 1U))

/* A = 2^l - d. */
#define Z_TC32_UDIV64_A(d) ((1ULL << Z_TC32_UDIV64_L(d)) - (uint64_t)(d))

/* m' = floor(A 2^64 / d) + 1, from two long-division steps in base 2^32. */
#define Z_TC32_UDIV64_MAGIC(d)                                                                     \
	((((Z_TC32_UDIV64_A(d) << 32) / (uint64_t)(d)) << 32) +                                    \
	 ((((Z_TC32_UDIV64_A(d) << 32) % (uint64_t)(d)) << 32) / (uint64_t)(d)) + 1U)

/* d m' as P_HI 2^32 + (P_LO mod 2^32). */
#define Z_TC32_UDIV64_P_LO(d) ((uint64_t)(d) * (Z_TC32_UDIV64_MAGIC(d) & 0xffffffffU))
#define Z_TC32_UDIV64_P_HI(d)                                                                      \
	((uint64_t)(d) * (Z_TC32_UDIV64_MAGIC(d) >> 32) + (Z_TC32_UDIV64_P_LO(d) >> 32))

/*
 * A 2^64 < d m' <= A 2^64 + 2^l. With d m' - A 2^64 =
 * (P_HI - A 2^32) 2^32 + (P_LO mod 2^32) and 2^l <= 2^32, this holds when
 * P_HI = A 2^32 and 0 < P_LO mod 2^32 <= 2^l.
 */
#define Z_TC32_UDIV64_VALID(d)                                                                     \
	(Z_TC32_UDIV64_P_HI(d) == (Z_TC32_UDIV64_A(d) << 32) &&                                    \
	 (Z_TC32_UDIV64_P_LO(d) & 0xffffffffU) != 0U &&                                            \
	 (Z_TC32_UDIV64_P_LO(d) & 0xffffffffU) <= (1ULL << Z_TC32_UDIV64_L(d)))

/* d is in the range the derivation covers: 3 <= d <= 2^32 - 1, not a power of two. */
#define Z_TC32_UDIV64_RANGE(d)                                                                     \
	((uint64_t)(d) >= 3U && (uint64_t)(d) <= 0xffffffffU &&                                    \
	 ((uint64_t)(d) & ((uint64_t)(d) - 1U)) != 0U)

/* Division of n by d goes through the multiply: d is a constant in range with a valid m'. */
#define Z_TC32_UDIV64_BY_MUL(d)                                                                    \
	(__builtin_constant_p(d) && Z_TC32_UDIV64_RANGE(d) && Z_TC32_UDIV64_VALID(d))

/*
 * a * b from four 16 x 16 -> 32 bit products. The middle column (bits 16 to
 * 47) is below 3 * 2^16, so no sum here overflows 32 bits. The empty asm
 * statement makes the halves opaque to the compiler, which would otherwise
 * recognize the products as one 64-bit multiply and call the library routine
 * for it. Out of line: called four times by the one copy of
 * z_tc32_udiv64_avg_mulhi_inline() (arch/tc32/core/udiv64_const.c), it keeps
 * that small.
 */
static inline __attribute__((noinline)) uint64_t z_tc32_mul32x32(uint32_t a, uint32_t b)
{
	uint32_t a0 = a & 0xffffU;
	uint32_t a1 = a >> 16;
	uint32_t b0 = b & 0xffffU;
	uint32_t b1 = b >> 16;

	__asm__("" : "+r"(a0), "+r"(a1), "+r"(b0), "+r"(b1));

	uint32_t p00 = a0 * b0;
	uint32_t p01 = a0 * b1;
	uint32_t p10 = a1 * b0;
	uint32_t p11 = a1 * b1;
	uint32_t mid = (p00 >> 16) + (p01 & 0xffffU) + (p10 & 0xffffU);
	uint32_t lo = (mid << 16) | (p00 & 0xffffU);
	uint32_t hi = p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16);

	return ((uint64_t)hi << 32) | lo;
}

/*
 * floor((n + floor(n m / 2^64)) / 2) for 64-bit n and m. With
 * n = n1 2^32 + n0 and m = m1 2^32 + m0,
 * n m = n1 m1 2^64 + (n0 m1 + n1 m0) 2^32 + n0 m0, so
 * floor(n m / 2^64) = n1 m1 + floor(n0 m1 / 2^32) + floor(n1 m0 / 2^32) + floor(c / 2^32)
 * with c = (n0 m1 mod 2^32) + (n1 m0 mod 2^32) + floor(n0 m0 / 2^32) < 3 2^32.
 * The sum is the high half of a 128-bit product, below 2^64. Since m < 2^64,
 * it is at most n, so n - it does not wrap and the average does not overflow.
 */
static inline __attribute__((always_inline)) uint64_t z_tc32_udiv64_avg_mulhi_inline(uint64_t n,
										    uint64_t m)
{
	uint32_t n0 = (uint32_t)n;
	uint32_t n1 = (uint32_t)(n >> 32);
	uint32_t m0 = (uint32_t)m;
	uint32_t m1 = (uint32_t)(m >> 32);
	uint64_t p00 = z_tc32_mul32x32(n0, m0);
	uint64_t p01 = z_tc32_mul32x32(n0, m1);
	uint64_t p10 = z_tc32_mul32x32(n1, m0);
	uint64_t p11 = z_tc32_mul32x32(n1, m1);
	uint64_t c = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
	uint64_t t = p11 + (p01 >> 32) + (p10 >> 32) + (c >> 32);

	return t + ((n - t) >> 1);
}

/* The out-of-line copy of z_tc32_udiv64_avg_mulhi_inline() (arch/tc32/core/udiv64_const.c). */
uint64_t z_tc32_udiv64_avg_mulhi(uint64_t n, uint64_t m) __attribute__((const));

/*
 * floor(n / d) and n mod d for a divisor d with Z_TC32_UDIV64_BY_MUL(d), magic
 * being Z_TC32_UDIV64_MAGIC(d) and l Z_TC32_UDIV64_L(d). A dividend the
 * compiler knows to fit 32 bits (a 32-bit value widened, say) is divided in
 * 32 bits instead, as the / operator then is. The remainder is n - q d in 32
 * bits: it is below d < 2^32, so the low words give it exactly.
 */
static inline __attribute__((always_inline)) uint64_t
z_tc32_udiv64_by_mul(uint64_t n, uint64_t magic, unsigned int l, uint32_t d)
{
	if (__builtin_constant_p((uint32_t)(n >> 32) == 0U) && ((uint32_t)(n >> 32) == 0U)) {
		return (uint32_t)n / d;
	}
	return z_tc32_udiv64_avg_mulhi(n, magic) >> (l - 1U);
}

static inline __attribute__((always_inline)) uint64_t
z_tc32_umod64_by_mul(uint64_t n, uint64_t magic, unsigned int l, uint32_t d)
{
	uint32_t q = (uint32_t)z_tc32_udiv64_by_mul(n, magic, l, d);

	return (uint32_t)n - q * d;
}

/*
 * floor(n / d) and n mod d as uint64_t: through the multiply when d qualifies
 * and n is not a constant, by the operators otherwise (in a constant
 * expression too, where only that branch is evaluated). n is evaluated once.
 */
#define z_tc32_udiv64(n, d)                                                                        \
	((!__builtin_constant_p(n) && Z_TC32_UDIV64_BY_MUL(d))                                     \
		 ? z_tc32_udiv64_by_mul((n), Z_TC32_UDIV64_MAGIC(d), Z_TC32_UDIV64_L(d),           \
					(uint32_t)(d))                                             \
		 : ((uint64_t)(n) / (d)))

#define z_tc32_umod64(n, d)                                                                        \
	((!__builtin_constant_p(n) && Z_TC32_UDIV64_BY_MUL(d))                                     \
		 ? z_tc32_umod64_by_mul((n), Z_TC32_UDIV64_MAGIC(d), Z_TC32_UDIV64_L(d),           \
					(uint32_t)(d))                                             \
		 : ((uint64_t)(n) % (d)))

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_UDIV64_CONST_H_ */
