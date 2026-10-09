/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * P-256 (tc32_p256.h) on the public key engine or in software (p256-m), in
 * TC32-devtools' emulator. The known answers: the Bluetooth Core specification's debug key pair (Vol 3 Part H
 * 2.3.5.6.1) and the keys and DHKey of a Secure Connections pairing
 * recorded in Apache NimBLE's host tests (nimble_sc_vectors.py re-derives
 * them). With the engine, the emulator's register 0xffec sets its time per
 * operation; it does not exist on the chip.
 */

#include <string.h>

#include <zephyr/crypto/tc32_p256.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#define EMU_PKE_US 0x0080ffecU
#define CLKEN1     0x00800064U
#define RAM_A      0x00802400U
#define RAM_B      0x00803000U
#define SLOT       0x24U

/* 32 little-endian octets each. */
static const uint8_t da[32] = {0xbd, 0x1a, 0x3c, 0xcd, 0xa6, 0xb8, 0x99, 0x58, 0x99, 0xb7, 0x40,
			       0xeb, 0x7b, 0x60, 0xff, 0x4a, 0x50, 0x3f, 0x10, 0xd2, 0xe3, 0xb3,
			       0xc9, 0x74, 0x38, 0x5f, 0xc5, 0xa3, 0xd4, 0xf6, 0x49, 0x3f};
static const uint8_t ax[32] = {0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc, 0xdb, 0xfd, 0xf4,
			       0xac, 0x11, 0x91, 0xf4, 0xef, 0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83,
			       0x2c, 0x5e, 0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20};
static const uint8_t ay[32] = {0x8b, 0xd2, 0x89, 0x15, 0xd0, 0x8e, 0x1c, 0x74, 0x24, 0x30, 0xed,
			       0x8f, 0xc2, 0x45, 0x63, 0x76, 0x5c, 0x15, 0x52, 0x5a, 0xbf, 0x9a,
			       0x32, 0x63, 0x6d, 0xeb, 0x2a, 0x65, 0x49, 0x9c, 0x80, 0xdc};
/*
 * The responder's key pair and the initiator's public key of the Secure
 * Connections pairing Apache NimBLE's host tests replay
 * (ble_sm_sc_peer_jw_iio3_rio3_b1_iat0_rat0_ik5_rk7), and their DHKey: the
 * value f5 turns into the LTK that pairing recorded.
 */
static const uint8_t db[32] = {
	0x54, 0x8d, 0x20, 0xb8, 0x97, 0x0b, 0xbc, 0x43,
	0x9a, 0xad, 0x10, 0x6f, 0x60, 0x74, 0xd4, 0x6a,
	0x55, 0xc1, 0x7a, 0x17, 0x8b, 0x60, 0xe0, 0xb4,
	0x5a, 0xe6, 0x58, 0xf1, 0xea, 0x12, 0xd9, 0xfb,
};
static const uint8_t bx[32] = {
	0x72, 0x8c, 0xd1, 0x88, 0xd7, 0xbe, 0x49, 0xb2,
	0xc5, 0x5c, 0x95, 0xb3, 0x64, 0xe0, 0x12, 0x32,
	0xb6, 0xc9, 0x47, 0x63, 0x37, 0x38, 0x5b, 0x9c,
	0x1e, 0x1b, 0x1a, 0x06, 0x09, 0xe2, 0x31, 0x85,
};
static const uint8_t by[32] = {
	0x19, 0x3a, 0x29, 0x69, 0x62, 0xd6, 0x30, 0xe7,
	0xe8, 0x48, 0x63, 0xdc, 0x00, 0x73, 0x0a, 0x70,
	0x7d, 0x2e, 0x29, 0xcc, 0x91, 0x77, 0x71, 0xb1,
	0x75, 0xb8, 0xf7, 0xdc, 0xb0, 0xe2, 0x91, 0x10,
};
static const uint8_t px[32] = {
	0xbc, 0xf2, 0xd8, 0xa5, 0xdb, 0xa3, 0x95, 0x6c,
	0x99, 0xf9, 0x11, 0x0d, 0x4d, 0x2e, 0xf0, 0xbd,
	0xee, 0x9b, 0x69, 0xb6, 0xcd, 0x88, 0x74, 0xbe,
	0x40, 0xe8, 0xe5, 0xcc, 0xdc, 0x88, 0x44, 0x53,
};
static const uint8_t py[32] = {
	0xbf, 0xa9, 0x82, 0x0e, 0x18, 0x7a, 0x14, 0xf8,
	0x77, 0xfd, 0x8e, 0x92, 0x2a, 0xf8, 0x5d, 0x39,
	0xd1, 0x6d, 0x92, 0x1f, 0x38, 0x74, 0x99, 0xdc,
	0x6c, 0x2c, 0x94, 0x23, 0xf9, 0x72, 0x56, 0xab,
};
static const uint8_t dhkey[32] = {
	0x75, 0xf5, 0x9e, 0xfd, 0x28, 0x38, 0xb8, 0xed,
	0x9e, 0xd5, 0x5c, 0xa1, 0x1c, 0x43, 0x64, 0x5e,
	0x64, 0x72, 0x78, 0x0a, 0x35, 0xbe, 0xba, 0x5a,
	0x37, 0x82, 0x8f, 0x2a, 0x3a, 0xa8, 0x6b, 0x5d,
};
/* The order n, and n - 1. */
static const uint8_t n[32] = {0x51, 0x25, 0x63, 0xfc, 0xc2, 0xca, 0xb9, 0xf3, 0x84, 0x9e, 0x17,
			      0xa7, 0xad, 0xfa, 0xe6, 0xbc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			      0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};

