/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * tc32_p256.h in software (TC32_P256_SOFT) on p256-m: an operation runs to
 * the end inside the start call and its result waits for the poll, or is
 * dropped by tc32_p256_cancel(). p256-m takes and gives big-endian octets;
 * the API's octets are little-endian.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/crypto/tc32_p256.h>

#include "p256-m.h"

static struct {
	bool pending;
	int rc;
	uint8_t q[64]; /* big-endian x then y */
} s;

static void reverse(uint8_t *dst, const uint8_t *src, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		dst[i] = src[len - 1U - i];
	}
}

/* Private keys come from the caller (tc32_p256_mul_start()); p256-m's own
 * key generation and signing, which would draw here, are not used. */
int p256_generate_random(uint8_t *output, unsigned int output_size)
{
	(void)output;
	(void)output_size;
	return P256_RANDOM_FAILED;
}

static int status(int p256)
{
	switch (p256) {
	case P256_SUCCESS:
		return 0;
	case P256_INVALID_PRIVKEY:
	case P256_INVALID_PUBKEY:
		return -EINVAL;
	default:
		return -EIO;
	}
}

bool tc32_p256_scalar_ok(const uint8_t k[32])
{
	uint8_t kb[32];
	int rc;

	reverse(kb, k, 32);
	rc = p256_validate_privkey(kb);
	memset(kb, 0, sizeof(kb));
	return rc == P256_SUCCESS;
}

int tc32_p256_mul_start(const uint8_t k[32], const uint8_t *px, const uint8_t *py)
{
	uint8_t kb[32], p[64];

	if (s.pending) {
		return -EBUSY;
	}
	if (!tc32_p256_scalar_ok(k)) {
		return -EINVAL;
	}
	reverse(kb, k, 32);
	if (px == NULL) {
		s.rc = status(p256_public_from_private(s.q, kb));
	} else {
		reverse(&p[0], px, 32);
		reverse(&p[32], py, 32);
		s.rc = status(p256_point_mul(s.q, kb, p));
	}
	memset(kb, 0, sizeof(kb));
	s.pending = true;
	return 0;
}

int tc32_p256_verify_start(const uint8_t px[32], const uint8_t py[32])
{
	uint8_t p[64];

	if (s.pending) {
		return -EBUSY;
	}
	reverse(&p[0], px, 32);
	reverse(&p[32], py, 32);
	s.rc = status(p256_validate_pubkey(p));
	memset(s.q, 0, sizeof(s.q));
	s.pending = true;
	return 0;
}

int tc32_p256_poll(uint8_t *qx, uint8_t *qy)
{
	int rc;

	if (!s.pending) {
		return -ENOENT;
	}
	rc = s.rc;
	if (rc == 0) {
		if (qx != NULL) {
			reverse(qx, &s.q[0], 32);
		}
		if (qy != NULL) {
			reverse(qy, &s.q[32], 32);
		}
	}
	memset(&s, 0, sizeof(s));
	return rc;
}

int tc32_p256_cancel(void)
{
	if (!s.pending) {
		return -ENOENT;
	}
	memset(&s, 0, sizeof(s));
	return 0;
}

int tc32_p256_mul(const uint8_t k[32], const uint8_t *px, const uint8_t *py, uint8_t qx[32],
		  uint8_t qy[32])
{
	int rc = tc32_p256_mul_start(k, px, py);

	return rc != 0 ? rc : tc32_p256_poll(qx, qy);
}

int tc32_p256_verify(const uint8_t px[32], const uint8_t py[32])
{
	int rc = tc32_p256_verify_start(px, py);

	return rc != 0 ? rc : tc32_p256_poll(NULL, NULL);
}
