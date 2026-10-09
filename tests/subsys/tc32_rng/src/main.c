/*
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * tc32_rng in TC32-devtools' emulator. The emulator's register 0xfff4 sets
 * the TRNG's source (healthy, stuck, biased, a short cycle, a sequence an
 * attacker knows, never ready), 0xfff8 the 32 kHz oscillator's period jitter
 * in ns and 0xfffc that jitter's seed, 0xffe8 the ADC's codes on VBAT
 * (noise, or a fault: none, stuck, biased, a ramp, two values, a sequence an
 * attacker chose, no conversions, noise around another level); none exists
 * on the chip, so this suite runs in the emulator only (with the AES block
 * it models). Each test starts with the ADC's default noise (2 LSB rms) and
 * the 32 kHz count running with 50 ns of period jitter.
 *
 * The seed record, device ID and ADC lock hooks are this file's: a record
 * kept in RAM, with switches for a failing store, another device's ID and
 * an ADC that is busy.
 */

#include <string.h>

#include <zephyr/crypto/tc32_aes.h>
#include <zephyr/random/tc32_rng.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#define EMU_RBG    0x0080fff4U
#define EMU_JITTER 0x0080fff8U
#define EMU_JSEED  0x0080fffcU
#define EMU_ADC    0x0080ffe8U
#define K32_STOP   BIT(31)

enum {
	ADC_NOISE,
	ADC_OFF,
	ADC_STUCK,
	ADC_BIASED,
	ADC_RAMP,
	ADC_TWO,
	ADC_ATTACKER,
	ADC_NEVER,
	ADC_LEVEL,
};

enum {
	RBG_LCG,
	RBG_HEALTHY,
	RBG_STUCK,
	RBG_BIASED,
	RBG_CYCLE,
	RBG_ATTACKER,
	RBG_NEVER,
};

#define NOT_READY 0
#define READY     1
#define FAILED    2
#define SPB       CONFIG_TC32_RNG_JITTER_SAMPLES_PER_BIT
#define CREDIT_TRNG IS_ENABLED(CONFIG_TC32_RNG_TRNG_CREDIT)
#define HAVE_ADC    IS_ENABLED(CONFIG_TC32_RNG_ADC)
#if defined(CONFIG_TC32_RNG_ADC)
#define ADC_SPB CONFIG_TC32_RNG_ADC_SAMPLES_PER_BIT
#else
#define ADC_SPB 16
#endif

static uint8_t stored[TC32_RNG_SEED_SIZE];
static bool have_stored;
static bool store_fails;
static uint32_t stores;
static uint8_t dev_id = 0x5a;

int tc32_rng_seed_load(uint8_t record[TC32_RNG_SEED_SIZE])
{
	if (!have_stored) {
		return -ENOENT;
	}
	memcpy(record, stored, sizeof(stored));
	return 0;
}

int tc32_rng_seed_store(const uint8_t record[TC32_RNG_SEED_SIZE])
{
	if (store_fails) {
		return -EIO;
	}
	memcpy(stored, record, sizeof(stored));
	have_stored = true;
	stores++;
	return 0;
}

int tc32_rng_device_id(uint8_t id[16])
{
	memset(id, dev_id, 16);
	return 0;
}

static uint32_t adc_locks, adc_unlocks;
static bool adc_busy, adc_nested;

int tc32_rng_adc_lock(void)
{
	if (adc_busy) {
		return -EBUSY;
	}
	if (adc_locks != adc_unlocks) {
		adc_nested = true;
	}
	adc_locks++;
	return 0;
}

void tc32_rng_adc_unlock(void)
{
	adc_unlocks++;
}

static void emu_rbg(uint32_t mode, uint32_t param)
{
	sys_write32(mode | (param << 8), EMU_RBG);
}

static void emu_adc(uint32_t mode, uint32_t param)
{
	sys_write32(mode | (param << 8), EMU_ADC);
}

static void status(struct tc32_rng_status *st, const char *what)
{
	tc32_rng_get_status(st);
	TC_PRINT("%s: state %u, credit %u bits, reseeds %u, jitter samples %u (counted %u), "
		 "ADC codes %u (counted %u, windows failed %u)\n",
		 what, st->state, st->credit_bits, st->reseeds, st->samples, st->counted,
		 st->adc_samples, st->adc_counted, st->adc_failed);
}

static uint32_t ms_since(uint32_t t0)
{
	return (k_cycle_get_32() - t0) / 16000U;
}

static void hex(const char *what, const uint8_t *b, size_t n)
{
	TC_PRINT("%s:", what);
	for (size_t i = 0; i < n; i++) {
		TC_PRINT("%02x", b[i]);
	}
	TC_PRINT("\n");
}

static int hamming(const uint8_t *a, const uint8_t *b, size_t n)
{
	int d = 0;

	for (size_t i = 0; i < n; i++) {
		d += __builtin_popcount(a[i] ^ b[i]);
	}
	return d;
}

/* A boot without a usable seed record: init, then requests until ready. */
static int boot_and_wait(void)
{
	uint8_t k[16];
	int calls = 0;

	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	while (tc32_rng_get(k, sizeof(k)) == -EAGAIN) {
		calls++;
		zassert_true(calls < 64, "never ready");
	}
	return calls;
}

