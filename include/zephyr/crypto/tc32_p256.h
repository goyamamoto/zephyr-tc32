/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief P-256 for Bluetooth LE Secure Connections (TC32_P256).
 *
 * Point multiplication and the check that a point is on the curve (a key
 * pair, the shared secret, the peer's public key), on the TLSR8278's public
 * key engine (TC32_P256_PKE, tlsr_pke.c) or in software (TC32_P256_SOFT,
 * p256-m). Coordinates and scalars are 32 little-endian octets, as the
 * Pairing Public Key PDU carries them. An operation is started and its result
 * taken by a later call, so a thread can serve other work while the engine
 * runs (the software implementation finishes inside the start call); one
 * operation runs at a time, until its result is taken or it is given up
 * (tc32_p256_cancel()).
 */

#ifndef ZEPHYR_INCLUDE_CRYPTO_TC32_P256_H_
#define ZEPHYR_INCLUDE_CRYPTO_TC32_P256_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether k is a valid private key: 1 <= k < n, the curve's order.
 *
 * @param k The scalar, 32 little-endian octets.
 *
 * @return true when k can be a private key.
 */
bool tc32_p256_scalar_ok(const uint8_t k[32]);

/**
 * @brief Start Q = k * P.
 *
 * @param k The scalar, 32 little-endian octets.
 * @param px The x coordinate of P, or NULL for the base point G (with py NULL).
 * @param py The y coordinate of P.
 *
 * @retval 0 Started.
 * @retval -EBUSY An operation is under way.
 * @retval -EINVAL k is 0 or not below the curve's order (tc32_p256_scalar_ok()).
 */
int tc32_p256_mul_start(const uint8_t k[32], const uint8_t *px, const uint8_t *py);

/**
 * @brief Start the check that (x, y) is a point on the curve.
 *
 * @param px The x coordinate.
 * @param py The y coordinate.
 *
 * @retval 0 Started.
 * @retval -EBUSY An operation is under way.
 */
int tc32_p256_verify_start(const uint8_t px[32], const uint8_t py[32]);

/**
 * @brief Take the result of the operation under way.
 *
 * Once it gives anything but -EAGAIN the operation is over and nothing of it
 * is kept (the engine's operand RAM is cleared and its clock stopped).
 *
 * @param qx Where the x coordinate of a multiplication's result goes (NULL:
 *        not wanted; a check has none).
 * @param qy Where its y coordinate goes.
 *
 * @retval 0 Done: qx and qy hold the result, or the point is on the curve.
 * @retval -EAGAIN The engine is still running.
 * @retval -EINVAL The point is not on the curve, or the multiplication gave
 *         the point at infinity (a scalar that is a multiple of the order).
 * @retval -EIO The engine stopped for another reason.
 * @retval -ENOENT No operation was started.
 */
int tc32_p256_poll(uint8_t *qx, uint8_t *qy);

/**
 * @brief Give up the operation under way.
 *
 * For a caller that no longer wants the result (a pairing abandoned while
 * its key pair or DHKey is being computed): until the operation is taken by
 * tc32_p256_poll() or given up here, every start gives -EBUSY. The engine is
 * stopped (CTRL bit 16) and left as tc32_p256_poll() leaves it after an
 * operation: Done cleared, the operand RAM cleared, its clock stopped; the
 * software implementation drops the result it holds. Afterwards
 * tc32_p256_poll() gives -ENOENT and a new operation can start. It does not
 * sleep: the wait for the engine's stop is bounded and busy.
 *
 * @retval 0 The operation is given up.
 * @retval -ENOENT No operation was started; nothing is done.
 * @retval -ETIMEDOUT The engine did not report the stop in time: it is
 *         given up all the same and the engine is held in reset until the
 *         next start.
 */
int tc32_p256_cancel(void);

/**
 * @brief Q = k * P, waiting for the engine.
 *
 * As tc32_p256_mul_start() and tc32_p256_poll(), waiting for the engine up
 * to SOC_TLSR8278_PKE_TIMEOUT_MS.
 *
 * @param k The scalar.
 * @param px The x coordinate of P, or NULL for the base point G.
 * @param py The y coordinate of P.
 * @param qx Where the result's x coordinate goes.
 * @param qy Where its y coordinate goes.
 *
 * @retval 0 qx and qy hold the result.
 * @retval -EBUSY An operation is under way.
 * @retval -EINVAL k is 0 or not below the curve's order.
 * @retval -EIO The engine stopped for another reason.
 * @retval -ETIMEDOUT The engine did not finish in time (the operation is
 *         given up, as by tc32_p256_cancel()).
 */
int tc32_p256_mul(const uint8_t k[32], const uint8_t *px, const uint8_t *py, uint8_t qx[32],
		  uint8_t qy[32]);

/**
 * @brief Whether (x, y) is a point on the curve, waiting for the engine.
 *
 * @param px The x coordinate.
 * @param py The y coordinate.
 *
 * @retval 0 On the curve.
 * @retval -EBUSY An operation is under way.
 * @retval -EINVAL Not on the curve.
 * @retval -EIO The engine stopped for another reason.
 * @retval -ETIMEDOUT The engine did not finish in time (the operation is
 *         given up, as by tc32_p256_cancel()).
 */
int tc32_p256_verify(const uint8_t px[32], const uint8_t py[32]);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_CRYPTO_TC32_P256_H_ */
