/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief tc32_rng: the TLSR8278's random number generator (TC32_RNG).
 *
 * A generator of its own, next to Zephyr's random subsystem, which it does
 * not touch: it provides neither sys_rand_get() nor sys_csrand_get(). It
 * draws on several sources, trusts none of them alone, and keeps a seed
 * across power cycles:
 *
 * - a 16-byte pool that absorbs 16-byte blocks by CBC-MAC with AES-128
 *   under a secret key that every reseed replaces;
 * - an AES-128-CTR generator with fast key erasure: each request ends by
 *   replacing the key with a block of its own key stream;
 * - a reseed that replaces the key by a Davies-Meyer step of the old key
 *   with the pool as the cipher key, then clears the pool;
 * - sources: the TRNG's raw output (credited with nothing unless
 *   TC32_RNG_TRNG_CREDIT), the 32 kHz RC oscillator's jitter against the
 *   crystal's system timer, the ADC's noise on VBAT (TC32_RNG_ADC), events
 *   and samples the application feeds in, a seed record kept by the
 *   application, and the device's unique ID;
 * - readiness as in Linux: tc32_rng_get() and tc32_rng_get_key_material()
 *   give nothing until 128 credited bits have been absorbed since boot.
 *
 * The TRNG is read by this generator only; its output never reaches a
 * caller unmixed.
 */

#ifndef ZEPHYR_INCLUDE_RANDOM_TC32_RNG_H_
#define ZEPHYR_INCLUDE_RANDOM_TC32_RNG_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the seed record. */
#define TC32_RNG_SEED_SIZE 20

/** Sources for tc32_rng_add_event() and tc32_rng_add_sample(). */
enum tc32_rng_source {
	/** Time a radio packet arrived (BLE or 2.4 GHz). */
	TC32_RNG_SRC_RADIO_RX = 1,
	/** RSSI or noise level of a received packet (a sample). */
	TC32_RNG_SRC_RADIO_RSSI = 2,
	/** Time of a key press or release. */
	TC32_RNG_SRC_KEY = 3,
	/** Time of a knob step. */
	TC32_RNG_SRC_KNOB = 4,
	/** System timer at a USB start of frame (the host's crystal against ours). */
	TC32_RNG_SRC_USB_SOF = 5,
	/** ADC battery reading (a sample). */
	TC32_RNG_SRC_ADC_BATTERY = 6,
	/** ADC temperature reading (a sample). */
	TC32_RNG_SRC_ADC_TEMP = 7,
	/** A BLE peer's random values (public: a sample, credited with nothing). */
	TC32_RNG_SRC_PEER_RANDOM = 8,
	/** A BLE peer's address (public: a sample, credited with nothing). */
	TC32_RNG_SRC_PEER_ADDRESS = 9,
	/** Calibration data of the chip (a sample, credited with nothing). */
	TC32_RNG_SRC_CALIBRATION = 10,
	/** Anything else. */
	TC32_RNG_SRC_OTHER = 15,
};

/**
 * @brief Random bytes for cryptographic use.
 *
 * Thread context. While the generator is not ready, each call first takes
 * TC32_RNG_JITTER_CHUNK jitter samples (about 8 ms) and, with TC32_RNG_ADC,
 * 512 ADC codes (about 11 ms). The generator reseeds
 * when TC32_RNG_RESEED_INTERVAL has passed since the last reseed (or the
 * system timer's 2^32-tick wrap, 268 s, has hidden the time since).
 *
 * @param dst Where the bytes go.
 * @param len How many bytes.
 *
 * @retval 0 dst holds len bytes.
 * @retval -EAGAIN Not ready: fewer than 128 credited bits since boot.
 * @retval -ETIMEDOUT Not ready, the 32 kHz count stopped changing while the
 *         call took jitter (it gave up after 4 of its periods without an
 *         edge and the count read again unchanged), and the ADC credited
 *         nothing; a later call tries again.
 * @retval -EIO The AES known-answer test failed; nothing is given out.
 */
int tc32_rng_get(void *dst, size_t len);

