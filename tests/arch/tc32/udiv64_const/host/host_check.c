/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Host check of include/zephyr/arch/tc32/udiv64_const.h, the same code the
 * TC32 runs, against the host's 64-bit division and 128-bit arithmetic:
 *
 * 1. z_tc32_udiv64_avg_mulhi_inline(n, m) equals (n + (n m >> 64)) >> 1 in
 *    128 bits, for special and pseudo-random n and m.
 * 2. For every divisor d from 1 to MAX_D (default 2^20): the range test
 *    (3 <= d, not a power of two) and, in range, l, m' and the validity test
 *    against a derivation in 128-bit arithmetic and the condition of the
 *    proof checked in 128 bits; then the quotient and remainder through the
 *    multiply against n / d and n % d for the special values of d (0, 1,
 *    d - 1, d, d + 1, around multiples of d next to 2^32, 2^63 and 2^64 - 1,
 *    2^64 - 1) and VALUES pseudo-random n (default 10000): uniform 64-bit,
 *    of a random bit length, and q d + r with r in {0, 1, d - 2, d - 1}.
 * 3. Pseudo-random divisors up to 2^32 - 1, divisors next to powers of two,
 *    with fewer values each.
 * 4. The dispatching macros z_tc32_udiv64() and z_tc32_umod64() with
 *    constant divisors (the multiply decided at compile time), among them
 *    those of the TC32's time unit conversions at 10000 ticks/s and 16 MHz.
 *
 * Build and run: cc -O2 -I<zephyr>/include host_check.c -o host_check -lpthread
 *                ./host_check [MAX_D [VALUES [THREADS]]]
 * Exit status 1 on any mismatch; the last line gives the counts.
 */

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/arch/tc32/udiv64_const.h>

typedef unsigned __int128 u128;

uint64_t z_tc32_udiv64_avg_mulhi(uint64_t n, uint64_t m)
{
	return z_tc32_udiv64_avg_mulhi_inline(n, m);
}

static atomic_ulong n_checks;
static atomic_ulong n_fail;
static atomic_ulong n_divisors;
static uint32_t max_d = 1U << 20;
static uint32_t values = 10000U;
static uint32_t threads = 6U;
static atomic_uint next_d = 1U;

static uint64_t splitmix(uint64_t *s)
{
	uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);

	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static void fail(const char *what, uint64_t d, uint64_t n, uint64_t got, uint64_t want)
{
	if (atomic_fetch_add(&n_fail, 1) < 20U) {
		printf("FAIL %s d=%" PRIu64 " n=%" PRIu64 " got=%" PRIu64 " want=%" PRIu64 "\n",
		       what, d, n, got, want);
	}
}

static unsigned int ceil_log2(uint64_t d)
{
	unsigned int l = 0;

	while ((1ULL << l) < d) {
		l++;
	}
	return l;
}

/* The derivation and its validity for d against 128-bit arithmetic; 1 when d is in range. */
static int check_magic(uint64_t d)
{
	int pow2 = (d & (d - 1U)) == 0U;
	int range = Z_TC32_UDIV64_RANGE(d);

	if (range != (d >= 3U && d <= 0xffffffffULL && !pow2)) {
		fail("range", d, 0, range, !range);
		return 0;
	}
	if (!range) {
		return 0;
	}
	unsigned int l = ceil_log2(d);
	u128 mprime = ((((u128)1 << l) - d) << 64) / d + 1U;
	uint64_t magic = Z_TC32_UDIV64_MAGIC(d);

	if (Z_TC32_UDIV64_L(d) != l) {
		fail("l", d, 0, Z_TC32_UDIV64_L(d), l);
	}
	if ((mprime >> 64) != 0U || (uint64_t)mprime != magic) {
		fail("magic", d, 0, magic, (uint64_t)mprime);
	}
	if (!Z_TC32_UDIV64_VALID(d)) {
		fail("valid", d, 0, 0, 1);
	}
	/* 2^(64+l) < (2^64 + m') d <= 2^(64+l) + 2^l, in 128 bits (l <= 32). */
	u128 md = (((u128)1 << 64) + magic) * (u128)d;
	u128 p = (u128)1 << (64 + l);

	if (!(md > p && md <= p + ((u128)1 << l))) {
		fail("condition", d, 0, 0, 1);
	}
	return 1;
}