static void *setup(void)
{
	struct tc32_rng_status st;
	uint8_t k[16];
	int err;

	/* What the boot did, with the emulator's settings from its environment. */
	status(&st, "boot");
	err = tc32_rng_get_key_material(k, sizeof(k));
	TC_PRINT("boot: first key request %d\n", err);
	zassert_equal(tc32_rng_collect(), 0);
	zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), 0);
	hex("boot key", k, sizeof(k));
	status(&st, "boot, after collect");
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	emu_adc(ADC_NOISE, 0U);
	sys_write32(50U, EMU_JITTER);
	adc_busy = false;
}

/* AES-128 (FIPS-197 Appendix C.1) through tc32_aes128_encrypt(), and the
 * known-answer test at init: made to fail, nothing is given out.
 */
ZTEST(tc32_rng, test_01_kat)
{
	static const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
	static const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
				       0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	static const uint8_t ct[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
				       0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
	uint8_t b[16];

	tc32_aes128_encrypt(key, pt, b);
	zassert_mem_equal(b, ct, 16);
	zassert_equal(tc32_rng_reinit(1U), FAILED);
	zassert_equal(tc32_rng_get(b, sizeof(b)), -EIO);
	zassert_equal(tc32_rng_get_key_material(b, sizeof(b)), -EIO);
	zassert_equal(tc32_rng_collect(), -EIO);
	(void)boot_and_wait();
}

/* A first boot (no seed record): not ready until the jitter is in; then
 * a credited record is stored.
 */
ZTEST(tc32_rng, test_02_first_boot)
{
	struct tc32_rng_status st;
	uint8_t k[16];
	int calls;

	emu_rbg(RBG_HEALTHY, 3U);
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	status(&st, "first boot, init");
	zassert_true(have_stored && stored[15] == 0U, "init stored no uncredited record");
	if (!CREDIT_TRNG) {
		zassert_equal(tc32_rng_get(k, sizeof(k)), -EAGAIN);
		zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), -EAGAIN);
	}
	calls = boot_and_wait();
	status(&st, "first boot, ready");
	TC_PRINT("first boot: ready after %d refused requests\n", calls);
	zassert_equal(st.credit_bits, 128U);
	zassert_true(have_stored && stored[15] == 1U, "no credited record stored");
	zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), 0);
}

/* A valid record: ready at once, and a different record every boot. */
ZTEST(tc32_rng, test_03_valid_seed)
{
	struct tc32_rng_status st;
	uint8_t seen[4][16];

	emu_rbg(RBG_STUCK, 0U);
	store_fails = false;
	have_stored = false;
	(void)boot_and_wait();
	for (int i = 0; i < 4; i++) {
		memcpy(seen[i], stored, 16);
		zassert_equal(tc32_rng_reinit(0U), READY, "boot %d not ready at once", i);
		for (int j = 0; j <= i; j++) {
			zassert_true(memcmp(seen[j], stored, 16) != 0, "a record came back");
		}
	}
	status(&st, "valid seed");
	zassert_equal(st.credit_bits, 128U);
}

/* Damaged, another device's, not storable, or made by a generator that was
 * not ready: not credited.
 */
ZTEST(tc32_rng, test_04_seed_not_credited)
{
	emu_rbg(RBG_STUCK, 0U);
	store_fails = false;
	have_stored = false;
	(void)boot_and_wait();

	stored[3] ^= 0x01U;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY, "damaged record credited");

	(void)boot_and_wait();
	dev_id = 0x77;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY, "another device's record credited");
	dev_id = 0x5a;

	(void)boot_and_wait();
	store_fails = true;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY, "a record not replaced credited");
	store_fails = false;

	/* The record init stores before the generator is ready says so. */
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	zassert_true(have_stored && stored[15] == 0U);
	zassert_equal(tc32_rng_reinit(0U), NOT_READY, "an uncredited record credited");
}

/* The TRNG stuck, or giving a sequence an attacker knows: refused until the
 * jitter is in, and then keys of two boots that differ only in the jitter
 * are unrelated.
 */
static void boot_with(uint32_t mode, uint32_t jitter_seed, uint8_t key[16])
{
	emu_rbg(mode, 0x1234U);
	sys_write32(jitter_seed, EMU_JSEED);
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	if (!(CREDIT_TRNG && mode == RBG_ATTACKER)) {
		zassert_equal(tc32_rng_get_key_material(key, 16), -EAGAIN);
	}
	zassert_equal(tc32_rng_collect(), 0);
	zassert_equal(tc32_rng_get_key_material(key, 16), 0);
}

ZTEST(tc32_rng, test_05_bad_trng)
{
	uint8_t a[16], b[16];
	int d;

	boot_with(RBG_STUCK, 11U, a);
	boot_with(RBG_STUCK, 12U, b);
	d = hamming(a, b, 16);
	TC_PRINT("TRNG stuck, different jitter: %d of 128 bits differ\n", d);
	zassert_true(d >= 32 && d <= 96);
	boot_with(RBG_ATTACKER, 11U, a);
	boot_with(RBG_ATTACKER, 12U, b);
	d = hamming(a, b, 16);
	hex("attacker's TRNG, key A", a, 16);
	hex("attacker's TRNG, key B", b, 16);
	TC_PRINT("attacker's TRNG, different jitter: %d of 128 bits differ\n", d);
	zassert_true(d >= 32 && d <= 96);
}

