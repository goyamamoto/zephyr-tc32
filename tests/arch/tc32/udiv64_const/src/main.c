/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The TC32's 64-bit division by a constant through a multiply
 * (zephyr/arch/tc32/udiv64_const.h) against the / and % operators
 * (__aeabi_uldivmod), both run on the TC32:
 *
 * - test_divisors: for each divisor of the time unit conversions at
 *   10000 ticks/s and 16 MHz, floor(n / d) and n mod d for the special
 *   values (0, 1, d - 1, d, d + 1, around multiples of d next to 2^32,
 *   2^63 and 2^64 - 1, 2^64 - 1) and RANDOM_VALUES pseudo-random n. Each
 *   divisor's line gives a hash of the results, which host/check.py
 *   recomputes with the host's arithmetic.
 * - test_conversions: every k_<from>_to_<to>_<rounding><32|64>() as
 *   time_units.h has it on the TC32 against the same code with the divisions
 *   done by the operators, with a 64-bit and with a 32-bit argument, for the
 *   special values (the rounding bounds of int_div_32 among them) and
 *   CONV_RANDOM_VALUES pseudo-random ones.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include "conv.h"

#define RANDOM_VALUES      200000U
#define CONV_RANDOM_VALUES 2000U
#define MAX_REPORTS        8U

static uint64_t rng_state;

