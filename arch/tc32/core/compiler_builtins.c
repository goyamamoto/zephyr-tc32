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

static union u64_words u64_divmod_u64(union u64_words numerator, union u64_words denominator,
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

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor)
{
	union u64_words numerator = { .value = dividend };
	union u64_words denominator = { .value = divisor };

	return u64_divmod_u64(numerator, denominator, NULL).value;
}

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor)
{
	union u64_words numerator = { .value = dividend };
	union u64_words denominator = { .value = divisor };
	union u64_words remainder;

	(void)u64_divmod_u64(numerator, denominator, &remainder);

	return remainder.value;
}

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

	union u64_words quotient = u64_divmod_u64(numerator, denominator, NULL);

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

	(void)u64_divmod_u64(numerator, denominator, &remainder);

	if (negative) {
		u64_negate(&remainder);
	}

	return (int64_t)remainder.value;
}

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

int64_t __ashrdi3(int64_t value, int shift)
{
	union u64_words result = { .value = (uint64_t)value };
	unsigned int amount = (unsigned int)shift & 63U;
	uint32_t sign = (result.word.hi & BIT(31)) != 0U ? UINT32_MAX : 0U;

	if (amount >= 32U) {
		result.word.lo = (int32_t)result.word.hi >> (amount - 32U);
		result.word.hi = sign;
	} else if (amount != 0U) {
		result.word.lo = (result.word.lo >> amount) | (result.word.hi << (32U - amount));
		result.word.hi = (uint32_t)((int32_t)result.word.hi >> amount);
	}

	return (int64_t)result.value;
}

uint32_t __udivsi3(uint32_t a, uint32_t b)
{
	if (b == 0U) {
		return UINT32_MAX;
	}
	if (a < b) {
		return 0U;
	}

	uint32_t q = 0;
	union u64_words rem = { .value = 0 };
	union u64_words divisor = { .value = b };

	for (int i = 31; i >= 0; i--) {
		u64_shl1(&rem);
		rem.word.lo |= (a >> i) & 1U;
		if (u64_ge(rem, divisor)) {
			u64_sub(&rem, divisor);
			q |= (1U << i);
		}
	}

	return q;
}

uint32_t __umodsi3(uint32_t a, uint32_t b)
{
	if (b == 0U) {
		return a;
	}
	if (a < b) {
		return a;
	}

	union u64_words rem = { .value = 0 };
	union u64_words divisor = { .value = b };

	for (int i = 31; i >= 0; i--) {
		u64_shl1(&rem);
		rem.word.lo |= (a >> i) & 1U;
		if (u64_ge(rem, divisor)) {
			u64_sub(&rem, divisor);
		}
	}

	return rem.word.lo;
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

int32_t __modsi3(int32_t a, int32_t b)
{
	int neg = 0;
	uint32_t ua = (uint32_t)a;
	uint32_t ub = (uint32_t)b;

	if (a < 0) {
		neg = 1;
		ua = 0U - ua;
	}
	if (b < 0) {
		ub = 0U - ub;
	}
	if (ub == 0U) {
		return a;
	}

	uint32_t r = __umodsi3(ua, ub);

	return (int32_t)(neg ? (0U - r) : r);
}