/* Fast key erasure: a request's output is AES under the old key of counter
 * blocks 1 and 2, the new key AES of block 0; the new key gives back
 * neither the output nor the old key.
 */
ZTEST(tc32_rng, test_06_fast_key_erasure)
{
	struct tc32_rng_status before, after;
	uint8_t out[32], c[16] = {0}, x[16];

	emu_rbg(RBG_HEALTHY, 5U);
	(void)tc32_rng_collect();
	tc32_rng_get_status(&before);
	tc32_rng_fill(out, sizeof(out));
	tc32_rng_get_status(&after);
	for (uint8_t i = 0; i < 3; i++) {
		c[0] = i;
		tc32_aes128_encrypt(before.key, c, x);
		zassert_mem_equal(x, i == 0 ? after.key : &out[16 * (i - 1)], 16,
				  "block %u is not the construction's", i);
	}
	for (uint8_t i = 0; i < 4; i++) {
		c[0] = i;
		tc32_aes128_encrypt(after.key, c, x);
		zassert_true(memcmp(x, out, 16) != 0 && memcmp(x, &out[16], 16) != 0 &&
			     memcmp(x, before.key, 16) != 0,
			     "the new key reproduces the output or the old key");
	}
	zassert_true(memcmp(after.key, before.key, 16) != 0);
	zassert_true(memcmp(after.key, out, 16) != 0 && memcmp(after.key, &out[16], 16) != 0);
}

/* tc32_rng_get() reseeds once the interval has passed, key requests always. */
ZTEST(tc32_rng, test_07_reseeds)
{
	struct tc32_rng_status a, b, c;
	uint8_t k[16];

	(void)tc32_rng_collect();
	zassert_equal(tc32_rng_get(k, sizeof(k)), 0);
	tc32_rng_get_status(&a);
	zassert_equal(tc32_rng_get(k, sizeof(k)), 0);
	tc32_rng_get_status(&b);
	zassert_equal(b.reseeds, a.reseeds, "reseeded before the interval");
	k_sleep(K_MSEC(1100 * CONFIG_TC32_RNG_RESEED_INTERVAL));
	zassert_equal(tc32_rng_get(k, sizeof(k)), 0);
	tc32_rng_get_status(&c);
	zassert_equal(c.reseeds, b.reseeds + 1U, "no reseed after the interval");
	zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), 0);
	zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), 0);
	tc32_rng_get_status(&a);
	zassert_equal(a.reseeds, c.reseeds + 2U, "a key request did not reseed");
}

/* Events and samples: mixed in, credited with nothing by default. */
ZTEST(tc32_rng, test_08_events)
{
	struct tc32_rng_status a, b;

	tc32_rng_get_status(&a);
	for (uint32_t i = 0; i < 100U; i++) {
		tc32_rng_add_event(k_cycle_get_32(), TC32_RNG_SRC_KEY);
		tc32_rng_add_sample(i * 7U, TC32_RNG_SRC_RADIO_RSSI);
	}
	tc32_rng_get_status(&b);
	zassert_equal(b.credit_bits, a.credit_bits);
}

/* The pool key: not the public K0 after init, replaced at every reseed, and
 * different for two boots with the same device ID, seed record and TRNG
 * words that differ only in the jitter or only in the timers.
 */
static void boot_same(uint32_t jitter_ns, uint32_t jitter_seed,
		      const uint8_t rec[TC32_RNG_SEED_SIZE], uint8_t pool_key[16])
{
	struct tc32_rng_status st;

	sys_write32(jitter_ns, 0x0080fff8U);
	sys_write32(jitter_seed, EMU_JSEED);
	emu_rbg(RBG_ATTACKER, 0x42U);
	memcpy(stored, rec, sizeof(stored));
	have_stored = true;
	store_fails = false;
	zassert_equal(tc32_rng_reinit(0U), READY);
	tc32_rng_get_status(&st);
	memcpy(pool_key, st.pool_key, 16);
}

ZTEST(tc32_rng, test_12_pool_key)
{
	static const uint8_t k0[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
				       0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
	struct tc32_rng_status a, b;
	uint8_t rec[TC32_RNG_SEED_SIZE];
	uint8_t k1[16], k2[16], k3[16], k4[16];
	uint8_t k[16];

	have_stored = false;
	(void)boot_and_wait();
	memcpy(rec, stored, sizeof(rec));

	boot_same(50U, 21U, rec, k1);
	boot_same(50U, 22U, rec, k2);
	hex("pool key, jitter seed 21", k1, 16);
	hex("pool key, jitter seed 22", k2, 16);
	zassert_true(memcmp(k1, k0, 16) != 0, "the pool key is still K0");
	zassert_true(hamming(k1, k2, 16) >= 32, "jitter alone does not change the pool key");

	/* No jitter at all: only the timers differ between the two boots. */
	boot_same(0U, 21U, rec, k3);
	k_sleep(K_MSEC(3));
	boot_same(0U, 21U, rec, k4);
	hex("pool key, no jitter, t", k3, 16);
	hex("pool key, no jitter, t + 3 ms", k4, 16);
	zassert_true(hamming(k3, k4, 16) >= 32, "the timers alone do not change the pool key");

	tc32_rng_get_status(&a);
	zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), 0);
	tc32_rng_get_status(&b);
	zassert_true(memcmp(a.pool_key, b.pool_key, 16) != 0, "a reseed kept the pool key");
	sys_write32(50U, 0x0080fff8U);
}