/*
 * 2^64 * G and 2^255 * G: affine double-and-add over the integers modulo p
 * in Python (pow(x, -1, p) for the inverses), the same points as OpenSSL's
 * public key for those private keys.
 */
static const uint8_t g64x[32] = {
	0x63, 0xdb, 0x14, 0x8e, 0xb4, 0x5c, 0xe7, 0x90,
	0x7e, 0x1f, 0x65, 0xad, 0xaa, 0x3b, 0x49, 0x29,
	0xde, 0x25, 0x6e, 0x32, 0x2e, 0x59, 0x92, 0x84,
	0xa5, 0xaa, 0x11, 0x28, 0xbc, 0x22, 0xa8, 0x0f,
};
static const uint8_t g64y[32] = {
	0xe7, 0x2e, 0x46, 0x5f, 0x54, 0x24, 0x11, 0xe4,
	0xf5, 0x82, 0xfe, 0x50, 0x50, 0xa6, 0xb1, 0x34,
	0x8b, 0x18, 0xdf, 0xb3, 0xbc, 0xd4, 0x4a, 0x6f,
	0x0d, 0xa8, 0xdb, 0xf5, 0xe8, 0x4a, 0xf4, 0xbf,
};
static const uint8_t g255x[32] = {
	0x54, 0x2b, 0x5f, 0x37, 0x8f, 0xec, 0x2d, 0xb5,
	0x50, 0x23, 0xe9, 0xe3, 0x60, 0x35, 0xfe, 0x4e,
	0xbc, 0x24, 0x15, 0x89, 0x11, 0xe9, 0x66, 0x50,
	0x13, 0x23, 0x6b, 0x2e, 0x91, 0x0a, 0xb2, 0x77,
};
static const uint8_t g255y[32] = {
	0xff, 0x67, 0xcc, 0xd6, 0xfc, 0x01, 0xa8, 0xca,
	0xf1, 0xe0, 0x50, 0xe8, 0xa1, 0x3d, 0x62, 0xdf,
	0x72, 0x8a, 0x03, 0xdd, 0xfc, 0x0b, 0xb1, 0xf7,
	0xf7, 0xa3, 0xce, 0x25, 0x18, 0x29, 0xdc, 0xa3,
};

static uint32_t ram_word(uint32_t slot, uint32_t i)
{
	return sys_read32(slot + 4U * i);
}

static bool slot_clear(uint32_t slot)
{
	for (uint32_t i = 0; i < 9U; i++) {
		if (ram_word(slot, i) != 0U) {
			return false;
		}
	}
	return true;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	if (IS_ENABLED(CONFIG_TC32_P256_PKE)) {
		sys_write32(2000U, EMU_PKE_US);
	}
}

