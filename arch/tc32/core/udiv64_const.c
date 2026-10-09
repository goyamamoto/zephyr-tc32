/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/toolchain.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/arch/tc32/udiv64_const.h>

/*
 * One copy of the multiply step of the constant-divisor division
 * (include/zephyr/arch/tc32/udiv64_const.h) for every call site: inline, each
 * site would carry it.
 */
uint64_t z_tc32_udiv64_avg_mulhi(uint64_t n, uint64_t m)
{
	return z_tc32_udiv64_avg_mulhi_inline(n, m);
}

/*
 * The divisor of a 64-bit division in z_tmcvt() converting from_hz into to_hz: the
 * ratio when it is an integer, from_hz in the general conversion. When the
 * rates make it a multiply, it is from_hz, checked all the same.
 */
#define Z_TC32_TMCVT_DIVISOR(from_hz, to_hz)                                                       \
	((((from_hz) > (to_hz)) && (((from_hz) % (to_hz)) == 0U)) ? ((from_hz) / (to_hz))          \
								    : (from_hz))

/*
 * Each conversion of time_units.h whose divisor is in the multiply's range
 * has an m' that meets the condition of the proof, so it takes the multiply.
 */
#define Z_TC32_TMCVT_DIV_ASSERT(from_hz, to_hz)                                                    \
	BUILD_ASSERT(!Z_TC32_UDIV64_RANGE(Z_TC32_TMCVT_DIVISOR(from_hz, to_hz)) ||                 \
		     Z_TC32_UDIV64_VALID(Z_TC32_TMCVT_DIVISOR(from_hz, to_hz)))

#if !defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME) &&                                       \
	!defined(CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE)
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_sec, Z_HZ_cyc);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ms, Z_HZ_cyc);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_us, Z_HZ_cyc);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ns, Z_HZ_cyc);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_cyc, Z_HZ_sec);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_cyc, Z_HZ_ms);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_cyc, Z_HZ_us);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_cyc, Z_HZ_ns);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_cyc, Z_HZ_ticks);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ticks, Z_HZ_cyc);
#endif
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_sec, Z_HZ_ticks);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ms, Z_HZ_ticks);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_us, Z_HZ_ticks);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ns, Z_HZ_ticks);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ticks, Z_HZ_sec);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ticks, Z_HZ_ms);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ticks, Z_HZ_us);
Z_TC32_TMCVT_DIV_ASSERT(Z_HZ_ticks, Z_HZ_ns);