/* With TC32_RNG_TRNG_CREDIT: a healthy TRNG makes the first request
 * succeed on a first boot; a failing one is credited with nothing.
 */
ZTEST(tc32_rng, test_09_trng_credit)
{
	struct tc32_rng_status st;
	uint8_t k[16];

	if (!CREDIT_TRNG) {
		ztest_test_skip();
	}
	emu_rbg(RBG_HEALTHY, 9U);
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	status(&st, "healthy TRNG, init");
	zassert_equal(tc32_rng_get(k, sizeof(k)), 0, "a healthy TRNG not credited");
	status(&st, "healthy TRNG, first request");
	emu_rbg(RBG_STUCK, 0U);
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	zassert_equal(tc32_rng_get(k, sizeof(k)), -EAGAIN, "a stuck TRNG credited");
	status(&st, "stuck TRNG, first request");
}

/* The stack a key request needs in the calling thread: a thread on a painted
 * stack makes one and another of the same entry makes none.
 */
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(probe_stack, STACK_SIZE);
static struct k_thread probe;

/* Below a 256-byte frame, deeper than the thread's first frame, so that
 * the request's own depth shows in the high water mark.
 */
static __noinline void probe_deep(int *ret)
{
	volatile uint8_t pad[256];
	uint8_t k[16];

	pad[0] = 1U;
	pad[255] = 1U;
	if (ret != NULL) {
		*ret = tc32_rng_get_key_material(k, sizeof(k));
	}
}

static void probe_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	probe_deep(p1);
}

static size_t probe_used(int *ret)
{
	size_t unused = 0U;

	k_thread_create(&probe, probe_stack, STACK_SIZE, probe_entry, ret, NULL, NULL,
			K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	k_thread_join(&probe, K_FOREVER);
	zassert_equal(k_thread_stack_space_get(&probe, &unused), 0);
	return STACK_SIZE - unused;
}

ZTEST(tc32_rng, test_10_stack)
{
	int ret = 1;
	size_t base, key, worst = 0U;

	(void)tc32_rng_collect();
	base = probe_used(NULL);
	for (int i = 0; i < 8; i++) {
		key = probe_used(&ret);
		zassert_equal(ret, 0);
		worst = MAX(worst, key);
	}
	TC_PRINT("stack: thread entry %u B, with a key request up to %u B: "
		 "the request needs %u B\n",
		 (unsigned int)base, (unsigned int)worst, (unsigned int)(worst - base));
}

/* The measurement hook: one byte per 32 kHz edge. */
ZTEST(tc32_rng, test_11_jitter_dump)
{
	static uint8_t b[257];
	uint32_t distinct = 0U;
	bool seen[256] = {false};

	zassert_equal(tc32_rng_jitter_samples(b, 256), 257U);
	for (int i = 0; i < 257; i++) {
		if (!seen[b[i]]) {
			seen[b[i]] = true;
			distinct++;
		}
	}
	hex("jitter bytes", b, 24);
	TC_PRINT("256 samples: %u distinct byte values\n", distinct);
	zassert_true(distinct > 16U);
}

/* The 32 kHz count stopped and the ADC without noise: collect and requests
 * give up after one round (4 of the count's periods without an edge, and
 * an ADC window that fails: about 11 ms, 11.6 ms with software AES), credit
 * nothing, leave the generator not ready; once the count goes again,
 * collect gets ready.
 */
ZTEST(tc32_rng, test_13_collect_stopped)
{
	struct tc32_rng_status st;
	uint8_t k[16];
	uint32_t t0, us;
	int err;

	emu_rbg(RBG_HEALTHY, 4U);
	emu_adc(ADC_OFF, 0U);
	have_stored = false;
	sys_write32(K32_STOP | 50U, EMU_JITTER);
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	status(&st, "32 kHz count stopped, init");
	t0 = k_cycle_get_32();
	err = tc32_rng_collect();
	us = (k_cycle_get_32() - t0) / 16U;
	TC_PRINT("32 kHz count stopped: collect %d after %u us\n", err, us);
	if (CREDIT_TRNG) {
		zassert_equal(err, 0, "the TRNG's credit did not make it ready");
	} else {
		zassert_equal(err, -ETIMEDOUT);
		zassert_true(us < 2000U + (HAVE_ADC ? 13000U : 0U), "the stop rule took %u us", us);
		t0 = k_cycle_get_32();
		zassert_equal(tc32_rng_get(k, sizeof(k)), -ETIMEDOUT);
		zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), -ETIMEDOUT);
		us = (k_cycle_get_32() - t0) / 16U;
		TC_PRINT("32 kHz count stopped: two requests gave up after %u us\n", us);
		zassert_true(us < 4000U + (HAVE_ADC ? 26000U : 0U));
		status(&st, "32 kHz count stopped, after the attempts");
		zassert_equal(st.state, NOT_READY);
		zassert_equal(st.credit_bits, 0U, "credit from a stopped count");
	}
	sys_write32(50U, EMU_JITTER);
	zassert_equal(tc32_rng_collect(), 0, "not ready once the count goes again");
}

