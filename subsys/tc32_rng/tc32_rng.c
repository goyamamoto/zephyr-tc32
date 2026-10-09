/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * tc32_rng (include/zephyr/random/tc32_rng.h). The only cipher is AES-128,
 * tc32_aes128_encrypt().
 *
 * Pool. 16 bytes P; a 16-byte input block b is absorbed as
 * P = AES_K(P ^ b), a CBC-MAC step under the pool key K. CBC-MAC with AES
 * is one of the vetted conditioning components of NIST SP 800-90B
 * (3.1.5.1.1); as a conditioner its output has the entropy of its input up
 * to the 128-bit width. Fortuna absorbs into SHA-256 pools and Linux into a
 * BLAKE2s state, both keyless; CBC-MAC needs no second primitive.
 *
 * Pool key. K is secret and changes: at init it is K0, the public FIPS-197
 * Appendix B key (the known-answer test's), only until the first reseed;
 * every reseed sets K = AES_G({0xff, 0, ..., 0}) from the new generator key
 * G, a block of G's key stream the generator never hands out. With a fixed
 * public key the chain is a public permutation: whoever knows P and the
 * key steers P' = AES_K(P ^ b) with the block b he supplies (an attacker's
 * TRNG words or events), can undo blocks, and can precompute across
 * devices. With a secret key he can do none of that, and as K follows G a
 * key that leaked is replaced at the next reseed. The order keeps every
 * block's key independent of the block: a reseed first folds the blocks
 * absorbed since the last reseed (under the key derived then, from earlier
 * blocks) into G, clears P, and only then derives the next K from the new
 * G; blocks absorbed after it use that K. So the first secret K is a
 * one-way derivation (CBC-MAC under K0, Davies-Meyer into G, AES under G)
 * of what init absorbs under K0, in this order: the device's unique ID, the
 * system timer, the 32 kHz timer's count and its calibration result, the
 * seed record, the TRNG's words, a burst of jitter samples and a few ADC
 * codes. It is unique to the device and the boot even when the record and
 * the TRNG repeat.
 *
 * Generator. Key G; a request is AES-128-CTR under G with the counter
 * block {i, 0, ..., 0}: blocks 1, 2, ... are the output and block 0
 * becomes the next G (fast key erasure, as in Linux's ChaCha20 CRNG and
 * Bernstein's construction): the key left after a request says nothing
 * about the output it gave. A request runs in pieces of at most 32 bytes,
 * each with its own key, interrupts off for three AES blocks.
 *
 * Concurrency. Each step is atomic on its own: an absorb, a reseed and a
 * piece of output run with interrupts off; the TRNG's clock and the 32 kHz
 * timer's read mode are put back as they were found, so nested use by an
 * interrupting thread leaves them right. Two threads reseeding at once
 * both fold their inputs into the one pool. The ADC is the application's:
 * its use here sits between tc32_rng_adc_lock() and tc32_rng_adc_unlock().
 *
 * Reseed. G = AES_P(G) ^ G, a Davies-Meyer step with the pool as the cipher
 * key, then P = 0. Davies-Meyer is the one-way compression function block
 * ciphers give: the new key depends on both, one of them unknown is enough,
 * and the new key does not give the old one back. Fortuna hashes the old
 * key with the pools (SHA-256d); Linux replaces the key by the hash of its
 * input pool. The pool is cleared as Fortuna clears the pools it used.
 *
 * Credit, in 1/16 bit, counted since boot and saturating at 128 bits. The
 * generator is ready once 128 credited bits have been absorbed and a reseed
 * has taken them into G, and stays ready (as Linux's crng_ready()). Until
 * then tc32_rng_get() and tc32_rng_get_key_material() give nothing.
 * - Jitter: a sample is the system timer (16 MHz, 0.5 us steps) when the
 *   32 kHz RC timer's count changes; it counts when the system timer moved
 *   since the last edge: 1 / TC32_RNG_JITTER_SAMPLES_PER_BIT bit. An edge
 *   the wait did not see within 4 periods (the caller held up meanwhile)
 *   gives no sample, and the wait starts again from the count as it is.
 *   A byte of each sample is XOR-ed into a block that starts as the event
 *   accumulator, absorbed every 16 samples and at the end.
 * - Seed record: 128 bits, if its check value (a CBC-MAC under K0 over the
 *   device ID and the record's first 16 bytes) matches, it was made by a
 *   ready generator, and storing its replacement succeeded. Someone who
 *   puts an old flash image back puts an old record back; the jitter and
 *   the TRNG are still mixed in, as with systemd's seed on a cloned image.
 * - ADC (TC32_RNG_ADC): a sample is a conversion of VBAT, the chip's
 *   supply; the low byte of each is XOR-ed into a block absorbed every 16
 *   codes and at the end. Each window of 512 codes whose health tests pass
 *   and whose codes all lie in VBAT's range counts
 *   1 / TC32_RNG_ADC_SAMPLES_PER_BIT bit per code; a window that fails
 *   counts nothing and is absorbed all the same.
 * - TRNG: nothing, unless TC32_RNG_TRNG_CREDIT: then 1 bit per byte while
 *   its health tests pass.
 * - Events: TC32_RNG_EVENT_CREDIT / 16 bit per tc32_rng_add_event().
 * The SRAM's contents at power-on are not a source: the startup code that
 * runs before the boot is counted clears and fills the RAM.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/crypto/tc32_aes.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/random/tc32_rng.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define FULL (128U * 16U)
#define SPB  CONFIG_TC32_RNG_JITTER_SAMPLES_PER_BIT

/* ADC codes per collection round (one health-test window, ADC_WINDOW) and
 * per reseed.
 */
#if defined(CONFIG_TC32_RNG_ADC)
#define ADC_CHUNK  512U
#define RESEED_ADC CONFIG_TC32_RNG_RESEED_ADC
#else
#define ADC_CHUNK  0U
#define RESEED_ADC 0U
#endif

/* The system timer (16 MHz; bits 2:0 read 0), the 32 kHz timer's control
 * (bit 0: 1 write mode, 0 read mode), its read value and its calibration
 * result (DS-TLSR8278 Table 5-2).
 */
#define STIMER    0x00800740U
#define K32_CTRL  0x0080074aU
#define K32_READ  0x00800750U
#define K32_CAL   0x00800754U
#define K32_POLLS 2000U

/* The 32 kHz count counts as stopped when 4 of its periods (on the system
 * timer) have passed in the wait for an edge and the count, read again then,
 * has still not changed; one that has changed was only seen late (the caller
 * held up meanwhile) and the wait starts again. A wait also ends after
 * K32_POLLS polls should the system timer stand still as well.
 * tc32_rng_collect() gives up after TC32_RNG_COLLECT_TIMEOUT_MS.
 */
#define K32_EDGE_TICKS (4U * 16000000U / 32768U)
#define COLLECT_TICKS  ((uint32_t)CONFIG_TC32_RNG_COLLECT_TIMEOUT_MS * 16000U)

/* The system timer in units of 2^24 ticks (1.05 s). */
#define SECONDS() ((uint8_t)(sys_read32(STIMER) >> 24))

#define lock()      tlsr827x_irq_save()
#define unlock(key) tlsr827x_irq_restore(key)

enum {
	NOT_READY,
	READY,
	FAILED,
};

static struct {
	uint8_t g[16];
	uint8_t p[16];
	uint8_t k[16];
	uint32_t ev;
	uint16_t credit;
	uint8_t last;
	uint8_t state;
#if defined(CONFIG_TC32_RNG_TEST)
	uint8_t kat_fault;
	uint16_t reseeds;
	uint32_t samples;
	uint32_t counted;
#endif
#if defined(CONFIG_TC32_RNG_TEST) && defined(CONFIG_TC32_RNG_ADC)
	uint32_t adc_samples;
	uint32_t adc_counted;
	uint16_t adc_failed;
#endif
#if defined(CONFIG_TC32_RNG_JITTER_DUMP)
	uint8_t *dump;
#endif
#if defined(CONFIG_TC32_RNG_ADC_DUMP)
	uint16_t *adc_dump;
#endif
#if defined(CONFIG_TC32_RNG_READINESS)
	struct tc32_rng_readiness readiness;
#endif
} rng;

/* FIPS-197 Appendix B: key (K0, and the seed record's check key), plaintext,
 * ciphertext.
 */
static const uint8_t kat[48] = {
	0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
	0x09, 0xcf, 0x4f, 0x3c, 0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
	0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34, 0x39, 0x25, 0x84, 0x1d,
	0x02, 0xdc, 0x09, 0xfb, 0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32,
};

#define KEY kat

/* x = AES_key(x ^ b): one CBC-MAC step. */
static void cbc(const uint8_t *key, uint8_t x[16], const uint8_t b[16])
{
	for (int i = 0; i < 16; i++) {
		x[i] ^= b[i];
	}
	tc32_aes128_encrypt(key, x, x);
}

static void absorb(const void *b)
{
	uint8_t key = lock();

	cbc(rng.k, rng.p, b);
	unlock(key);
}

static void credit(uint32_t c)
{
	rng.credit = (uint16_t)MIN(rng.credit + c, FULL);
}

/* Davies-Meyer: G = AES_P(G) ^ G; then P = 0; then K = AES_G({0xff, 0, ...}). */
static void reseed(void)
{
	uint8_t t[16];
	uint8_t key = lock();

	tc32_aes128_encrypt(rng.p, rng.g, t);
	for (int i = 0; i < 16; i++) {
		rng.g[i] ^= t[i];
		rng.p[i] = 0U;
		t[i] = 0U;
	}
	t[0] = 0xffU;
	tc32_aes128_encrypt(rng.g, t, rng.k);
	unlock(key);
	rng.last = SECONDS();
	IF_ENABLED(CONFIG_TC32_RNG_TEST, (rng.reseeds++;))
}

/* AES-128-CTR under G with fast key erasure: per piece, blocks 1 and 2 are
 * output and block 0 is the next G.
 */
static void gen(uint8_t *out, size_t len)
{
	uint8_t c[16] = {0};
	uint8_t b[16];

	do {
		uint8_t key = lock();

		for (c[0] = 1U; c[0] <= 2U && len > 0U; c[0]++) {
			size_t n = MIN(len, 16U);

			tc32_aes128_encrypt(rng.g, c, b);
			memcpy(out, b, n);
			out += n;
			len -= n;
		}
		c[0] = 0U;
		tc32_aes128_encrypt(rng.g, c, rng.g);
		unlock(key);
	} while (len > 0U);
}

/*
 * Takes n jitter samples after one more edge that only starts the count,
 * XOR-ing a byte of each into a block that starts as the event accumulator
 * and absorbing the block every 16 samples and at the end; credits those
 * that count and returns their number.
 */
static uint32_t jitter(uint32_t n)
{
	uint32_t w[4] = {rng.ev};
	uint8_t *b = (uint8_t *)w;
	uint8_t ctrl = sys_read8(K32_CTRL);
	uint32_t good = 0U, prev = 0U, k;

	sys_write8(ctrl & (uint8_t)~BIT(0), K32_CTRL);
	k = sys_read32(K32_READ);
	for (uint32_t i = 0U; i <= n; i++) {
		uint32_t polls = K32_POLLS;
		uint32_t s = sys_read32(STIMER);

		while (sys_read32(K32_READ) == k) {
			if (--polls == 0U) {
				goto out;
			}
			if (sys_read32(STIMER) - s > K32_EDGE_TICKS) {
				uint32_t now = sys_read32(K32_READ);

				if (now == k) {
					goto out;
				}
				k = now;
				s = sys_read32(STIMER);
				polls = K32_POLLS;
			}
		}
		s = sys_read32(STIMER);
		k = sys_read32(K32_READ);
		b[i & 15U] ^= (uint8_t)(s >> 3);
#if defined(CONFIG_TC32_RNG_JITTER_DUMP)
		if (rng.dump != NULL) {
			*rng.dump++ = (uint8_t)(s >> 3);
		}
#endif
		if ((i & 15U) == 15U) {
			absorb(b);
		}
		if (i > 0U && s != prev) {
			good++;
		}
		prev = s;
	}
out:
	sys_write8(ctrl, K32_CTRL);
	absorb(b);
#if defined(CONFIG_TC32_RNG_TEST)
	rng.samples += n;
	rng.counted += good;
#endif
	credit(good * 16U / SPB);
	return good;
}

#if defined(CONFIG_TC32_RNG_ADC)
/*
 * The SAR ADC on VBAT, the chip's own supply: an input every board has, with
 * nothing outside the chip. The settings: the misc channel, 4 MHz ADC
 * clock, states of 240 + 10 cycles, 14 bits, differential against ground,
 * 1.2 V reference, 6 sampling cycles, the 1/3 VBAT divider and prescaler 1,
 * and the reference trims the battery measurement sets. A conversion comes
 * every 20.8 us (48 kHz): on hardware, a load switched every 500 us showed
 * in the codes 24 codes apart, half the rate that 250 cycles of 24 MHz
 * would give. The conversions go by DFIFO2 into an 8-slot ring of 32-bit
 * words: the code in bits 12:0, its sign in bit 13 and again in bits 15:14.
 * The application owns the ADC: tc32_rng_adc_lock() and
 * tc32_rng_adc_unlock() bracket this use, with the ADC driver
 * (ADC_TELINK_B87) also its lock, and every analog and DFIFO2 register it
 * sets is put back as it was, the ADC powered down meanwhile.
 * The analog register port is 0xb8-0xba: the register number to 0xb8, then
 * 0xba = 0x40 (read, the value from 0xb9) or the value to 0xb9 and
 * 0xba = 0x60 (write), a wait while 0xba bit 0 reads 1, and 0xba = 0;
 * interrupts off around each register's read and write, as interrupt
 * handlers use the port too.
 */
#define ANA_ADDR    0x008000b8U
#define ANA_DATA    0x008000b9U
#define ANA_CTRL    0x008000baU
#define RST1        0x00800061U
#define DFIFO2_ADDR 0x00800b08U
#define DFIFO2_SIZE 0x00800b0aU
#define DFIFO_MODE  0x00800b10U
#define DFIFO2_WPTR 0x00800b1eU
#define ADC_SPB     CONFIG_TC32_RNG_ADC_SAMPLES_PER_BIT

/* A slot is read when it is not 0 and cleared then, so a code is taken
 * once. A reader held up for more than the ring (8 conversions, 167 us)
 * finds every slot written, takes them, and is then up to 8 conversions out
 * of step with the DMA: a slot is waited for 16 conversion periods, two
 * rings (on the system timer), and, should the system timer stand still,
 * ADC_POLLS polls (each a read of the slot and of the system timer), and
 * one still 0 when read again then means that the ADC does not convert.
 */
#define ADC_WAIT_TICKS (16U * 16000000U / 48000U)
#define ADC_POLLS      20000U

/* A code of VBAT: bits 15:12 0001, 4096 to 8191, VBAT from 1.8 V to 3.6 V
 * (the chip's supply range) through the 1/3 divider against 1.2 V. A code
 * with bits 15:13 set (below ground) or under 4096 (below 1.8 V) is not the
 * supply the chip runs on: its window counts nothing.
 */
#define ADC_VBAT(v) (((v) & 0xf000U) == 0x1000U)

/*
 * Health tests (NIST SP 800-90B 4.4) on d, the low 4 bits of the difference
 * of two consecutive codes: the difference takes out the input's level and
 * its drift, and a constant, ramping or two-valued input makes d constant
 * or leaves one value at least half the time. Claimed for the tests: 2 bits
 * of min-entropy per d, alpha = 2^-20 (alpha a cutoff's false positive
 * rate). Repetition Count Test cutoff 1 + 20 / 2 = 11; Adaptive Proportion
 * Test window 512, cutoff 177 (the 90B formula, 1 + CRITBINOM(512, 2^-2,
 * 1 - 2^-20)), counted for every value of d rather than only the window's
 * first. Thermal noise of 1 LSB rms passes nearly every window (a d value
 * then has a chance of about 0.27); less fails, and the ADC is credited
 * with nothing. The credit is not the claim: TC32_RNG_ADC_SAMPLES_PER_BIT.
 */
#define ADC_WINDOW ADC_CHUNK
#define ADC_RCT    11U
#define ADC_APT    177U

/* Analog register, the bits of its value kept, the bits set; entries 0-10
 * are put back in order (entry 0 by entry 11's place, power last).
 */
static const uint8_t adc_cfg[][3] = {
	{0xfc, 0xff, 0x20}, /* powered down while the rest change */
	{0xf4, 0x00, 0x05}, /* ADC clock 24 MHz / 6 */
	{0xf2, 0x00, 0x24}, /* misc channel, state count 2 */
	{0xef, 0x00, 0xf0}, /* r_max_mc 240 */
	{0xf1, 0x00, 0x0a}, /* r_max_s 10, r_max_mc bits 9:8 0 */
	{0xec, 0x00, 0x43}, /* 14 bits, differential */
	{0xeb, 0x00, 0xff}, /* VBAT against ground */
	{0xea, 0x00, 0x02}, /* 1.2 V reference */
	{0xee, 0x00, 0x01}, /* 6 sampling cycles */
	{0xf9, 0xc3, 0x08}, /* VBAT divider 1/3, bits 5:4 0 for prescaler 1 */
	{0xfa, 0x00, 0x3d}, /* prescaler 1, the reference trims */
	{0xfc, 0xdf, 0x00}, /* powered up */
};

#define ADC_SAVED 11U

static volatile uint32_t adc_ring[8] __aligned(16);

#if defined(CONFIG_ADC_TELINK_B87)
#include <tlsr8278_adc.h>

/* With the ADC driver, its lock too: after the application's, the order of
 * an application that holds its lock around adc_read().
 */
static int adc_lock_all(void)
{
	if (tc32_rng_adc_lock() != 0) {
		return -EBUSY;
	}
	return adc_tlsr8278_lock();
}

static void adc_unlock_all(void)
{
	adc_tlsr8278_unlock();
	tc32_rng_adc_unlock();
}

#define ADC_LOCK()   adc_lock_all()
#define ADC_UNLOCK() adc_unlock_all()
#else
#define ADC_LOCK()   tc32_rng_adc_lock()
#define ADC_UNLOCK() tc32_rng_adc_unlock()
#endif

static uint8_t ana_read(uint8_t addr)
{
	uint8_t v;

	sys_write8(addr, ANA_ADDR);
	sys_write8(BIT(6), ANA_CTRL);
	while ((sys_read8(ANA_CTRL) & BIT(0)) != 0U) {
	}
	v = sys_read8(ANA_DATA);
	sys_write8(0U, ANA_CTRL);
	return v;
}

static void ana_write(uint8_t addr, uint8_t v)
{
	sys_write8(addr, ANA_ADDR);
	sys_write8(v, ANA_DATA);
	sys_write8(BIT(6) | BIT(5), ANA_CTRL);
	while ((sys_read8(ANA_CTRL) & BIT(0)) != 0U) {
	}
	sys_write8(0U, ANA_CTRL);
}

/* on: the settings saved to s and the source's put in; off: s put back. */
static void adc_config(uint8_t s[ADC_SAVED], bool on)
{
	for (uint32_t i = 0U; i < ARRAY_SIZE(adc_cfg); i++) {
		uint8_t key = lock();
		uint8_t v = ana_read(adc_cfg[i][0]);

		if (on) {
			if (i < ADC_SAVED) {
				s[i] = v;
			}
			v = (v & adc_cfg[i][1]) | adc_cfg[i][2];
		} else {
			v = i == 0U ? v | BIT(5) : s[i % ADC_SAVED];
		}
		ana_write(adc_cfg[i][0], v);
		unlock(key);
	}
}

/*
 * Takes n codes after two that only let the input settle (two conversion
 * periods after the ADC's reset), XOR-ing the low byte of each into a block
 * absorbed every 16 codes and at the end; credits the windows of ADC_WINDOW
 * codes whose health tests pass and whose codes are all ADC_VBAT(), and
 * returns the number of codes in them. A window cut short counts nothing.
 */
static uint32_t adc(uint32_t n)
{
	uint8_t b[16] = {0};
	uint8_t s[ADC_SAVED];
	uint16_t hist[16];
	uint32_t good = 0U, prev = 0U, slot = 0U, i;
	uint8_t last = 0U, run = 0U, fail = 0U;
	IF_ENABLED(CONFIG_TC32_RNG_READINESS, (uint8_t why = 0U;))
	uint16_t addr, wptr;
	uint8_t size, mode, rst;

	if ((((uintptr_t)adc_ring) >> 16) != 0x84U || ADC_LOCK() != 0) {
		return 0U;
	}
	addr = sys_read16(DFIFO2_ADDR);
	size = sys_read8(DFIFO2_SIZE);
	mode = sys_read8(DFIFO_MODE);
	wptr = sys_read16(DFIFO2_WPTR);
	adc_config(s, true);
	rst = sys_read8(RST1);
	sys_write8(rst | BIT(5), RST1);
	sys_write8(rst & (uint8_t)~BIT(5), RST1);
	for (i = 0U; i < ARRAY_SIZE(adc_ring); i++) {
		adc_ring[i] = 0U;
	}
	sys_write16((uint16_t)(uintptr_t)adc_ring, DFIFO2_ADDR);
	sys_write8(sizeof(adc_ring) / 16U - 1U, DFIFO2_SIZE);
	sys_write16(0U, DFIFO2_WPTR);
	sys_write8(mode | BIT(2), DFIFO_MODE);
	for (i = 0U; i < n + 2U; i++) {
		uint32_t polls = ADC_POLLS;
		uint32_t t0 = sys_read32(STIMER);
		uint32_t v, d, j;

		while ((v = adc_ring[slot]) == 0U) {
			if (--polls == 0U) {
				goto out;
			}
			if (sys_read32(STIMER) - t0 > ADC_WAIT_TICKS) {
				v = adc_ring[slot];
				if (v == 0U) {
					goto out;
				}
				break;
			}
		}
		adc_ring[slot] = 0U;
		slot = (slot + 1U) & 7U;
		d = (v - prev) & 15U;
		prev = v;
		if (i < 2U) {
			continue;
		}
		j = i - 2U;
#if defined(CONFIG_TC32_RNG_ADC_DUMP)
		if (rng.adc_dump != NULL) {
			*rng.adc_dump++ = (uint16_t)v;
		}
#endif
		b[j & 15U] ^= (uint8_t)v;
		if ((j & 15U) == 15U) {
			absorb(b);
		}
		if (j % ADC_WINDOW == 0U) {
			memset(hist, 0, sizeof(hist));
			run = 0U;
			fail = 0U;
			IF_ENABLED(CONFIG_TC32_RNG_READINESS, (why = 0U;))
		}
		if (!ADC_VBAT(v)) {
			fail = 1U;
			IF_ENABLED(CONFIG_TC32_RNG_READINESS, (why |= 1U;))
		}
		if (++hist[d] >= ADC_APT) {
			fail = 1U;
			IF_ENABLED(CONFIG_TC32_RNG_READINESS, (why |= 2U;))
		}
		if (run > 0U && d == last) {
			if (++run >= ADC_RCT) {
				fail = 1U;
				IF_ENABLED(CONFIG_TC32_RNG_READINESS, (why |= 2U;))
			}
		} else {
			last = (uint8_t)d;
			run = 1U;
		}
		if (j % ADC_WINDOW == ADC_WINDOW - 1U) {
			if (fail == 0U) {
				good += ADC_WINDOW;
			}
			IF_ENABLED(CONFIG_TC32_RNG_TEST, (rng.adc_failed += fail;))
#if defined(CONFIG_TC32_RNG_READINESS)
			rng.readiness.adc_windows += fail == 0U ? 1U : 0U;
			rng.readiness.adc_out_of_range += why & 1U;
			rng.readiness.adc_health += why >> 1;
#endif
		}
	}
out:
	sys_write8(mode, DFIFO_MODE);
	sys_write16(addr, DFIFO2_ADDR);
	sys_write8(size, DFIFO2_SIZE);
	sys_write16(wptr, DFIFO2_WPTR);
	adc_config(s, false);
	ADC_UNLOCK();
	absorb(b);
#if defined(CONFIG_TC32_RNG_TEST)
	rng.adc_samples += i > 2U ? i - 2U : 0U;
	rng.adc_counted += good;
#endif
	credit(good * 16U / ADC_SPB);
	return good;
}
#else
static uint32_t adc(uint32_t n)
{
	ARG_UNUSED(n);
	return 0U;
}
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(telink_b87_rbg)
/*
 * The TRNG (DS-TLSR8278 17), raw output (RTCR = 0), read by this file
 * only. Reset released (0x62 bit 3), clock on (0x65 bit 3), RBGEN with the
 * four RO sources (CR = 0x1f); a word from DR once SR bit 0 reads 1 (Table
 * 17-1 marks bit 0 reserved and bit 1 as the data-ready interrupt flag);
 * the block and its clock off again unless the clock was on. A word that
 * does not come leaves zeros.
 */
#define RBG       DT_REG_ADDR(DT_INST(0, telink_b87_rbg))
#define RBG_CR    (RBG + 0x00U)
#define RBG_RTCR  (RBG + 0x04U)
#define RBG_SR    (RBG + 0x08U)
#define RBG_DR    (RBG + 0x0cU)
#define RST2      0x00800062U
#define CLK_EN2   0x00800065U
#define RBG_POLLS 10000U

#if defined(CONFIG_TC32_RNG_TRNG_CREDIT)
/*
 * NIST SP 800-90B 4.4 health tests on 32-bit words, for a claimed 8 bits of
 * min-entropy per word and alpha = 2^-30: Repetition Count Test cutoff 5,
 * Adaptive Proportion Test window 512, cutoff 16. A failure is kept until
 * reboot. The start-up test runs them over 1024 words before the first use.
 */
static struct {
	uint32_t rct, apt;
	uint16_t seen;
	uint8_t rct_n, apt_n, fail, started;
} hlt;

static void health(uint32_t w)
{
	if (w == hlt.rct) {
		if (++hlt.rct_n >= 5U) {
			hlt.rct_n = 5U;
			hlt.fail = 1U;
		}
	} else {
		hlt.rct = w;
		hlt.rct_n = 1U;
	}
	if (hlt.seen == 0U) {
		hlt.apt = w;
		hlt.apt_n = 1U;
	} else if (w == hlt.apt && ++hlt.apt_n >= 16U) {
		hlt.apt_n = 16U;
		hlt.fail = 1U;
	}
	hlt.seen = (hlt.seen + 1U) & 511U;
}
#endif

/* Reads n words; b NULL: through the health tests only. */
static void trng_read(uint8_t *b, size_t n)
{
	bool on = (sys_read8(CLK_EN2) & BIT(3)) != 0U;

	if (!on) {
		sys_write8(sys_read8(RST2) & ~BIT(3), RST2);
		sys_write8(sys_read8(CLK_EN2) | BIT(3), CLK_EN2);
		sys_write8(0U, RBG_CR);
		sys_write32(0U, RBG_RTCR);
		sys_write8(0x1fU, RBG_CR);
	}
	while (n-- > 0U) {
		uint32_t polls = RBG_POLLS;
		uint32_t w;

		while ((sys_read8(RBG_SR) & BIT(0)) == 0U) {
			if (--polls == 0U) {
				IF_ENABLED(CONFIG_TC32_RNG_TRNG_CREDIT, (hlt.fail = 1U;))
				goto out;
			}
		}
		w = sys_read32(RBG_DR);
		IF_ENABLED(CONFIG_TC32_RNG_TRNG_CREDIT, (health(w);))
		if (b != NULL) {
			memcpy(b, &w, 4);
			b += 4;
		}
	}
out:
	if (!on) {
		sys_write8(0U, RBG_CR);
		sys_write8(sys_read8(CLK_EN2) & ~BIT(3), CLK_EN2);
	}
}

/* Absorbs TC32_RNG_TRNG_BYTES of the TRNG's output. */
static void trng(void)
{
	uint8_t b[16];

#if defined(CONFIG_TC32_RNG_TRNG_CREDIT)
	if (hlt.started == 0U) {
		hlt.started = 1U;
		trng_read(NULL, 1024);
	}
#endif
	for (int i = 0; i < CONFIG_TC32_RNG_TRNG_BYTES; i += 16) {
		memset(b, 0, sizeof(b));
		trng_read(b, 4);
		absorb(b);
	}
#if defined(CONFIG_TC32_RNG_TRNG_CREDIT)
	if (hlt.fail == 0U) {
		credit(CONFIG_TC32_RNG_TRNG_BYTES * 16U);
	}
#endif
}
#else
static void trng(void)
{
}
#endif

/* The TRNG, jitter, the ADC and the event accumulator; then a reseed. */
static void refresh(void)
{
	trng();
	(void)jitter(CONFIG_TC32_RNG_RESEED_JITTER);
	(void)adc(RESEED_ADC);
	reseed();
}

/*
 * The record's check value, the first 4 bytes of a CBC-MAC under K over the
 * device ID and r[0..15] (r[15] says whether a ready generator made it), to
 * r[16..19]; returns 0 if r held it.
 */
static int seed_check(uint8_t r[TC32_RNG_SEED_SIZE])
{
	uint8_t x[16] = {0};
	uint8_t id[16] = {0};
	int bad;

	(void)tc32_rng_device_id(id);
	cbc(KEY, x, id);
	cbc(KEY, x, r);
	bad = memcmp(x, &r[16], 4);
	memcpy(&r[16], x, 4);
	return bad;
}

/* A replacement record from the generator. */
static int seed_out(uint8_t credited)
{
	uint8_t r[TC32_RNG_SEED_SIZE] = {0};

	gen(r, 15);
	r[15] = credited;
	(void)seed_check(r);
	return tc32_rng_seed_store(r);
}

static void start(void)
{
	uint8_t r[TC32_RNG_SEED_SIZE] = {0};
	bool ok;

	tc32_aes128_encrypt(KEY, &kat[16], r);
	if (memcmp(r, &kat[32], 16) != 0 IF_ENABLED(CONFIG_TC32_RNG_TEST, (|| rng.kat_fault))) {
		rng.state = FAILED;
		return;
	}
	memcpy(rng.k, KEY, 16);
	memset(r, 0, 16);
	(void)tc32_rng_device_id(r);
	absorb(r);
	{
		uint32_t t[4] = {sys_read32(STIMER), sys_read32(K32_READ), sys_read32(K32_CAL)};

		absorb(t);
	}
	ok = tc32_rng_seed_load(r) == 0 && seed_check(r) == 0 && r[15] == 1U;
	IF_ENABLED(CONFIG_TC32_RNG_READINESS, (rng.readiness.record = ok;))
	absorb(r);
	refresh();
	if (seed_out(ok || rng.credit >= FULL) == 0 && ok) {
		credit(FULL);
	}
	if (rng.credit >= FULL) {
		rng.state = READY;
	}
}

/*
 * 0 once ready; while not, a round of a chunk of jitter and a window of ADC
 * codes (wait: rounds until ready or TC32_RNG_COLLECT_TIMEOUT_MS). A round
 * in which the jitter lost an edge (the 32 kHz count stopped) and the ADC
 * credited nothing ends it at once. What was taken keeps its credit.
 */
static int ready(bool wait)
{
	uint32_t t0 = sys_read32(STIMER);

	while (rng.state == NOT_READY) {
		uint32_t got, codes;

		trng();
		got = jitter(CONFIG_TC32_RNG_JITTER_CHUNK);
		codes = adc(ADC_CHUNK);
		if (rng.credit >= FULL) {
			reseed();
			rng.state = READY;
			(void)seed_out(1U);
		} else if (got < CONFIG_TC32_RNG_JITTER_CHUNK && codes == 0U) {
			return -ETIMEDOUT;
		} else if (!wait) {
			return -EAGAIN;
		} else if (sys_read32(STIMER) - t0 >= COLLECT_TICKS) {
			return -ETIMEDOUT;
		}
	}
	return rng.state == READY ? 0 : -EIO;
}

static int get(void *dst, size_t len, bool key)
{
	int err = ready(false);

	if (err == 0) {
		if (key || (uint8_t)(SECONDS() - rng.last) >= CONFIG_TC32_RNG_RESEED_INTERVAL) {
			refresh();
		}
		gen(dst, len);
	}
	return err;
}

int tc32_rng_get(void *dst, size_t len)
{
	return get(dst, len, false);
}

int tc32_rng_get_key_material(void *dst, size_t len)
{
	return get(dst, len, true);
}

void tc32_rng_fill(void *dst, size_t len)
{
	gen(dst, len);
}

void tc32_rng_add_sample(uint32_t value, uint8_t source)
{
	uint8_t key = lock();

	rng.ev = (((rng.ev << 7) | (rng.ev >> 25)) ^ value ^ ((uint32_t)source << 24)) *
		 0x9e3779b1U;
	unlock(key);
}

void tc32_rng_add_event(uint32_t timestamp, uint8_t source)
{
	tc32_rng_add_sample(timestamp, source);
	if (CONFIG_TC32_RNG_EVENT_CREDIT > 0) {
		uint8_t key = lock();

		credit(CONFIG_TC32_RNG_EVENT_CREDIT);
		unlock(key);
	}
}

int tc32_rng_collect(void)
{
	return ready(true);
}

__weak int tc32_rng_seed_load(uint8_t record[TC32_RNG_SEED_SIZE])
{
	ARG_UNUSED(record);
	return -ENOENT;
}

__weak int tc32_rng_seed_store(const uint8_t record[TC32_RNG_SEED_SIZE])
{
	ARG_UNUSED(record);
	return -ENOTSUP;
}

__weak int tc32_rng_device_id(uint8_t id[16])
{
	ARG_UNUSED(id);
	return -ENOTSUP;
}

__weak int tc32_rng_adc_lock(void)
{
	return 0;
}

__weak void tc32_rng_adc_unlock(void)
{
}

#if defined(CONFIG_TC32_RNG_JITTER_DUMP)
uint32_t tc32_rng_jitter_samples(uint8_t *buf, uint32_t n)
{
	rng.dump = buf;
	(void)jitter(n);
	n = (uint32_t)(rng.dump - buf);
	rng.dump = NULL;
	return n;
}
#endif

#if defined(CONFIG_TC32_RNG_ADC_DUMP)
uint32_t tc32_rng_adc_samples(uint16_t *buf, uint32_t n)
{
	rng.adc_dump = buf;
	(void)adc(n);
	n = (uint32_t)(rng.adc_dump - buf);
	rng.adc_dump = NULL;
	return n;
}
#endif

#if defined(CONFIG_TC32_RNG_READINESS)
void tc32_rng_get_readiness(struct tc32_rng_readiness *r)
{
	*r = rng.readiness;
	r->state = rng.state;
	r->credit_bits = (uint8_t)(rng.credit / 16U);
}
#endif

#if defined(CONFIG_TC32_RNG_TEST)
void tc32_rng_get_status(struct tc32_rng_status *st)
{
	st->state = rng.state;
	st->credit_bits = (uint8_t)(rng.credit / 16U);
	st->reseeds = rng.reseeds;
	st->samples = rng.samples;
	st->counted = rng.counted;
#if defined(CONFIG_TC32_RNG_ADC)
	st->adc_samples = rng.adc_samples;
	st->adc_counted = rng.adc_counted;
	st->adc_failed = rng.adc_failed;
#endif
	memcpy(st->key, rng.g, 16);
	memcpy(st->pool_key, rng.k, 16);
}

int tc32_rng_reinit(uint8_t kat_fault)
{
	memset(&rng, 0, sizeof(rng));
	IF_ENABLED(CONFIG_TC32_RNG_TRNG_CREDIT, (memset(&hlt, 0, sizeof(hlt));))
	rng.kat_fault = kat_fault;
	start();
	return rng.state;
}
#endif

static int tc32_rng_init(void)
{
	start();
	return 0;
}

SYS_INIT(tc32_rng_init, POST_KERNEL, CONFIG_TC32_RNG_INIT_PRIORITY);