static void check_n(uint64_t d, uint64_t magic, unsigned int l, uint64_t n)
{
	uint64_t q = z_tc32_udiv64_by_mul(n, magic, l, (uint32_t)d);
	uint64_t r = z_tc32_umod64_by_mul(n, magic, l, (uint32_t)d);

	if (q != n / d) {
		fail("div", d, n, q, n / d);
	}
	if (r != n % d) {
		fail("mod", d, n, r, n % d);
	}
	atomic_fetch_add_explicit(&n_checks, 1, memory_order_relaxed);
}

/* n around k d for the k next to a bound b (a multiple of d below b and the next). */
static void check_near(uint64_t d, uint64_t magic, unsigned int l, uint64_t b)
{
	uint64_t k = b / d;

	for (int j = -1; j <= 1; j++) {
		uint64_t kk = k + (uint64_t)j;
		u128 base = (u128)kk * d;

		for (int o = -1; o <= 1; o++) {
			u128 n = base + (u128)(__int128)o;

			if (n >> 64 == 0U) {
				check_n(d, magic, l, (uint64_t)n);
			}
		}
	}
}

static void check_divisor(uint64_t d, uint32_t nvalues)
{
	if (!check_magic(d)) {
		return;
	}
	atomic_fetch_add(&n_divisors, 1);

	uint64_t magic = Z_TC32_UDIV64_MAGIC(d);
	unsigned int l = Z_TC32_UDIV64_L(d);
	static const uint64_t special[] = {
		0, 1, 2, 0xfffffffeULL, 0xffffffffULL, 0x100000000ULL, 0x100000001ULL,
		0x7fffffffffffffffULL, 0x8000000000000000ULL, 0x8000000000000001ULL,
		0xfffffffffffffffeULL, 0xffffffffffffffffULL,
	};

	for (size_t i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
		check_n(d, magic, l, special[i]);
	}
	check_n(d, magic, l, d - 1U);
	check_n(d, magic, l, d);
	check_n(d, magic, l, d + 1U);
	check_n(d, magic, l, 2U * d - 1U);
	check_n(d, magic, l, 2U * d);
	check_near(d, magic, l, 0x100000000ULL);
	check_near(d, magic, l, 0x8000000000000000ULL);
	check_near(d, magic, l, 0xffffffffffffffffULL);

	uint64_t s = d * 0x2545f4914f6cdd1dULL;

	for (uint32_t i = 0; i < nvalues; i++) {
		uint64_t x = splitmix(&s);
		uint64_t n;

		switch (i & 3U) {
		case 0:
			n = x;
			break;
		case 1:
			n = x >> (splitmix(&s) & 63U);
			break;
		default: {
			uint64_t qmax = UINT64_MAX / d;
			uint64_t q = (splitmix(&s) >> (splitmix(&s) & 63U)) % (qmax + 1U);
			uint64_t sel = x & 3U;
			uint64_t r = sel == 0U ? 0U : sel == 1U ? 1U : sel == 2U ? d - 2U : d - 1U;
			u128 v = (u128)q * d + r;

			n = (v >> 64) != 0U ? x : (uint64_t)v;
			break;
		}
		}
		check_n(d, magic, l, n);
	}
}

static void *worker(void *arg)
{
	(void)arg;
	for (;;) {
		uint32_t d0 = atomic_fetch_add(&next_d, 1024U);

		if (d0 > max_d) {
			break;
		}
		for (uint32_t d = d0; d < d0 + 1024U && d <= max_d; d++) {
			check_divisor(d, values);
		}
	}
	return NULL;
}

#define CONST_DIVISOR(D)                                                                           \
	do {                                                                                       \
		_Static_assert(__builtin_constant_p(D) && Z_TC32_UDIV64_RANGE(D) &&                \
				       Z_TC32_UDIV64_VALID(D),                                     \
			       "multiply");                                                        \
		uint64_t s_ = (D);                                                                 \
		for (uint32_t i_ = 0; i_ < 200000U; i_++) {                                        \
			uint64_t n_ = splitmix(&s_) >> (i_ & 63U);                                 \
			if (z_tc32_udiv64(n_, D) != n_ / (D) ||                                    \
			    z_tc32_umod64(n_, D) != n_ % (D)) {                                    \
				fail("const", (D), n_, z_tc32_udiv64(n_, D), n_ / (D));            \
			}                                                                          \
			atomic_fetch_add_explicit(&n_checks, 1, memory_order_relaxed);             \
		}                                                                                  \
		ndispatch++;                                                                       \
	} while (0)

