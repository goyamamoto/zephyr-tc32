/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/sys/util.h>

union u64_words {
	uint64_t value;
	struct {
		uint32_t lo;
		uint32_t hi;
	} word;
};

static void u64_shl1(union u64_words *value)
{
	value->word.hi = (value->word.hi << 1) | (value->word.lo >> 31);
	value->word.lo <<= 1;
}

static void u64_set_bit(union u64_words *value, unsigned int bit)
{
	if (bit >= 32U) {
		value->word.hi |= BIT(bit - 32U);
	} else {
		value->word.lo |= BIT(bit);
	}
}

static uint32_t u64_get_bit(union u64_words value, unsigned int bit)
{
	if (bit >= 32U) {
		return (value.word.hi >> (bit - 32U)) & 1U;
	}

	return (value.word.lo >> bit) & 1U;
}

static int u64_ge(union u64_words lhs, union u64_words rhs)
{
	if (lhs.word.hi != rhs.word.hi) {
		return lhs.word.hi > rhs.word.hi;
	}

	return lhs.word.lo >= rhs.word.lo;
}

static void u64_sub(union u64_words *lhs, union u64_words rhs)
{
	uint32_t borrow = lhs->word.lo < rhs.word.lo;

	lhs->word.lo -= rhs.word.lo;
	lhs->word.hi = lhs->word.hi - rhs.word.hi - borrow;
}

static void u64_negate(union u64_words *value)
{
	value->word.lo = ~value->word.lo + 1U;
	value->word.hi = ~value->word.hi + (value->word.lo == 0U ? 1U : 0U);
}

#if defined(CONFIG_TC32_FAST_DIV64)
uint32_t __udivsi3(uint32_t a, uint32_t b);

/* The number of leading zero bits of a nonzero value, by halving. */
static uint32_t u32_clz(uint32_t value)
{
	uint32_t n = 0U;

	if ((value >> 16) == 0U) {
		n = 16U;
		value <<= 16;
	}
	if ((value >> 24) == 0U) {
		n += 8U;
		value <<= 8;
	}
	if ((value >> 28) == 0U) {
		n += 4U;
		value <<= 4;
	}
	if ((value >> 30) == 0U) {
		n += 2U;
		value <<= 2;
	}
	if ((value >> 31) == 0U) {
		n += 1U;
	}

	return n;
}

/*
 * (hi:lo) / v for hi < v, so that the quotient fits in 32 bits: long division
 * in 16-bit digits (Hacker's Delight, divlu). The divisor is shifted until
 * its top bit is set and split into two digits; each quotient digit is
 * estimated by one 32/32 division by the divisor's high digit, which is at
 * most 2 too large, and corrected against the low digit. Every product fits
 * in 32 bits. The remainder goes to *rem.
 */
static uint32_t u64_div_u32(uint32_t hi, uint32_t lo, uint32_t v, uint32_t *rem)
{
	uint32_t s = u32_clz(v);
	uint32_t vn1, vn0, un32, un10, un1, un0, un21, q1, q0, rhat;

	v <<= s;
	vn1 = v >> 16;
	vn0 = v & 0xffffU;
	un32 = (s == 0U) ? hi : ((hi << s) | (lo >> (32U - s)));
	un10 = lo << s;
	un1 = un10 >> 16;
	un0 = un10 & 0xffffU;

	q1 = __udivsi3(un32, vn1);
	rhat = un32 - q1 * vn1;
	while (((q1 >> 16) != 0U) || (q1 * vn0 > ((rhat << 16) | un1))) {
		q1--;
		rhat += vn1;
		if ((rhat >> 16) != 0U) {
			break;
		}
	}
	/* The partial remainder (un32:un1) - q1 * v, which is below v. */
	un21 = (un32 << 16) + un1 - q1 * v;

	q0 = __udivsi3(un21, vn1);
	rhat = un21 - q0 * vn1;
	while (((q0 >> 16) != 0U) || (q0 * vn0 > ((rhat << 16) | un0))) {
		q0--;
		rhat += vn1;
		if ((rhat >> 16) != 0U) {
			break;
		}
	}
	*rem = ((un21 << 16) + un0 - q0 * v) >> s;

	return (q1 << 16) | q0;
}