ZTEST(tc32_p256, test_01_debug_key)
{
	uint8_t x[32], y[32];

	zassert_equal(tc32_p256_mul(da, NULL, NULL, x, y), 0);
	zassert_mem_equal(x, ax, 32, "x of the debug public key");
	zassert_mem_equal(y, ay, 32, "y of the debug public key");
}

ZTEST(tc32_p256, test_02_ecdh)
{
	uint8_t x[32], y[32], x2[32], y2[32];

	zassert_equal(tc32_p256_mul(db, NULL, NULL, x, y), 0);
	zassert_mem_equal(x, bx, 32, "x of the recorded responder's public key");
	zassert_mem_equal(y, by, 32, "y of the recorded responder's public key");
	zassert_equal(tc32_p256_mul(db, px, py, x, y), 0);
	zassert_mem_equal(x, dhkey, 32, "the recorded pairing's DHKey");
	zassert_equal(tc32_p256_mul(da, bx, by, x, y), 0);
	zassert_equal(tc32_p256_mul(db, ax, ay, x2, y2), 0);
	zassert_mem_equal(x2, x, 32, "dA * B and dB * A, x");
	zassert_mem_equal(y2, y, 32, "dA * B and dB * A, y");
	zassert_equal(tc32_p256_verify(bx, by), 0, "the recorded responder's public key");
	zassert_equal(tc32_p256_verify(px, py), 0, "the recorded initiator's public key");
}

ZTEST(tc32_p256, test_03_verify)
{
	uint8_t bad[32];

	zassert_equal(tc32_p256_verify(ax, ay), 0, "the debug public key is on the curve");
	memcpy(bad, ay, 32);
	bad[0] ^= 0x01U;
	zassert_equal(tc32_p256_verify(ax, bad), -EINVAL, "a point off the curve");
	memset(bad, 0xff, 32);
	bad[12] = 0U;
	bad[13] = 0U;
	bad[14] = 0U;
	bad[15] = 0U;
	bad[16] = 0U;
	bad[17] = 0U;
	bad[18] = 0U;
	bad[19] = 0U;
	bad[20] = 0U;
	bad[21] = 0U;
	bad[22] = 0U;
	bad[23] = 0U;
	bad[24] = 0x01U;
	bad[25] = 0U;
	bad[26] = 0U;
	bad[27] = 0U;
	zassert_equal(tc32_p256_verify(ax, bad), -EINVAL, "y = p is not a coordinate");
}

/* k = 2^e: byte e / 8 holds bit e % 8. */
static void pow2(uint8_t k[32], unsigned int e)
{
	memset(k, 0, 32);
	k[e / 8U] = (uint8_t)BIT(e % 8U);
}

ZTEST(tc32_p256, test_04_scalar)
{
	uint8_t k[32] = {0};

	zassert_false(tc32_p256_scalar_ok(k), "0");
	k[0] = 1U;
	zassert_true(tc32_p256_scalar_ok(k), "1");
	pow2(k, 64);
	zassert_true(tc32_p256_scalar_ok(k), "2^64");
	pow2(k, 255);
	zassert_true(tc32_p256_scalar_ok(k), "2^255");
	memset(k, 0, 32);
	k[31] = 0x01U;
	zassert_true(tc32_p256_scalar_ok(k), "2^248, only the top octet not 0");
	k[31] = 0xffU;
	zassert_true(tc32_p256_scalar_ok(k), "0xff * 2^248, only the top octet not 0");
	memcpy(k, n, 32);
	k[0]--;
	zassert_true(tc32_p256_scalar_ok(k), "n - 1");
	zassert_false(tc32_p256_scalar_ok(n), "n");
	memcpy(k, n, 32);
	k[0]++;
	zassert_false(tc32_p256_scalar_ok(k), "n + 1");
	memset(k, 0xff, 32);
	zassert_false(tc32_p256_scalar_ok(k), "2^256 - 1");
	zassert_true(tc32_p256_scalar_ok(da) && tc32_p256_scalar_ok(db), "the test keys");
}

ZTEST(tc32_p256, test_05_zero_scalar)
{
	uint8_t k[32] = {0};
	uint8_t x[32], y[32];

	zassert_equal(tc32_p256_mul(k, NULL, NULL, x, y), -EINVAL, "0 * G is the point at infinity");
}