int main(int argc, char **argv)
{
	if (argc > 1) {
		max_d = (uint32_t)strtoul(argv[1], NULL, 0);
	}
	if (argc > 2) {
		values = (uint32_t)strtoul(argv[2], NULL, 0);
	}
	if (argc > 3) {
		threads = (uint32_t)strtoul(argv[3], NULL, 0);
	}

	/* 1. The multiply step against 128-bit arithmetic. */
	uint64_t s = 1;
	static const uint64_t edge[] = {0, 1, 2, 0xffffULL, 0x10000ULL, 0xffffffffULL,
					0x100000000ULL, 0xffff0000ffff0000ULL,
					0x0000ffff0000ffffULL, 0x8000000000000000ULL,
					0xfffffffffffffffeULL, 0xffffffffffffffffULL};
	unsigned long nmul = 0;

	for (size_t i = 0; i < sizeof(edge) / sizeof(edge[0]); i++) {
		for (size_t j = 0; j < sizeof(edge) / sizeof(edge[0]); j++) {
			uint64_t n = edge[i], m = edge[j];
			uint64_t want = (uint64_t)(((u128)n + (((u128)n * m) >> 64)) >> 1);

			if (z_tc32_udiv64_avg_mulhi(n, m) != want) {
				fail("mulhi", m, n, z_tc32_udiv64_avg_mulhi(n, m), want);
			}
			nmul++;
		}
	}
	for (uint32_t i = 0; i < 20000000U; i++) {
		uint64_t n = splitmix(&s), m = splitmix(&s);

		if (i & 1U) {
			n >>= splitmix(&s) & 63U;
		}
		if (i & 2U) {
			m >>= splitmix(&s) & 63U;
		}
		uint64_t want = (uint64_t)(((u128)n + (((u128)n * m) >> 64)) >> 1);

		if (z_tc32_udiv64_avg_mulhi(n, m) != want) {
			fail("mulhi", m, n, z_tc32_udiv64_avg_mulhi(n, m), want);
		}
		nmul++;
	}
	printf("multiply step: %lu (n, m) pairs\n", nmul);

	/* 2. Every divisor from 1 to max_d. */
	pthread_t th[64];

	threads = threads > 64U ? 64U : threads;
	for (uint32_t i = 0; i < threads; i++) {
		pthread_create(&th[i], NULL, worker, NULL);
	}
	for (uint32_t i = 0; i < threads; i++) {
		pthread_join(th[i], NULL);
	}
	unsigned long d_all = atomic_load(&n_divisors);
	unsigned long c_all = atomic_load(&n_checks);

	printf("divisors 1..%u: %lu in range, %lu quotient/remainder checks\n", max_d, d_all,
	       c_all);

	/* 3. Divisors up to 2^32 - 1. */
	uint64_t ds = 7;
	unsigned long nbig = 0;

	for (uint32_t i = 0; i < 200000U; i++) {
		uint64_t d = (splitmix(&ds) >> (32U + (splitmix(&ds) % 13U))) | 3U;

		check_divisor(d, 200U);
		nbig++;
	}
	for (unsigned int k = 2; k < 32; k++) {
		for (int o = -3; o <= 3; o++) {
			check_divisor((1ULL << k) + (uint64_t)(int64_t)o, 2000U);
			nbig++;
		}
	}
	for (uint64_t d = 0xffffffffULL; d > 0xffffffffULL - 1000U; d--) {
		check_divisor(d, 2000U);
		nbig++;
	}
	check_divisor(0x100000000ULL, 10U); /* out of range: no checks */
	printf("large divisors: %lu, checks so far %lu\n", nbig,
	       (unsigned long)atomic_load(&n_checks));

	/* 4. The dispatching macros with constant divisors. */
	unsigned int ndispatch = 0;

	CONST_DIVISOR(3U);
	CONST_DIVISOR(10U);
	CONST_DIVISOR(100U);
	CONST_DIVISOR(1000U);
	CONST_DIVISOR(1600U);
	CONST_DIVISOR(10000U);
	CONST_DIVISOR(16000U);
	CONST_DIVISOR(100000U);
	CONST_DIVISOR(1000000U);
	CONST_DIVISOR(16000000U);
	CONST_DIVISOR(32768U * 3U);
	CONST_DIVISOR(1000000000U);
	CONST_DIVISOR(0xffffffffU);
	printf("constant divisors through the macros: %u\n", ndispatch);

	unsigned long nf = atomic_load(&n_fail);

	printf("%s: %lu checks, %lu failures\n", nf ? "FAILED" : "PASSED",
	       (unsigned long)atomic_load(&n_checks), nf);
	return nf ? 1 : 0;
}