/*
 * 64-bit unsigned division, the quotient returned and the remainder in *rem.
 * A divisor of 0 gives the quotient UINT64_MAX and the dividend as the
 * remainder.
 * - Both below 2^32: one 32-bit division (__udivsi3, which uses the
 *   TLSR8278's divider when built for it).
 * - Divisor below 2^32: the high word divided first, then the low word with
 *   the remainder in front of it: in two 16-bit digits for a divisor below
 *   2^16 (each step a 32/32 division), otherwise by u64_div_u32().
 * - Divisor from 2^32: the quotient has at most 32 bits. The divisor is
 *   shifted so that its top bit lines up with the dividend's, then
 *   shift-and-subtract over the quotient's bits only, in 32-bit halves.
 */
static uint64_t u64_divmod(uint64_t dividend, uint64_t divisor, uint64_t *rem)
{
	uint32_t nh = (uint32_t)(dividend >> 32);
	uint32_t nl = (uint32_t)dividend;
	uint32_t dh = (uint32_t)(divisor >> 32);
	uint32_t dl = (uint32_t)divisor;
	uint32_t qh, ql, r;

	if (dh == 0U) {
		if (dl == 0U) {
			*rem = dividend;
			return UINT64_MAX;
		}
		if (nh == 0U) {
			ql = __udivsi3(nl, dl);
			*rem = nl - ql * dl;
			return ql;
		}
		qh = __udivsi3(nh, dl);
		r = nh - qh * dl;
		if ((dl >> 16) == 0U) {
			uint32_t part = (r << 16) | (nl >> 16);
			uint32_t q1 = __udivsi3(part, dl);

			part = ((part - q1 * dl) << 16) | (nl & 0xffffU);
			ql = __udivsi3(part, dl);
			r = part - ql * dl;
			ql |= q1 << 16;
		} else {
			ql = u64_div_u32(r, nl, dl, &r);
		}
		*rem = r;
		return ((uint64_t)qh << 32) | ql;
	}

	if ((nh < dh) || ((nh == dh) && (nl < dl))) {
		*rem = dividend;
		return 0U;
	}

	uint32_t s = u32_clz(dh) - u32_clz(nh);

	if (s != 0U) {
		dh = (dh << s) | (dl >> (32U - s));
		dl <<= s;
	}
	ql = 0U;
	for (;;) {
		ql <<= 1;
		if ((nh > dh) || ((nh == dh) && (nl >= dl))) {
			nh = nh - dh - (nl < dl ? 1U : 0U);
			nl -= dl;
			ql |= 1U;
		}
		if (s == 0U) {
			break;
		}
		s--;
		dl = (dl >> 1) | (dh << 31);
		dh >>= 1;
	}
	*rem = ((uint64_t)nh << 32) | nl;

	return ql;
}
#endif /* CONFIG_TC32_FAST_DIV64 */

/*
 * Bit-serial restoring division: one quotient bit per step, over all 64 bits
 * of the dividend. A divisor of 0 gives the quotient UINT64_MAX and the
 * dividend as the remainder. With CONFIG_TC32_FAST_DIV64, only
 * __tc32_udivmoddi4_proven() and __tc32_uldivmod_proven() call it: code that
 * must run this division instruction for instruction calls those by name.
 * Without it, every 64-bit division of this file comes here. External and not
 * inlined, so that its instructions do not depend on what its callers pass.
 */
union u64_words __tc32_u64_divmod_u64_proven(union u64_words numerator,
					     union u64_words denominator,
					     union u64_words *remainder_out);

union u64_words __attribute__((noinline))
__tc32_u64_divmod_u64_proven(union u64_words numerator, union u64_words denominator,
			     union u64_words *remainder_out)
{
	union u64_words quotient = { .value = 0 };
	union u64_words remainder = { .value = 0 };

	if ((denominator.word.lo == 0U) && (denominator.word.hi == 0U)) {
		quotient.word.lo = UINT32_MAX;
		quotient.word.hi = UINT32_MAX;
		remainder = numerator;
	} else {
		for (int bit = 63; bit >= 0; bit--) {
			u64_shl1(&remainder);
			remainder.word.lo |= u64_get_bit(numerator, bit);

			if (u64_ge(remainder, denominator)) {
				u64_sub(&remainder, denominator);
				u64_set_bit(&quotient, bit);
			}
		}
	}