/* Held up while it collects: a timer interrupt that busy-waits 200 us
 * (more than 4 periods of the 32 kHz count) every 3 ms while the count runs,
 * the ADC without noise. An edge the wait sees only late is a missed edge,
 * not a stopped count: collect goes on and gets ready on the jitter within
 * its time limit.
 */
static void hold_up(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_busy_wait(200);
}

ZTEST(tc32_rng, test_14_collect_held_up)
{
	static struct k_timer timer;
	struct tc32_rng_status st;
	uint32_t t0, ms;
	int err;

	emu_rbg(RBG_HEALTHY, 4U);
	emu_adc(ADC_OFF, 0U);
	have_stored = false;
	sys_write32(50U, EMU_JITTER);
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	k_timer_init(&timer, hold_up, NULL);
	k_timer_start(&timer, K_MSEC(3), K_MSEC(3));
	t0 = k_cycle_get_32();
	err = tc32_rng_collect();
	ms = (k_cycle_get_32() - t0) / 16000U;
	k_timer_stop(&timer);
	tc32_rng_get_status(&st);
	TC_PRINT("held up 200 us every 3 ms: collect %d after %u ms, %u samples counted\n", err, ms,
		 st.counted);
	zassert_equal(err, 0, "a collection held up by an interrupt gave up");
	zassert_equal(st.state, READY);
	zassert_true(ms < CONFIG_TC32_RNG_COLLECT_TIMEOUT_MS, "collect took %u ms", ms);
}

/* A first boot's collect (no record, the TRNG stuck): its result, its time
 * in ms, and the status after it.
 */
static int first_collect(uint32_t *ms, struct tc32_rng_status *st)
{
	uint32_t t0;
	int err;

	emu_rbg(RBG_STUCK, 0U);
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	t0 = k_cycle_get_32();
	err = tc32_rng_collect();
	*ms = ms_since(t0);
	tc32_rng_get_status(st);
	return err;
}

/* The 32 kHz count stopped and the ADC's noise in: ready from the ADC
 * alone, every credited bit from windows that passed.
 */
ZTEST(tc32_rng, test_15_adc_alone)
{
	struct tc32_rng_status st;
	uint32_t ms;
	int err;

	if (!HAVE_ADC) {
		ztest_test_skip();
	}
	sys_write32(K32_STOP | 50U, EMU_JITTER);
	err = first_collect(&ms, &st);
	sys_write32(50U, EMU_JITTER);
	status(&st, "32 kHz count stopped, ADC noise");
	TC_PRINT("ADC alone: collect %d after %u ms\n", err, ms);
	zassert_equal(err, 0, "the ADC alone did not make it ready");
	zassert_equal(st.state, READY);
	zassert_equal(st.counted, 0U, "jitter counted from a stopped count");
	zassert_true(st.adc_counted >= 128U * ADC_SPB);
	zassert_equal(st.adc_failed, 0U);
	zassert_false(adc_nested, "the ADC lock was taken twice");
	zassert_equal(adc_locks, adc_unlocks);
}

/* The 32 kHz count stopped and the ADC broken: no ADC fault reaches credit
 * (a constant, a stuck code, mostly one code, ramps fast and slow, two
 * values, too little noise), the codes are taken all the same, and collect
 * and requests give up at once; with no conversions at all the same.
 */
ZTEST(tc32_rng, test_16_adc_faults)
{
	static const struct {
		const char *name;
		uint32_t mode, param;
	} faults[] = {
		{"no noise", ADC_OFF, 0U},     {"stuck", ADC_STUCK, 0U},
		{"biased", ADC_BIASED, 0U},    {"ramp 1/1", ADC_RAMP, 1U},
		{"ramp 1/4", ADC_RAMP, 4U},    {"ramp 1/64", ADC_RAMP, 64U},
		{"two values", ADC_TWO, 1U},   {"two values 3 apart", ADC_TWO, 3U},
		{"0.5 LSB noise", ADC_NOISE, 5U}, {"no conversions", ADC_NEVER, 0U},
	};
	struct tc32_rng_status st;
	uint8_t k[16];
	uint32_t ms;
	int err;

	if (!HAVE_ADC || CREDIT_TRNG) {
		ztest_test_skip();
	}
	sys_write32(K32_STOP | 50U, EMU_JITTER);
	for (size_t i = 0; i < ARRAY_SIZE(faults); i++) {
		emu_adc(faults[i].mode, faults[i].param);
		err = first_collect(&ms, &st);
		TC_PRINT("count stopped, ADC %s: collect %d after %u ms, credit %u bits, ADC codes "
			 "%u (counted %u, windows failed %u)\n",
			 faults[i].name, err, ms, st.credit_bits, st.adc_samples, st.adc_counted,
			 st.adc_failed);
		zassert_equal(err, -ETIMEDOUT, "%s: collect %d", faults[i].name, err);
		zassert_equal(st.adc_counted, 0U, "%s reached credit", faults[i].name);
		zassert_equal(st.credit_bits, 0U, "%s: credit", faults[i].name);
		zassert_equal(st.state, NOT_READY);
		zassert_true(ms < 15U, "%s: gave up after %u ms", faults[i].name, ms);
		if (faults[i].mode == ADC_NEVER) {
			zassert_equal(st.adc_samples, 0U);
		} else {
			zassert_true(st.adc_samples >= 512U && st.adc_failed >= 1U,
				     "%s: the codes were not taken", faults[i].name);
		}
		zassert_equal(tc32_rng_get(k, sizeof(k)), -ETIMEDOUT);
		zassert_equal(tc32_rng_get_key_material(k, sizeof(k)), -ETIMEDOUT);
	}
	sys_write32(50U, EMU_JITTER);
	zassert_equal(adc_locks, adc_unlocks);
}