#ifdef CONFIG_TC32_P256_PKE
ZTEST(tc32_p256, test_06_async)
{
	uint8_t x[32], y[32];
	uint32_t t0;
	int polls = 0;

	sys_write32(5000U, EMU_PKE_US);
	zassert_equal(tc32_p256_poll(x, y), -ENOENT, "nothing started");
	t0 = k_cycle_get_32();
	zassert_equal(tc32_p256_mul_start(da, NULL, NULL), 0);
	zassert_equal(tc32_p256_mul_start(da, NULL, NULL), -EBUSY, "one operation at a time");
	zassert_equal(tc32_p256_verify_start(ax, ay), -EBUSY);
	while (tc32_p256_poll(x, y) == -EAGAIN) {
		polls++;
		k_busy_wait(100);
	}
	zassert_true(polls >= 40 && polls <= 60, "polled %d times over about 5 ms", polls);
	zassert_true(k_cycle_get_32() - t0 >= 5U * (sys_clock_hw_cycles_per_sec() / 1000U),
		     "the result came no sooner than the engine's time");
	zassert_mem_equal(x, ax, 32);
	zassert_mem_equal(y, ay, 32);
	zassert_equal(tc32_p256_poll(x, y), -ENOENT, "the operation is over");
	/* The next one starts at once: Done was cleared. */
	zassert_equal(tc32_p256_verify_start(bx, by), 0);
	while (tc32_p256_poll(NULL, NULL) == -EAGAIN) {
		k_busy_wait(100);
	}
	zassert_equal(tc32_p256_mul(db, NULL, NULL, x, y), 0);
	zassert_mem_equal(x, bx, 32);
}

ZTEST(tc32_p256, test_07_cleared_after)
{
	uint8_t x[32], y[32];

	zassert_equal(tc32_p256_mul(da, bx, by, x, y), 0);
	zassert_true(slot_clear(RAM_A) && slot_clear(RAM_A + SLOT) && slot_clear(RAM_A + 4U * SLOT),
		     "the result and the scalar are cleared from the operand RAM");
	zassert_true(slot_clear(RAM_B) && slot_clear(RAM_B + SLOT), "the point is cleared");
	zassert_equal(sys_read8(CLKEN1) & BIT(7), 0U, "the engine's clock is off");
}

ZTEST(tc32_p256, test_08_timeout)
{
	uint8_t x[32], y[32];
	uint32_t t0 = k_cycle_get_32();

	sys_write32(CONFIG_SOC_TLSR8278_PKE_TIMEOUT_MS * 1000U * 4U, EMU_PKE_US);
	zassert_equal(tc32_p256_mul(da, NULL, NULL, x, y), -ETIMEDOUT);
	zassert_true(k_cycle_get_32() - t0 <
			     (uint32_t)(CONFIG_SOC_TLSR8278_PKE_TIMEOUT_MS + 100) *
				     (sys_clock_hw_cycles_per_sec() / 1000U),
		     "gave up at the time limit");
	zassert_equal(tc32_p256_poll(x, y), -ENOENT, "the stopped operation is over");
	sys_write32(2000U, EMU_PKE_US);
	zassert_equal(tc32_p256_mul(da, NULL, NULL, x, y), 0, "the engine works again");
	zassert_mem_equal(x, ax, 32);
}
#endif /* CONFIG_TC32_P256_PKE */

ZTEST(tc32_p256, test_09_time)
{
	uint8_t x[32], y[32];
	uint32_t t0, t_g, t_p;

	t0 = k_cycle_get_32();
	zassert_equal(tc32_p256_mul(db, NULL, NULL, x, y), 0);
	t_g = k_cycle_get_32() - t0;
	t0 = k_cycle_get_32();
	zassert_equal(tc32_p256_mul(db, px, py, x, y), 0);
	t_p = k_cycle_get_32() - t0;
	TC_PRINT("time: k * G %u us, k * P %u us (%s)\n",
		 t_g / (sys_clock_hw_cycles_per_sec() / 1000000U),
		 t_p / (sys_clock_hw_cycles_per_sec() / 1000000U),
		 IS_ENABLED(CONFIG_TC32_P256_PKE) ? "the engine, the emulator's model" : "p256-m");
}