	if (remainder_out != NULL) {
		*remainder_out = remainder;
	}

	return quotient;
}

static int __attribute__((noinline)) u32_nonzero(uint32_t value)
{
	return value != 0U;
}

static int __attribute__((noinline)) u32_bit0_set(uint32_t value)
{
	return (value & 1U) != 0U;
}

int __clzsi2(uint32_t value)
{
	if (value == 0U) {
		return 32;
	}

	int count = 0;

	for (uint32_t bit = BIT(31); (value & bit) == 0U; bit >>= 1) {
		count++;
	}

	return count;
}

uint64_t __muldi3(uint64_t a, uint64_t b)
{
	union u64_words multiplicand = { .value = a };
	union u64_words multiplier = { .value = b };
	union u64_words result = { .value = 0 };

	while (u32_nonzero(multiplier.word.lo) || u32_nonzero(multiplier.word.hi)) {
		if (u32_bit0_set(multiplier.word.lo)) {
			uint32_t carry = UINT32_MAX - result.word.lo < multiplicand.word.lo;

			result.word.lo += multiplicand.word.lo;
			result.word.hi += multiplicand.word.hi + carry;
		}

		multiplier.word.lo = (multiplier.word.lo >> 1) | (multiplier.word.hi << 31);
		multiplier.word.hi >>= 1;
		u64_shl1(&multiplicand);
	}

	return result.value;
}

#if defined(CONFIG_TC32_FAST_DIV64)
int64_t __divdi3(int64_t dividend, int64_t divisor)
{
	union u64_words numerator = { .value = (uint64_t)dividend };
	union u64_words denominator = { .value = (uint64_t)divisor };
	bool negative = false;

	if ((numerator.word.hi & BIT(31)) != 0U) {
		negative = !negative;
		u64_negate(&numerator);
	}
	if ((denominator.word.hi & BIT(31)) != 0U) {
		negative = !negative;
		u64_negate(&denominator);
	}

	uint64_t remainder;
	union u64_words quotient = {
		.value = u64_divmod(numerator.value, denominator.value, &remainder),
	};

	if (negative) {
		u64_negate(&quotient);
	}

	return (int64_t)quotient.value;
}

int64_t __moddi3(int64_t dividend, int64_t divisor)
{
	union u64_words numerator = { .value = (uint64_t)dividend };
	union u64_words denominator = { .value = (uint64_t)divisor };
	bool negative = false;
	union u64_words remainder;

	if ((numerator.word.hi & BIT(31)) != 0U) {
		negative = true;
		u64_negate(&numerator);
	}
	if ((denominator.word.hi & BIT(31)) != 0U) {
		u64_negate(&denominator);
	}

	(void)u64_divmod(numerator.value, denominator.value, &remainder.value);

	if (negative) {
		u64_negate(&remainder);
	}

	return (int64_t)remainder.value;
}
#else
int64_t __divdi3(int64_t dividend, int64_t divisor)
{
	union u64_words numerator = { .value = (uint64_t)dividend };
	union u64_words denominator = { .value = (uint64_t)divisor };
	bool negative = false;

	if ((numerator.word.hi & BIT(31)) != 0U) {
		negative = !negative;
		u64_negate(&numerator);
	}
	if ((denominator.word.hi & BIT(31)) != 0U) {
		negative = !negative;
		u64_negate(&denominator);
	}

	union u64_words quotient = __tc32_u64_divmod_u64_proven(numerator, denominator, NULL);

	if (negative) {
		u64_negate(&quotient);
	}

	return (int64_t)quotient.value;
}

int64_t __moddi3(int64_t dividend, int64_t divisor)
{
	union u64_words numerator = { .value = (uint64_t)dividend };
	union u64_words denominator = { .value = (uint64_t)divisor };
	bool negative = false;
	union u64_words remainder;

	if ((numerator.word.hi & BIT(31)) != 0U) {
		negative = true;
		u64_negate(&numerator);
	}
	if ((denominator.word.hi & BIT(31)) != 0U) {
		u64_negate(&denominator);
	}

	(void)__tc32_u64_divmod_u64_proven(numerator, denominator, &remainder);

	if (negative) {
		u64_negate(&remainder);
	}

	return (int64_t)remainder.value;
}
#endif /* CONFIG_TC32_FAST_DIV64 */