/* The ADC stuck and the 32 kHz count running: ready from the jitter alone. */
ZTEST(tc32_rng, test_17_adc_stuck_jitter)
{
	struct tc32_rng_status st;
	uint32_t ms;
	int err;

	if (!HAVE_ADC) {
		ztest_test_skip();
	}
	emu_adc(ADC_STUCK, 0U);
	err = first_collect(&ms, &st);
	status(&st, "ADC stuck, jitter");
	TC_PRINT("ADC stuck, jitter: collect %d after %u ms\n", err, ms);
	zassert_equal(err, 0);
	zassert_equal(st.adc_counted, 0U, "a stuck ADC reached credit");
	zassert_true(st.counted >= 128U * SPB);
}

/* The ADC giving codes an attacker chose (they pass the health tests and
 * are credited): keys of two boots that differ only in the jitter are
 * unrelated.
 */
ZTEST(tc32_rng, test_18_adc_attacker)
{
	struct tc32_rng_status st;
	uint8_t a[16], b[16];
	int d;

	if (!HAVE_ADC) {
		ztest_test_skip();
	}
	for (int i = 0; i < 2; i++) {
		emu_adc(ADC_ATTACKER, 0x1234U);
		boot_with(RBG_STUCK, 11U + (uint32_t)i, i == 0 ? a : b);
		status(&st, i == 0 ? "attacker's ADC, jitter seed 11"
				    : "attacker's ADC, jitter seed 12");
	}
	d = hamming(a, b, 16);
	hex("attacker's ADC, key A", a, 16);
	hex("attacker's ADC, key B", b, 16);
	TC_PRINT("attacker's ADC, different jitter: %d of 128 bits differ\n", d);
	zassert_true(st.adc_counted > 0U, "the attacker's codes were not credited");
	zassert_true(d >= 32 && d <= 96);
}

/* The time a first boot's collect takes to readiness: both sources, the ADC
 * alone (the 32 kHz count stopped), the jitter alone (the ADC without
 * noise).
 */
ZTEST(tc32_rng, test_19_ready_times)
{
	struct tc32_rng_status st;
	uint32_t both, adc_ms, jit;

	if (!HAVE_ADC || CREDIT_TRNG) {
		ztest_test_skip();
	}
	zassert_equal(first_collect(&both, &st), 0);
	TC_PRINT("ready: both sources %u ms (jitter %u, ADC %u codes counted)\n", both,
		 st.counted, st.adc_counted);
	sys_write32(K32_STOP | 50U, EMU_JITTER);
	zassert_equal(first_collect(&adc_ms, &st), 0);
	sys_write32(50U, EMU_JITTER);
	TC_PRINT("ready: ADC alone %u ms (%u codes counted)\n", adc_ms, st.adc_counted);
	emu_adc(ADC_OFF, 0U);
	zassert_equal(first_collect(&jit, &st), 0);
	TC_PRINT("ready: jitter alone %u ms (%u samples counted)\n", jit, st.counted);
	zassert_true(both < jit && adc_ms < jit);
}

/* The ADC is the application's: an ADC that is busy gives no codes (and the
 * jitter makes it ready); every analog and DFIFO2 setting the source
 * changes is back as it was afterwards.
 */
static uint8_t ana(uint8_t addr, int value)
{
	uint8_t v = 0U;

	sys_write8(addr, 0x008000b8U);
	if (value >= 0) {
		sys_write8((uint8_t)value, 0x008000b9U);
		sys_write8(0x60U, 0x008000baU);
	} else {
		sys_write8(0x40U, 0x008000baU);
	}
	while ((sys_read8(0x008000baU) & BIT(0)) != 0U) {
	}
	if (value < 0) {
		v = sys_read8(0x008000b9U);
	}
	sys_write8(0U, 0x008000baU);
	return v;
}