static int finish(uint8_t *x, uint8_t *y)
{
	int rc;

	while ((rc = tc32_p256_poll(x, y)) == -EAGAIN) {
		k_busy_wait(100);
	}
	return rc;
}

ZTEST(tc32_p256, test_10_cancel)
{
	uint8_t x[32], y[32];
	uint32_t t0;

	zassert_equal(tc32_p256_cancel(), -ENOENT, "nothing started");

	/* A multiplication given up while the engine runs; then another one. */
	if (IS_ENABLED(CONFIG_TC32_P256_PKE)) {
		sys_write32(5000U, EMU_PKE_US);
	}
	zassert_equal(tc32_p256_mul_start(da, NULL, NULL), 0);
	if (IS_ENABLED(CONFIG_TC32_P256_PKE)) {
		zassert_equal(tc32_p256_poll(x, y), -EAGAIN, "the engine is running");
	}
	t0 = k_cycle_get_32();
	zassert_equal(tc32_p256_cancel(), 0, "the multiplication is given up");
	zassert_true(k_cycle_get_32() - t0 < sys_clock_hw_cycles_per_sec() / 1000U,
		     "given up at once, not waited for");
	zassert_equal(tc32_p256_poll(x, y), -ENOENT, "nothing is under way");
	zassert_equal(tc32_p256_cancel(), -ENOENT, "given up once");
	if (IS_ENABLED(CONFIG_TC32_P256_PKE)) {
		zassert_true(slot_clear(RAM_A + 4U * SLOT), "the scalar is cleared");
		zassert_true(slot_clear(RAM_B) && slot_clear(RAM_B + SLOT), "the point is cleared");
		zassert_equal(sys_read8(CLKEN1) & BIT(7), 0U, "the engine's clock is off");
	}
	zassert_equal(tc32_p256_mul_start(db, NULL, NULL), 0, "a new operation starts");
	zassert_equal(finish(x, y), 0);
	zassert_mem_equal(x, bx, 32, "x of the recorded responder's public key");
	zassert_mem_equal(y, by, 32, "y of the recorded responder's public key");

	/* A check given up; the DHKey after it. */
	zassert_equal(tc32_p256_verify_start(px, py), 0);
	zassert_equal(tc32_p256_cancel(), 0, "the check is given up");
	zassert_equal(tc32_p256_mul_start(db, px, py), 0, "a new operation starts");
	zassert_equal(finish(x, y), 0);
	zassert_mem_equal(x, dhkey, 32, "the recorded pairing's DHKey");

	/* An operation that has ended but whose result was not taken. */
	zassert_equal(tc32_p256_mul_start(da, bx, by), 0);
	if (IS_ENABLED(CONFIG_TC32_P256_PKE)) {
		k_busy_wait(6000);
	}
	zassert_equal(tc32_p256_cancel(), 0, "the result is dropped");
	zassert_equal(tc32_p256_poll(x, y), -ENOENT, "nothing is under way");
	if (IS_ENABLED(CONFIG_TC32_P256_PKE)) {
		zassert_true(slot_clear(RAM_A) && slot_clear(RAM_A + SLOT),
			     "the result is cleared from the operand RAM");
	}
	zassert_equal(tc32_p256_mul(da, NULL, NULL, x, y), 0, "a new operation runs");
	zassert_mem_equal(x, ax, 32, "x of the debug public key");
	zassert_mem_equal(y, ay, 32, "y of the debug public key");
}

/* Scalars whose low 64 bits are 0, through the start and the poll. */
ZTEST(tc32_p256, test_11_high_scalar)
{
	uint8_t k[32], x[32], y[32];

	pow2(k, 64);
	zassert_equal(tc32_p256_mul_start(k, NULL, NULL), 0, "2^64 is taken");
	zassert_equal(finish(x, y), 0);
	zassert_mem_equal(x, g64x, 32, "x of 2^64 * G");
	zassert_mem_equal(y, g64y, 32, "y of 2^64 * G");
	pow2(k, 255);
	zassert_equal(tc32_p256_mul(k, NULL, NULL, x, y), 0, "2^255 is taken");
	zassert_mem_equal(x, g255x, 32, "x of 2^255 * G");
	zassert_mem_equal(y, g255y, 32, "y of 2^255 * G");
}

ZTEST_SUITE(tc32_p256, NULL, NULL, before, NULL, NULL);