uint64_t __ashldi3(uint64_t value, int shift)
{
	union u64_words result = { .value = value };
	unsigned int amount = (unsigned int)shift & 63U;

	if (amount >= 32U) {
		result.word.hi = result.word.lo << (amount - 32U);
		result.word.lo = 0U;
	} else if (amount != 0U) {
		result.word.hi = (result.word.hi << amount) | (result.word.lo >> (32U - amount));
		result.word.lo <<= amount;
	}

	return result.value;
}

uint64_t __lshrdi3(uint64_t value, int shift)
{
	union u64_words result = { .value = value };
	unsigned int amount = (unsigned int)shift & 63U;

	if (amount >= 32U) {
		result.word.lo = result.word.hi >> (amount - 32U);
		result.word.hi = 0U;
	} else if (amount != 0U) {
		result.word.lo = (result.word.lo >> amount) | (result.word.hi << (32U - amount));
		result.word.hi >>= amount;
	}

	return result.value;
}

/*
 * The TLSR8278's divider (0x660-0x66b, not documented): 0x664 takes the
 * dividend, 0x668 the divisor, writing the mode to 0x660 starts it, and 0x660
 * reads 0 when it is done, with the quotient in 0x664 and the remainder in
 * 0x668. Interrupts are off meanwhile, so that a division in an interrupt
 * handler cannot overwrite one in progress. Only the unsigned
 * modes are used; the signed helpers below handle the signs. Division by zero
 * stays in software: what the divider does then is not known.
 */
#define TLSR8278_IRQ_ENABLE (*(volatile uint8_t *)0x00800643u)
#define TLSR8278_DIV_MODE   (*(volatile uint8_t *)0x00800660u)
#define TLSR8278_DIV_A      (*(volatile uint32_t *)0x00800664u)
#define TLSR8278_DIV_B      (*(volatile uint32_t *)0x00800668u)
#define TLSR8278_DIV_UDIV   0u
#define TLSR8278_DIV_UMOD   2u

__attribute__((noinline, section(".ram_code")))
static uint32_t tlsr8278_div(uint32_t a, uint32_t b, uint8_t mode)
{
	uint8_t key = TLSR8278_IRQ_ENABLE;
	uint32_t r;

	TLSR8278_IRQ_ENABLE = 0u;
	TLSR8278_DIV_A = a;
	TLSR8278_DIV_B = b;
	TLSR8278_DIV_MODE = mode;
	while (TLSR8278_DIV_MODE != 0u) {
	}
	r = (mode == TLSR8278_DIV_UMOD) ? TLSR8278_DIV_B : TLSR8278_DIV_A;
	TLSR8278_IRQ_ENABLE = key;

	return r;
}

uint32_t __udivsi3(uint32_t a, uint32_t b)
{
	if (b == 0U) {
		return UINT32_MAX;
	}
	if (a < b) {
		return 0U;
	}
	return tlsr8278_div(a, b, TLSR8278_DIV_UDIV);
}

int32_t __divsi3(int32_t a, int32_t b)
{
	int neg = 0;
	uint32_t ua = (uint32_t)a;
	uint32_t ub = (uint32_t)b;

	if (a < 0) {
		neg = !neg;
		ua = 0U - ua;
	}
	if (b < 0) {
		neg = !neg;
		ub = 0U - ub;
	}

	if (ub == 0U) {
		return INT32_MAX;
	}

	uint32_t q = __udivsi3(ua, ub);

	return (int32_t)(neg ? (0U - q) : q);
}

/*
 * ARM EABI names, for a mainstream compiler building ARMv4T Thumb that
 * thumb2tc32.py re-encodes (TC32_THUMB): it calls these instead of the GCC
 * names above. The 64-bit quotient-and-remainder helpers, which return in
 * r0-r3, are in aeabi_thumb.S.
 */
#include <stddef.h>
#include <string.h>