ZTEST(tc32_rng, test_20_adc_shared)
{
	static const uint8_t regs[] = {0xea, 0xeb, 0xec, 0xee, 0xef, 0xf1,
				       0xf2, 0xf4, 0xf9, 0xfa, 0xfc};
	static const uint8_t battery[] = {0x02, 0x4f, 0x43, 0x01, 0xf0, 0xca,
					  0x24, 0x05, 0x13, 0xfd, 0x20};
	struct tc32_rng_status st;
	uint32_t ms;
	int err;

	if (!HAVE_ADC) {
		ztest_test_skip();
	}
	adc_busy = true;
	err = first_collect(&ms, &st);
	adc_busy = false;
	TC_PRINT("ADC busy: collect %d after %u ms, ADC codes %u\n", err, ms, st.adc_samples);
	zassert_equal(err, 0);
	zassert_equal(st.adc_samples, 0U, "codes taken from a busy ADC");

	for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
		(void)ana(regs[i], battery[i]);
	}
	sys_write16(0x1230U, 0x00800b08U);
	sys_write8(0x03U, 0x00800b0aU);
	sys_write16(0x0040U, 0x00800b1eU);
	err = first_collect(&ms, &st);
	zassert_equal(err, 0);
	zassert_true(st.adc_counted > 0U);
	for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
		uint8_t v = ana(regs[i], -1);

		zassert_equal(v, battery[i], "analog 0x%02x: 0x%02x, was 0x%02x", regs[i], v,
			      battery[i]);
	}
	zassert_equal(sys_read16(0x00800b08U), 0x1230U);
	zassert_equal(sys_read8(0x00800b0aU), 0x03U);
	zassert_equal(sys_read16(0x00800b1eU), 0x0040U);
	zassert_equal(sys_read8(0x00800b10U) & BIT(2), 0U, "DFIFO2 left on");
	zassert_equal(adc_locks, adc_unlocks);
	zassert_false(adc_nested);
	TC_PRINT("ADC settings back as they were after %u locks\n", adc_locks);
}

/* The measurement hook: the codes, around the emulator's VBAT code. */
ZTEST(tc32_rng, test_21_adc_dump)
{
#if defined(CONFIG_TC32_RNG_ADC_DUMP)
	static uint16_t c[1024];
	struct tc32_rng_status a, b;
	uint32_t lo = 0xffffU, hi = 0U, n;

	tc32_rng_get_status(&a);
	n = tc32_rng_adc_samples(c, ARRAY_SIZE(c));
	tc32_rng_get_status(&b);
	for (size_t i = 0; i < ARRAY_SIZE(c); i++) {
		lo = MIN(lo, c[i]);
		hi = MAX(hi, c[i]);
	}
	TC_PRINT("%u ADC codes: 0x%04x to 0x%04x, first %u %u %u %u, %u in passing windows\n",
		 n, lo, hi, c[0], c[1], c[2], c[3], b.adc_counted - a.adc_counted);
	zassert_equal(n, 1024U);
	zassert_equal(b.adc_counted - a.adc_counted, 1024U);
	zassert_true(hi - lo >= 6U && hi - lo < 64U);
#else
	ztest_test_skip();
#endif
}

/* An interrupt that holds the CPU 170 to 250 us every millisecond, as a
 * keyboard's backlight and USB interrupt can: longer than the DMA takes to
 * go round the 8-slot ring (8 conversions of 20.8 us), so the reader falls a
 * ring behind and then waits up to a ring for the slot it reads next.
 */
static uint32_t holds;

static void hold_cpu(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_busy_wait(170U + 20U * (holds++ % 5U));
}

K_TIMER_DEFINE(hold_timer, hold_cpu, NULL);

ZTEST(tc32_rng, test_22_adc_held_up)
{
#if defined(CONFIG_TC32_RNG_ADC_DUMP)
	static uint16_t c[1536];
	struct tc32_rng_status a, b;
	uint32_t taken, n, odd = 0U;

	tc32_rng_get_status(&a);
	holds = 0U;
	k_timer_start(&hold_timer, K_USEC(700), K_MSEC(1));
	n = tc32_rng_adc_samples(c, ARRAY_SIZE(c));
	k_timer_stop(&hold_timer);
	tc32_rng_get_status(&b);
	taken = b.adc_samples - a.adc_samples;
	for (uint32_t i = 0U; i < n; i++) {
		odd += (c[i] < 0x1d00U || c[i] > 0x1f00U) ? 1U : 0U;
	}
	TC_PRINT("held 170-250 us every ms (%u times): %u of %u ADC codes taken, %u copied, "
		 "%u counted, %u away from the VBAT code\n", holds, taken,
		 (unsigned int)ARRAY_SIZE(c), n, b.adc_counted - a.adc_counted, odd);
	zassert_equal(taken, ARRAY_SIZE(c), "the reader stopped after %u codes", taken);
	zassert_equal(n, taken);
	zassert_equal(odd, 0U);
#else
	ztest_test_skip();
#endif
}

/* Codes with noise that passes the health tests but outside VBAT's range
 * (2048, VBAT 0.9 V): copied, absorbed, credited with nothing.
 */