/* xorshift64 (13, 7, 17). */
static uint64_t rnd64(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

static uint32_t hash_word(uint32_t h, uint32_t w)
{
	return (h ^ w) * 16777619U;
}

static unsigned int ceil_log2(uint32_t d)
{
	unsigned int l = 0U;

	while (((uint64_t)1 << l) < d) {
		l++;
	}
	return l;
}

struct div_run {
	const struct div_fns *new_fns;
	const struct div_fns *old_fns;
	uint32_t values;
	uint32_t mismatches;
	uint32_t hash;
};

static void div_check(struct div_run *run, uint64_t n)
{
	uint64_t q = run->new_fns->div(n);
	uint64_t r = run->new_fns->mod(n);
	uint64_t q_old = run->old_fns->div(n);
	uint64_t r_old = run->old_fns->mod(n);

	if (q != q_old || r != r_old) {
		if (run->mismatches < MAX_REPORTS) {
			TC_PRINT("MISMATCH d %u n 0x%016llx: q 0x%016llx r 0x%016llx, operators "
				 "q 0x%016llx r 0x%016llx\n",
				 run->new_fns->d, (unsigned long long)n, (unsigned long long)q,
				 (unsigned long long)r, (unsigned long long)q_old,
				 (unsigned long long)r_old);
		}
		run->mismatches++;
	}
	run->hash = hash_word(run->hash, (uint32_t)q);
	run->hash = hash_word(run->hash, (uint32_t)(q >> 32));
	run->hash = hash_word(run->hash, (uint32_t)r);
	run->values++;
}

/*
 * n = k d + o for k next to bound / d (k - 1, k, k + 1) and o in -1, 0, 1, where it fits
 * 64 bits.
 */
static void div_near(struct div_run *run, uint64_t bound)
{
	uint64_t d = run->new_fns->d;
	uint64_t k = bound / d;

	for (int j = -1; j <= 1; j++) {
		uint64_t kk = k + (uint64_t)(int64_t)j;

		if (kk > UINT64_MAX / d) {
			continue;
		}
		uint64_t base = kk * d;

		for (int o = -1; o <= 1; o++) {
			if ((o < 0 && base == 0U) || (o > 0 && base == UINT64_MAX)) {
				continue;
			}
			div_check(run, base + (uint64_t)(int64_t)o);
		}
	}
}

ZTEST(udiv64_const, test_divisors)
{
	static const uint64_t special[] = {
		0U, 1U, 2U, 0xffffffffULL, 0x100000000ULL, 0x100000001ULL,
		0x7fffffffffffffffULL, 0x8000000000000000ULL, 0x8000000000000001ULL,
		0xfffffffffffffffeULL, 0xffffffffffffffffULL,
	};
	uint32_t total = 0U;
	uint32_t bad = 0U;

	for (size_t i = 0; i < DIVISOR_COUNT; i++) {
		struct div_run run = {
			.new_fns = &div_new[i], .old_fns = &div_old[i], .hash = 2166136261U,
		};
		uint64_t d = div_new[i].d;
		unsigned int l = ceil_log2(div_new[i].d);

		zassert_equal(div_new[i].d, div_old[i].d);
		for (size_t j = 0; j < ARRAY_SIZE(special); j++) {
			div_check(&run, special[j]);
		}
		div_check(&run, d - 1U);
		div_check(&run, d);
		div_check(&run, d + 1U);
		div_check(&run, 2U * d - 1U);
		div_check(&run, 2U * d);
		div_check(&run, 2U * d + 1U);
		div_near(&run, 0x100000000ULL);
		div_near(&run, 0x8000000000000000ULL);
		div_near(&run, UINT64_MAX);

		rng_state = 0x9e3779b97f4a7c15ULL ^ d;
		for (uint32_t v = 0U; v < RANDOM_VALUES; v++) {
			uint64_t x = rnd64();
			uint64_t n;

			switch (v & 3U) {
			case 0:
				n = x;
				break;
			case 1:
				n = x >> (rnd64() & 63U);
				break;
			default: {
				/* q d + r with q below 2^(64 - l), so that it fits. */
				uint64_t q = rnd64() >> l;
				uint32_t sh = (uint32_t)(rnd64() >> 32) % (65U - l);
				uint64_t r;

				q = sh >= 64U - l ? 0U : q >> sh;
				if ((v & 3U) == 2U) {
					r = (x & 1U) != 0U ? 1U : 0U;
				} else {
					r = (x & 1U) != 0U ? d - 1U : d - 2U;
				}
				n = q * d + r;
				break;
			}
			}
			div_check(&run, n);
		}
		TC_PRINT("divisor %u: %u values, %u mismatches, hash %08x\n", div_new[i].d,
			 run.values, run.mismatches, run.hash);
		total += run.values;
		bad += run.mismatches;
	}
	TC_PRINT("divisors: %u values, %u mismatches\n", total, bad);
	zassert_equal(bad, 0U, "%u mismatches", bad);
}

struct conv_run {
	uint32_t values;
	uint32_t mismatches;
};

static void conv_check(struct conv_run *run, size_t i, uint64_t t)
{
	uint64_t a = conv_new[i].arg64(t);
	uint64_t b = conv_old[i].arg64(t);
	uint64_t a32 = conv_new[i].arg32((uint32_t)t);
	uint64_t b32 = conv_old[i].arg32((uint32_t)t);

	if (a != b || a32 != b32) {
		if (run->mismatches < MAX_REPORTS) {
			TC_PRINT("MISMATCH %s t 0x%016llx: 0x%016llx / 0x%016llx, operators "
				 "0x%016llx / 0x%016llx\n",
				 conv_new[i].name, (unsigned long long)t, (unsigned long long)a,
				 (unsigned long long)a32, (unsigned long long)b,
				 (unsigned long long)b32);
		}
		run->mismatches++;
	}
	run->values += 2U;
}

ZTEST(udiv64_const, test_conversions)
{
	static const uint32_t divisors[] = {10U, 16U, 100U, 1600U, 10000U, 16000U, 100000U,
					    16000000U, 1000000000U};
	static const uint64_t special[] = {
		0U, 1U, 2U, 3U, 5U, 9U, 15U, 0x7fffffffU, 0x80000000U, 0xfffffffeU,
		0xffffffffU, 0x100000000ULL, 0x100000001ULL, 0x7fffffffffffffffULL,
		0x8000000000000000ULL, 0x8000000000000001ULL, 0xfffffffffffffffeULL,
		0xffffffffffffffffULL,
	};
	struct conv_run run = {0};

	for (size_t i = 0; i < CONV_COUNT; i++) {
		zassert_true(strcmp(conv_new[i].name, conv_old[i].name) == 0);
		for (size_t j = 0; j < ARRAY_SIZE(special); j++) {
			conv_check(&run, i, special[j]);
		}
		/* d - 1 to d + 1, and the bound of int_div_32's 32-bit branch for each offset. */
		for (size_t j = 0; j < ARRAY_SIZE(divisors); j++) {
			uint32_t d = divisors[j];
			uint32_t offs[] = {0U, d / 2U, d - 1U};

			for (int o = -1; o <= 1; o++) {
				conv_check(&run, i, (uint64_t)d + (uint64_t)(int64_t)o);
			}
			for (size_t k = 0; k < ARRAY_SIZE(offs); k++) {
				uint64_t b = 0xffffffffULL - offs[k];

				for (int o = -1; o <= 1; o++) {
					conv_check(&run, i, b + (uint64_t)(int64_t)o);
				}
			}
		}
		rng_state = 0x2545f4914f6cdd1dULL ^ i;
		for (uint32_t v = 0U; v < CONV_RANDOM_VALUES; v++) {
			uint64_t x = rnd64();

			conv_check(&run, i, (v & 1U) != 0U ? x >> (rnd64() & 63U) : x);
		}
	}
	TC_PRINT("conversions: %u, %u values, %u mismatches\n", CONV_COUNT, run.values,
		 run.mismatches);
	zassert_equal(run.mismatches, 0U, "%u mismatches", run.mismatches);
}

ZTEST_SUITE(udiv64_const, NULL, NULL, NULL, NULL, NULL);
