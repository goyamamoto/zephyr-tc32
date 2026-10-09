/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * P-256 on the TLSR8278's public key engine (DS-TLSR8278 16; tc32_p256.h).
 *
 * The engine runs a microcode routine, named by MC_PTR, on operands in its
 * two RAMs, A and B; with 256-bit operands (CONF: base radix 2, partial
 * radix 8 words) a slot is 9 words, every 0x24 bytes, and the ten slots of
 * each RAM are A0-A9 and B0-B9 (Table 16-1). An operand fills the 8 words
 * of its number, least significant word first, and a ninth word of 0.
 * Which slot takes which operand is not in the datasheet; the routines used
 * here take:
 * - point multiplication (MC_PTR 0x10): the modulus p in B3, R^2 mod p (R =
 *   2^256) in A3 and -p^-1 mod 2^32 in B4's first word, the point's x in B0
 *   and y in B1, the curve's a in A5 and the scalar in A4; the result's x
 *   in A0 and y in A1;
 * - the check that a point is on the curve (0x0c): the same, with the
 *   curve's b in A4 instead of the scalar; STOP_LOG 3 when it is not.
 * EXE_CONF 0x15 takes and gives affine, non-Montgomery coordinates. Go
 * (CTRL bit 0) starts the routine; STAT bit 0 (Done) reads 1 when it has
 * ended and RT_CODE's STOP_LOG says how (0: the result is there, 1: stopped
 * by CTRL bit 16, 2: no modular inverse, the point at infinity, 3: not on
 * the curve, 4: no such microcode). Done is cleared before a start by a
 * write to STAT with bit 0 as 0 (Table 16-2 names a write of 1; the engine
 * clears it on either). RST1 bit 7 holds the engine in reset and CLKEN1
 * bit 7 gives it its clock: on for an operation, off after it, once the
 * operands and the result have been cleared from the RAM (the scalar is a
 * private key). An operation given up (tc32_p256_cancel(), or the blocking
 * calls' time limit) is stopped by CTRL bit 16 and ends as one that
 * finished: Done cleared, the RAM cleared, the clock off. An engine that
 * does not report the stop within PKE_STOP_US is also put in reset, which
 * the next start takes it out of.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/crypto/tc32_p256.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define PKE_NODE DT_INST(0, telink_b87_pke)
#define REGS     DT_REG_ADDR_BY_IDX(PKE_NODE, 0)
#define RAM_A    DT_REG_ADDR_BY_IDX(PKE_NODE, 1)
#define RAM_B    DT_REG_ADDR_BY_IDX(PKE_NODE, 2)

#define CTRL     (REGS + 0x00U)
#define CONF     (REGS + 0x04U)
#define MC_PTR   (REGS + 0x10U)
#define STAT     (REGS + 0x20U)
#define RT_CODE  (REGS + 0x24U)
#define EXE_CONF (REGS + 0x50U)

#define GO          BIT(0)
#define STOP        BIT(16)
#define DONE        BIT(0)
#define CONF_RADIX  ((2U << 24) | (8U << 16)) /* base radix 256 bits, 8 words of it */
#define CONF_MASK   0xf800ffffU
#define EXE_AFFINE  0x15U
#define STOP_LOG    0x0fU
#define RT_NO_INV   2U
#define RT_OFF      3U

#define MC_PMUL 0x10U
#define MC_PVER 0x0cU

#define SLOT       0x24U
#define SLOT_WORDS 9U
#define A(i)       (RAM_A + (i) * SLOT)
#define B(i)       (RAM_B + (i) * SLOT)

#define RST1   0x00800061U
#define CLKEN1 0x00800064U
#define PKE_EN BIT(7)

/* P-256 (FIPS 186-4 D.1.2.3), least significant word first: p, R^2 mod p,
 * a = p - 3, b, the base point and the order n.
 */
static const uint32_t p256_p[8] = {0xffffffffU, 0xffffffffU, 0xffffffffU, 0x00000000U,
				   0x00000000U, 0x00000000U, 0x00000001U, 0xffffffffU};
static const uint32_t p256_r2[8] = {0x00000003U, 0x00000000U, 0xffffffffU, 0xfffffffbU,
				    0xfffffffeU, 0xffffffffU, 0xfffffffdU, 0x00000004U};
static const uint32_t p256_b[8] = {0x27d2604bU, 0x3bce3c3eU, 0xcc53b0f6U, 0x651d06b0U,
				   0x769886bcU, 0xb3ebbd55U, 0xaa3a93e7U, 0x5ac635d8U};
static const uint32_t p256_gx[8] = {0xd898c296U, 0xf4a13945U, 0x2deb33a0U, 0x77037d81U,
				    0x63a440f2U, 0xf8bce6e5U, 0xe12c4247U, 0x6b17d1f2U};
static const uint32_t p256_gy[8] = {0x37bf51f5U, 0xcbb64068U, 0x6b315eceU, 0x2bce3357U,
				    0x7c0f9e16U, 0x8ee7eb4aU, 0xfe1a7f9bU, 0x4fe342e2U};
static const uint32_t p256_n[8] = {0xfc632551U, 0xf3b9cac2U, 0xa7179e84U, 0xbce6faadU,
				   0xffffffffU, 0xffffffffU, 0x00000000U, 0xffffffffU};
/* -p^-1 mod 2^32: p is -1 mod 2^32. */
#define P256_N1 1U

static uint8_t running; /* the microcode under way, or 0 */

static void load_words(uint32_t slot, const uint32_t w[8])
{
	for (uint32_t i = 0; i < 8U; i++) {
		sys_write32(w[i], slot + 4U * i);
	}
	sys_write32(0U, slot + 32U);
}

static void load(uint32_t slot, const uint8_t le[32])
{
	for (uint32_t i = 0; i < 8U; i++) {
		sys_write32(sys_get_le32(&le[4U * i]), slot + 4U * i);
	}
	sys_write32(0U, slot + 32U);
}

static void read_out(uint32_t slot, uint8_t le[32])
{
	for (uint32_t i = 0; i < 8U; i++) {
		sys_put_le32(sys_read32(slot + 4U * i), &le[4U * i]);
	}
}

static void clear(uint32_t slot)
{
	for (uint32_t i = 0; i < SLOT_WORDS; i++) {
		sys_write32(0U, slot + 4U * i);
	}
}

bool tc32_p256_scalar_ok(const uint8_t k[32])
{
	/* 1 <= k < n: compare from the most significant word down. */
	for (int i = 7; i >= 0; i--) {
		uint32_t w = sys_get_le32(&k[4 * i]);

		if (w < p256_n[i]) {
			for (int j = 0; j < 8; j++) {
				if (k[j] != 0U) {
					return true;
				}
			}
			return false; /* 0 */
		}
		if (w > p256_n[i]) {
			return false;
		}
	}
	return false; /* n */
}

static int start(uint8_t mc, const uint8_t *px, const uint8_t *py, const uint8_t *k)
{
	if (running != 0U) {
		return -EBUSY;
	}
	sys_write8(sys_read8(RST1) & (uint8_t)~PKE_EN, RST1);
	sys_write8(sys_read8(CLKEN1) | PKE_EN, CLKEN1);
	sys_write32((sys_read32(CONF) & CONF_MASK) | CONF_RADIX, CONF);
	sys_write32(EXE_AFFINE, EXE_CONF);
	load_words(B(3), p256_p);
	load_words(A(3), p256_r2);
	clear(B(4));
	sys_write32(P256_N1, B(4));
	load_words(A(5), p256_p); /* a = p - 3: the low word of p (0xffffffff) less 3 */
	sys_write32(0xfffffffcU, A(5));
	if (px != NULL) {
		load(B(0), px);
		load(B(1), py);
	} else {
		load_words(B(0), p256_gx);
		load_words(B(1), p256_gy);
	}
	if (k != NULL) {
		load(A(4), k);
	} else {
		load_words(A(4), p256_b);
	}
	sys_write32(sys_read32(STAT) & ~DONE, STAT);
	sys_write32(mc, MC_PTR);
	sys_write32(sys_read32(CTRL) | GO, CTRL);
	running = mc;
	return 0;
}

int tc32_p256_mul_start(const uint8_t k[32], const uint8_t *px, const uint8_t *py)
{
	if (!tc32_p256_scalar_ok(k)) {
		return -EINVAL; /* the engine never finishes a multiplication by 0 */
	}
	return start(MC_PMUL, px, py, k);
}

int tc32_p256_verify_start(const uint8_t px[32], const uint8_t py[32])
{
	return start(MC_PVER, px, py, NULL);
}

/* An operation is over: its operands and result cleared from the RAM, the clock off. */
static void end(void)
{
	clear(A(0));
	clear(A(1));
	clear(A(4));
	clear(B(0));
	clear(B(1));
	sys_write8(sys_read8(CLKEN1) & (uint8_t)~PKE_EN, CLKEN1);
	running = 0U;
}

int tc32_p256_poll(uint8_t *qx, uint8_t *qy)
{
	uint32_t rt;
	int err;

	if (running == 0U) {
		return -ENOENT;
	}
	if ((sys_read32(STAT) & DONE) == 0U) {
		return -EAGAIN;
	}
	rt = sys_read32(RT_CODE) & STOP_LOG;
	if (rt == 0U) {
		err = 0;
		if (running == MC_PMUL) {
			if (qx != NULL) {
				read_out(A(0), qx);
			}
			if (qy != NULL) {
				read_out(A(1), qy);
			}
		}
	} else if (rt == RT_NO_INV || rt == RT_OFF) {
		err = -EINVAL;
	} else {
		err = -EIO;
	}
	end();
	return err;
}

/* The longest wait for Done after a Stop. */
#define PKE_STOP_US 1000U

int tc32_p256_cancel(void)
{
	uint32_t t0, limit;
	int err = 0;

	if (running == 0U) {
		return -ENOENT;
	}
	sys_write32(STOP, CTRL);
	t0 = k_cycle_get_32();
	limit = PKE_STOP_US * (sys_clock_hw_cycles_per_sec() / 1000000U);
	while ((sys_read32(STAT) & DONE) == 0U) {
		if (k_cycle_get_32() - t0 > limit) {
			err = -ETIMEDOUT;
			break;
		}
	}
	sys_write32(sys_read32(STAT) & ~DONE, STAT);
	if (err != 0) {
		sys_write8(sys_read8(RST1) | PKE_EN, RST1);
	}
	end();
	return err;
}

/*
 * Polled without a pause for the first PKE_SPIN_US, then once a millisecond
 * so that lower-priority threads (a watchdog feeder among them) run while a
 * slow or stuck operation is waited for.
 */
#define PKE_SPIN_US 100000U

static int wait(uint8_t *qx, uint8_t *qy)
{
	uint32_t t0 = k_cycle_get_32();
	uint32_t per_ms = sys_clock_hw_cycles_per_sec() / 1000U;
	int err;

	while ((err = tc32_p256_poll(qx, qy)) == -EAGAIN) {
		uint32_t waited = k_cycle_get_32() - t0;

		if (waited > (uint32_t)CONFIG_SOC_TLSR8278_PKE_TIMEOUT_MS * per_ms) {
			(void)tc32_p256_cancel();
			return -ETIMEDOUT;
		}
		if (waited > PKE_SPIN_US / 1000U * per_ms) {
			k_sleep(K_MSEC(1));
		}
	}
	return err;
}

int tc32_p256_mul(const uint8_t k[32], const uint8_t *px, const uint8_t *py, uint8_t qx[32],
		  uint8_t qy[32])
{
	int err = tc32_p256_mul_start(k, px, py);

	return err != 0 ? err : wait(qx, qy);
}

int tc32_p256_verify(const uint8_t px[32], const uint8_t py[32])
{
	int err = tc32_p256_verify_start(px, py);

	return err != 0 ? err : wait(NULL, NULL);
}
