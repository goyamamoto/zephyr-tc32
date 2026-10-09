/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief One AES-128 block encryption for the TLSR8278 (TC32_AES128).
 *
 * The one AES of an image: tc32_rng's and, for example, a BLE stack's. On
 * the chip's AES block (TC32_AES128_HW_BLOCK) or in software
 * (TC32_AES128_SOFTWARE).
 */

#ifndef ZEPHYR_INCLUDE_CRYPTO_TC32_AES_H_
#define ZEPHYR_INCLUDE_CRYPTO_TC32_AES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encrypt one block with AES-128 (FIPS-197).
 *
 * Key and data in the standard orientation (first byte the most
 * significant). in and out may be the same. Callable from threads and
 * interrupts.
 *
 * @param key The 16-byte key.
 * @param in The 16-byte plaintext.
 * @param out The 16-byte ciphertext.
 */
void tc32_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_CRYPTO_TC32_AES_H_ */