ZTEST(tc32_rng, test_23_adc_out_of_range)
{
#if defined(CONFIG_TC32_RNG_ADC_DUMP)
	static uint16_t c[1024];
	struct tc32_rng_status a, b;
	uint32_t n;

	emu_adc(ADC_LEVEL, 0x800U);
	tc32_rng_get_status(&a);
	n = tc32_rng_adc_samples(c, ARRAY_SIZE(c));
	tc32_rng_get_status(&b);
	TC_PRINT("noise around 0x800: %u codes copied, first %u %u %u %u, %u counted, %u windows "
		 "failed\n", n, c[0], c[1], c[2], c[3], b.adc_counted - a.adc_counted,
		 b.adc_failed - a.adc_failed);
	zassert_equal(n, ARRAY_SIZE(c));
	zassert_true(c[0] > 0x7c0U && c[0] < 0x840U);
	zassert_equal(b.adc_counted - a.adc_counted, 0U, "codes out of range credited");
	zassert_equal(b.adc_failed - a.adc_failed, 2U);
	emu_adc(ADC_LEVEL, 0x1f00U);
	tc32_rng_get_status(&a);
	n = tc32_rng_adc_samples(c, ARRAY_SIZE(c));
	tc32_rng_get_status(&b);
	TC_PRINT("noise around 0x1f00: %u codes copied, %u counted\n", n,
		 b.adc_counted - a.adc_counted);
	zassert_equal(b.adc_counted - a.adc_counted, ARRAY_SIZE(c));
#else
	ztest_test_skip();
#endif
}

/* The readiness hook: not ready after a first boot, ready after collect,
 * ready at once from a record a ready generator made; the ADC windows
 * credited, refused for codes out of range and refused by the health tests.
 */
ZTEST(tc32_rng, test_24_readiness)
{
#if defined(CONFIG_TC32_RNG_READINESS) && defined(CONFIG_TC32_RNG_ADC_DUMP)
	static uint16_t c[1024];
	struct tc32_rng_readiness r;

	if (CREDIT_TRNG) {
		ztest_test_skip();
	}
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	tc32_rng_get_readiness(&r);
	zassert_true(r.state == NOT_READY && r.record == 0U && r.adc_windows == 0U);
	zassert_equal(tc32_rng_collect(), 0);
	tc32_rng_get_readiness(&r);
	TC_PRINT("after collect: state %u, %u bits, record %u, ADC windows %u, out of range %u, "
		 "health %u\n", r.state, r.credit_bits, r.record, r.adc_windows, r.adc_out_of_range,
		 r.adc_health);
	zassert_true(r.state == READY && r.credit_bits == 128U && r.adc_windows >= 1U);
	zassert_true(r.adc_out_of_range == 0U && r.adc_health == 0U);
	emu_adc(ADC_LEVEL, 0x800U);
	(void)tc32_rng_adc_samples(c, 512U);
	emu_adc(ADC_OFF, 0U);
	(void)tc32_rng_adc_samples(c, 512U);
	emu_adc(ADC_NOISE, 0U);
	(void)tc32_rng_adc_samples(c, 1024U);
	tc32_rng_get_readiness(&r);
	TC_PRINT("then: ADC windows %u, out of range %u, health %u\n", r.adc_windows,
		 r.adc_out_of_range, r.adc_health);
	zassert_true(r.adc_out_of_range == 1U && r.adc_health == 1U);
	zassert_equal(tc32_rng_reinit(0U), READY, "the record from a ready generator");
	tc32_rng_get_readiness(&r);
	zassert_true(r.record == 1U && r.adc_windows == 0U);
#else
	ztest_test_skip();
#endif
}

static bool fast_credit(const void *state)
{
	ARG_UNUSED(state);
	return SPB <= 64;
}

ZTEST_SUITE(tc32_rng, fast_credit, setup, before, NULL, NULL);

/* A credit too low for the limit (tc32_rng.collect_limit: 1/1024 bit per
 * jitter sample, the ADC without noise): collect returns -ETIMEDOUT at
 * TC32_RNG_COLLECT_TIMEOUT_MS, keeps the credit of what it took, and a
 * second call goes on from there.
 */
ZTEST(tc32_rng_slow, test_collect_limit)
{
	struct tc32_rng_status a, b;
	uint32_t t0, ms;

	emu_rbg(RBG_HEALTHY, 4U);
	emu_adc(ADC_OFF, 0U);
	have_stored = false;
	zassert_equal(tc32_rng_reinit(0U), NOT_READY);
	t0 = k_cycle_get_32();
	zassert_equal(tc32_rng_collect(), -ETIMEDOUT);
	ms = (k_cycle_get_32() - t0) / 16000U;
	tc32_rng_get_status(&a);
	TC_PRINT("credit 1/%u bit per sample: collect gave up after %u ms, %u samples, %u bits\n",
		 SPB, ms, a.counted, a.credit_bits);
	zassert_true(ms >= CONFIG_TC32_RNG_COLLECT_TIMEOUT_MS &&
		     ms <= CONFIG_TC32_RNG_COLLECT_TIMEOUT_MS + 20U + (HAVE_ADC ? 10U : 0U),
		     "gave up after %u ms", ms);
	zassert_equal(a.state, NOT_READY);
	zassert_true(a.credit_bits > 0U && a.credit_bits < 128U);
	zassert_equal(tc32_rng_collect(), -ETIMEDOUT);
	tc32_rng_get_status(&b);
	TC_PRINT("again: %u samples, %u bits\n", b.counted, b.credit_bits);
	zassert_true(b.counted > a.counted && b.credit_bits > a.credit_bits);
	zassert_equal(b.state, NOT_READY);
}

static bool slow_credit(const void *state)
{
	ARG_UNUSED(state);
	return SPB > 64;
}

ZTEST_SUITE(tc32_rng_slow, slow_credit, NULL, NULL, NULL, NULL);