uint32_t __aeabi_uidiv(uint32_t a, uint32_t b) __attribute__((alias("__udivsi3")));
int32_t __aeabi_idiv(int32_t a, int32_t b) __attribute__((alias("__divsi3")));
uint64_t __aeabi_lmul(uint64_t a, uint64_t b) __attribute__((alias("__muldi3")));
uint64_t __aeabi_llsl(uint64_t value, int shift) __attribute__((alias("__ashldi3")));
uint64_t __aeabi_llsr(uint64_t value, int shift) __attribute__((alias("__lshrdi3")));

/* The quotient in r0 and the remainder in r1: a 64-bit result is r0:r1. */
uint64_t __aeabi_uidivmod(uint32_t a, uint32_t b)
{
	uint32_t q = __udivsi3(a, b);

	return ((uint64_t)(a - q * b) << 32) | q;
}

uint64_t __aeabi_idivmod(int32_t a, int32_t b)
{
	uint32_t q = (uint32_t)__divsi3(a, b);

	return ((uint64_t)((uint32_t)a - q * (uint32_t)b) << 32) | q;
}

#if defined(CONFIG_TC32_FAST_DIV64)
uint64_t __tc32_udivmoddi4(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{
	return u64_divmod(dividend, divisor, remainder);
}

/*
 * __divdi3() and __moddi3() in one division: the quotient negative when the
 * signs differ, the remainder with the dividend's sign.
 */
int64_t __tc32_divmoddi4(int64_t dividend, int64_t divisor, int64_t *remainder)
{
	union u64_words numerator = { .value = (uint64_t)dividend };
	union u64_words denominator = { .value = (uint64_t)divisor };
	union u64_words quotient;
	union u64_words rem;
	bool quotient_negative = false;
	bool remainder_negative = false;

	if ((numerator.word.hi & BIT(31)) != 0U) {
		quotient_negative = true;
		remainder_negative = true;
		u64_negate(&numerator);
	}
	if ((denominator.word.hi & BIT(31)) != 0U) {
		quotient_negative = !quotient_negative;
		u64_negate(&denominator);
	}

	quotient.value = u64_divmod(numerator.value, denominator.value, &rem.value);

	if (quotient_negative) {
		u64_negate(&quotient);
	}
	if (remainder_negative) {
		u64_negate(&rem);
	}

	*remainder = (int64_t)rem.value;
	return (int64_t)quotient.value;
}
#else
int64_t __tc32_divmoddi4(int64_t dividend, int64_t divisor, int64_t *remainder)
{
	*remainder = __moddi3(dividend, divisor);
	return __divdi3(dividend, divisor);
}
#endif /* CONFIG_TC32_FAST_DIV64 */

/*
 * The same as __tc32_udivmoddi4(), through __tc32_u64_divmod_u64_proven(); with
 * __tc32_uldivmod_proven() in aeabi_thumb.S in front of it. Without
 * CONFIG_TC32_FAST_DIV64 this is __tc32_udivmoddi4() itself, under both names.
 */
uint64_t __tc32_udivmoddi4_proven(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{
	union u64_words numerator = { .value = dividend };
	union u64_words denominator = { .value = divisor };
	union u64_words rem;
	uint64_t q = __tc32_u64_divmod_u64_proven(numerator, denominator, &rem).value;

	*remainder = rem.value;
	return q;
}
#if !defined(CONFIG_TC32_FAST_DIV64)
uint64_t __tc32_udivmoddi4(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
	__attribute__((alias("__tc32_udivmoddi4_proven")));
#endif

void __aeabi_memcpy(void *d, const void *s, size_t n)
{
	memcpy(d, s, n);
}
void __aeabi_memcpy4(void *d, const void *s, size_t n) __attribute__((alias("__aeabi_memcpy")));
void __aeabi_memcpy8(void *d, const void *s, size_t n) __attribute__((alias("__aeabi_memcpy")));



void __aeabi_memclr(void *d, size_t n)
{
	memset(d, 0, n);
}
void __aeabi_memclr4(void *d, size_t n) __attribute__((alias("__aeabi_memclr")));
void __aeabi_memclr8(void *d, size_t n) __attribute__((alias("__aeabi_memclr")));