/**
 * @brief Random bytes for keys.
 *
 * As tc32_rng_get(), but the generator always reseeds first, from the TRNG,
 * a few jitter samples, a few ADC codes and the events since the last
 * reseed.
 *
 * @param dst Where the bytes go.
 * @param len How many bytes.
 *
 * @retval 0 dst holds len bytes.
 * @retval -EAGAIN Not ready.
 * @retval -ETIMEDOUT Not ready, the 32 kHz count stopped changing and the
 *         ADC credited nothing.
 * @retval -EIO The AES known-answer test failed.
 */
int tc32_rng_get_key_material(void *dst, size_t len);

/**
 * @brief Random bytes for uses that need no secrecy (timing jitter).
 *
 * Any context; never waits and never fails: the generator's output whatever
 * its state, so before it is ready (or if its known-answer test failed) the
 * bytes are only as unpredictable as what has been absorbed so far.
 *
 * @param dst Where the bytes go.
 * @param len How many bytes.
 */
void tc32_rng_fill(void *dst, size_t len);

/**
 * @brief Mix a value into the event accumulator.
 *
 * Any context, a few instructions. The accumulator (32 bits) is absorbed at
 * the next reseed. Credited with nothing.
 *
 * @param value The value (an RSSI, an ADC reading, a peer's random value).
 * @param source A tc32_rng_source.
 */
void tc32_rng_add_sample(uint32_t value, uint8_t source);

/**
 * @brief Mix the time of an event into the event accumulator.
 *
 * As tc32_rng_add_sample(), with TC32_RNG_EVENT_CREDIT sixteenths of a bit
 * of credit (0 by default).
 *
 * @param timestamp A fine-grained time of the event, such as the system
 *        timer (k_cycle_get_32()) when it was seen.
 * @param source A tc32_rng_source.
 */
void tc32_rng_add_event(uint32_t timestamp, uint8_t source);

/**
 * @brief Collect jitter and ADC codes until the generator is ready.
 *
 * Thread context. Blocks while it collects, on a first boot or after the
 * seed record was lost (at once otherwise): rounds of TC32_RNG_JITTER_CHUNK
 * jitter samples of about 31 us and, with TC32_RNG_ADC, a window of 512 ADC
 * codes of about 20.8 us, until the two sources' credit makes 128 bits
 * (about 64 ms from the jitter alone at 1/16 bit per sample). Never longer
 * than TC32_RNG_COLLECT_TIMEOUT_MS (250 ms) and one round. A jitter chunk
 * gives up at once when the 32 kHz count stops changing for 4 of its
 * periods (a count found moved after such a wait, as when the caller was
 * held up meanwhile, is a missed edge, and the chunk goes on); the
 * collection ends at once when that happens in a round whose ADC window
 * credited nothing. A collection that times out credits only what it took,
 * leaves the generator not ready, and can be called again later.
 *
 * @retval 0 Ready.
 * @retval -ETIMEDOUT Not ready within the limit, or the 32 kHz count stopped
 *         and the ADC credited nothing.
 * @retval -EIO The AES known-answer test failed.
 */
int tc32_rng_collect(void);

/**
 * @brief Load the seed record. Provided by the application (weak default:
 * none).
 *
 * Called once, at init.
 *
 * @param record Where the TC32_RNG_SEED_SIZE bytes go.
 *
 * @retval 0 record holds the record last stored.
 * @retval <0 There is none.
 */
int tc32_rng_seed_load(uint8_t record[TC32_RNG_SEED_SIZE]);

/**
 * @brief Store a new seed record in place of the old one. Provided by the
 * application (weak default: fails).
 *
 * Called at init before any output, and when the generator becomes ready.
 * The record must stay secret. A loaded record is credited only if storing
 * its replacement succeeded, so that no two boots use the same record.
 *
 * @param record The TC32_RNG_SEED_SIZE bytes to keep.
 *
 * @retval 0 Stored.
 * @retval <0 Not stored.
 */
int tc32_rng_seed_store(const uint8_t record[TC32_RNG_SEED_SIZE]);

/**
 * @brief The device's unique ID. Provided by the application (weak default:
 * none, zeros).
 *
 * The flash's 128-bit unique ID (DS-TLSR8278 2.1.3), or what else is unique
 * to the device. Absorbed at init (credited with nothing) and part of the
 * seed record's check value, which binds a record to the device.
 *
 * @param id Where the 16 bytes go.
 *
 * @retval 0 id holds the ID.
 * @retval <0 There is none; id is left as zeros.
 */
int tc32_rng_device_id(uint8_t id[16]);

/**
 * @brief Take the SAR ADC for the ADC source. Provided by the application
 * that uses the ADC too (weak default: 0 at once).
 *
 * Thread context. Called before tc32_rng sets the ADC and DFIFO2 up for
 * its VBAT conversions; tc32_rng_adc_unlock() follows once every setting is
 * back as it was. An application that measures with the ADC (a battery
 * level) makes the two uses exclusive here, and, when tc32_rng is called
 * from more than one thread, so are its own uses. With the Zephyr ADC
 * driver (ADC_TELINK_B87) tc32_rng takes that driver's lock as well, after
 * this hook, so that it never interleaves with an adc_read().
 *
 * @retval 0 Go ahead.
 * @retval <0 The ADC is not available: no ADC codes this time.
 */
int tc32_rng_adc_lock(void);

/**
 * @brief Give the SAR ADC back (weak default: nothing).
 */
void tc32_rng_adc_unlock(void);

/**
 * @brief Raw ADC codes (TC32_RNG_ADC_DUMP, measurement images only).
 *
 * Takes n codes as the generator does (after the two that settle the
 * input), absorbing and health-testing them, and copies the low 16 bits of
 * each DMA word (the code and its sign) to buf.
 *
 * @param buf Where the n codes go.
 * @param n The codes to take.
 *
 * @return The codes copied to buf: n, or fewer when the ADC stopped
 *         converting.
 */
uint32_t tc32_rng_adc_samples(uint16_t *buf, uint32_t n);

/**
 * @brief Raw jitter samples (TC32_RNG_JITTER_DUMP, measurement images only).
 *
 * Takes n samples as the generator does, absorbing them, and copies each
 * sample byte (the system timer at a 32 kHz edge, bits 10:3) to buf; the
 * first byte is the edge that only starts the count.
 *
 * @param buf Where the n + 1 bytes go.
 * @param n The samples to take.
 *
 * @return The bytes copied to buf: n + 1, or fewer when the 32 kHz count
 *         stopped.
 */
uint32_t tc32_rng_jitter_samples(uint8_t *buf, uint32_t n);

/** Readiness and the ADC windows' outcomes since boot (TC32_RNG_READINESS). */
struct tc32_rng_readiness {
	/** 0 not ready, 1 ready, 2 the known-answer test failed. */
	uint8_t state;
	/** Credited bits absorbed since boot (up to 128). */
	uint8_t credit_bits;
	/** 1 if the seed record at init was one a ready generator made (full credit). */
	uint8_t record;
	/** ADC windows credited. */
	uint16_t adc_windows;
	/** ADC windows refused for a code outside VBAT's range. */
	uint16_t adc_out_of_range;
	/** ADC windows refused by the health tests. */
	uint16_t adc_health;
};

/**
 * @brief Readiness and the ADC windows' outcomes (TC32_RNG_READINESS,
 *        measurement images only).
 *
 * A window can be refused for both reasons; it is counted under each.
 *
 * @param r Where they go.
 */
void tc32_rng_get_readiness(struct tc32_rng_readiness *r);

/** The generator's state (TC32_RNG_TEST). */
struct tc32_rng_status {
	/** 0 not ready, 1 ready, 2 the known-answer test failed. */
	uint8_t state;
	/** Credited bits absorbed since boot (up to 128). */
	uint8_t credit_bits;
	/** Reseeds since init. */
	uint16_t reseeds;
	/** Jitter samples taken and credited since init. */
	uint32_t samples;
	uint32_t counted;
	/** ADC codes taken and credited, and failed windows, since init (TC32_RNG_ADC). */
	uint32_t adc_samples;
	uint32_t adc_counted;
	uint16_t adc_failed;
	/** The generator's key now. */
	uint8_t key[16];
	/** The pool's key now. */
	uint8_t pool_key[16];
};

/**
 * @brief The generator's state (TC32_RNG_TEST).
 *
 * @param st Filled in.
 */
void tc32_rng_get_status(struct tc32_rng_status *st);

/**
 * @brief Clear the generator and run its init again (TC32_RNG_TEST).
 *
 * @param kat_fault Not 0: the known-answer test is made to fail.
 *
 * @return The state, as tc32_rng_status.state.
 */
int tc32_rng_reinit(uint8_t kat_fault);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_RANDOM_TC32_RNG_H_ */
