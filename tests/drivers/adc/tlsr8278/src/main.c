/*
 * The TLSR8278 ADC driver on tlsr8278_generic, in TC32-devtools' emulator,
 * whose emulator-only register 0xffe4 gives a pin input a code of its own:
 * the inputs a channel selects, the codes as resolution 13 and 14 give
 * them, the settings a conversion leaves, the refusals; and tc32_rng's ADC
 * source waiting for a read that holds the ADC, with the application's
 * hooks still called.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/random/tc32_rng.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#include <tlsr8278_analog.h>

#define PIN_CODE   0x0080ffe4U /* emulator only */
#define DFIFO_MODE 0x00800b10U
#define VBAT_CODE  0x1df5      /* the emulator's VBAT conversions, with noise */

#define SPEC(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec chan[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, SPEC)};

static atomic_t app_locks, app_unlocks;

/* The application's hooks, as an application with its own ADC user has them. */
int tc32_rng_adc_lock(void)
{
	atomic_inc(&app_locks);
	return 0;
}

void tc32_rng_adc_unlock(void)
{
	atomic_inc(&app_unlocks);
}

static void pin_code(uint8_t input, uint16_t code)
{
	sys_write32(BIT(24) | ((uint32_t)input << 16) | code, PIN_CODE);
}

static int read(int idx, uint8_t resolution, int16_t *out)
{
	struct adc_sequence seq = {
		.channels = BIT(chan[idx].channel_id),
		.buffer = out,
		.buffer_size = sizeof(*out),
		.resolution = resolution,
	};

	return adc_read(chan[idx].dev, &seq);
}

static void *setup(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(chan); i++) {
		zassert_true(adc_is_ready_dt(&chan[i]));
		zassert_ok(adc_channel_setup_dt(&chan[i]));
	}
	return NULL;
}

ZTEST(adc_tlsr8278, test_codes)
{
	int16_t v = 0, two[2] = {0};
	struct adc_sequence seq = {
		.channels = BIT(0) | BIT(1),
		.buffer = two,
		.buffer_size = sizeof(two),
		.resolution = 13,
	};

	pin_code(4, 0x0abc);       /* PB3 */
	pin_code(9, 0x3f00);       /* PC4: below ground, -256 */
	pin_code(5, 0x3fff);       /* PB4: -1 */
	zassert_ok(read(0, 13, &v));
	zassert_equal(v, 0x0abc, "PB3 0x%x", v);
	zassert_ok(read(1, 13, &v));
	zassert_equal(v, 0, "PC4 at resolution 13: %d", v);
	zassert_ok(read(1, 14, &v));
	zassert_equal(v, -256, "PC4 at resolution 14: %d", v);
	zassert_ok(read(2, 14, &v));
	zassert_equal(v, -1, "PB4 - PB5: %d", v);

	/* Two channels in one sequence, in channel order. */
	zassert_ok(adc_read(chan[0].dev, &seq));
	zassert_equal(two[0], 0x0abc);
	zassert_equal(two[1], 0);

	/* What the last conversion (channel 1) left: its input against ground, 12 sampling
	 * cycles (0xee = 3), 14-bit differential mode, 1.2 V, 4 MHz, the misc channel's two
	 * states, the ADC powered down and the DMA off.
	 */
	zassert_equal(tlsr8278_analog_read(0xeb), 0x9f);
	zassert_equal(tlsr8278_analog_read(0xee), 0x03);
	zassert_equal(tlsr8278_analog_read(0xec), 0x43);
	zassert_equal(tlsr8278_analog_read(0xea), 0x02);
	zassert_equal(tlsr8278_analog_read(0xf4), 0x05);
	zassert_equal(tlsr8278_analog_read(0xf2), 0x24);
	zassert_equal(tlsr8278_analog_read(0xfc) & BIT(5), BIT(5), "ADC left powered");
	zassert_equal(sys_read8(DFIFO_MODE) & BIT(2), 0U, "DMA left on");

	/* The differential channel's inputs. */
	zassert_ok(read(2, 14, &v));
	zassert_equal(tlsr8278_analog_read(0xeb), 0x56);
	sys_write32(0U, PIN_CODE);
}

ZTEST(adc_tlsr8278, test_refusals)
{
	struct adc_channel_cfg cfg = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 5,
		.input_positive = 1,
	};
	const struct device *dev = chan[0].dev;
	int16_t v[2];
	struct adc_sequence seq = {
		.channels = BIT(0),
		.buffer = v,
		.buffer_size = sizeof(v),
		.resolution = 13,
	};

	cfg.gain = ADC_GAIN_1_2;
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "gain 1/2");
	cfg.gain = ADC_GAIN_1;
	cfg.reference = ADC_REF_VDD_1;
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "reference VDD");
	cfg.reference = ADC_REF_INTERNAL;
	cfg.input_positive = 0;
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "input 0");
	cfg.input_positive = 11;
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "input 11");
	cfg.input_positive = 1;
	cfg.differential = 1;
	cfg.input_negative = 0;
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "negative input 0");
	cfg.differential = 0;
	cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 4);
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "4 cycles");
	cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 3);
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "microseconds");
	cfg.acquisition_time = ADC_ACQ_TIME_DEFAULT;
	cfg.channel_id = 8;
	zassert_equal(adc_channel_setup(dev, &cfg), -EINVAL, "channel 8");

	seq.resolution = 12;
	zassert_equal(adc_read(dev, &seq), -EINVAL, "resolution 12");
	seq.resolution = 13;
	seq.oversampling = 1;
	zassert_equal(adc_read(dev, &seq), -ENOTSUP, "oversampling");
	seq.oversampling = 0;
	seq.channels = BIT(5);
	zassert_equal(adc_read(dev, &seq), -EINVAL, "channel 5 not set up");
	seq.channels = BIT(0) | BIT(1) | BIT(2);
	zassert_equal(adc_read(dev, &seq), -ENOMEM, "three channels into two samples");
}

static K_THREAD_STACK_DEFINE(rng_stack, 1024);
static struct k_thread rng_thread;
static uint16_t rng_codes[16];
static volatile uint32_t rng_done_ms;

static void rng_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	(void)tc32_rng_adc_samples(rng_codes, ARRAY_SIZE(rng_codes));
	rng_done_ms = k_uptime_get_32();
}

ZTEST(adc_tlsr8278, test_rng_waits_for_a_read)
{
	int16_t samples[10];
	struct adc_sequence_options opt = {
		.interval_us = 10000,
		.extra_samplings = ARRAY_SIZE(samples) - 1,
	};
	struct adc_sequence seq = {
		.options = &opt,
		.channels = BIT(0),
		.buffer = samples,
		.buffer_size = sizeof(samples),
		.resolution = 13,
	};
	struct k_poll_signal sig;
	struct k_poll_event ev = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
							  K_POLL_MODE_NOTIFY_ONLY, &sig);
	uint32_t adc_done_ms;
	atomic_val_t locks0 = atomic_get(&app_locks);

	pin_code(4, 0x0abc);
	k_poll_signal_init(&sig);
	/* About 90 ms of conversions every 10 ms, the ADC held for all of them. */
	zassert_ok(adc_read_async(chan[0].dev, &seq, &sig));
	k_thread_create(&rng_thread, rng_stack, K_THREAD_STACK_SIZEOF(rng_stack), rng_entry,
			NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_MSEC(5));
	zassert_ok(k_poll(&ev, 1, K_SECONDS(2)));
	adc_done_ms = k_uptime_get_32();
	zassert_ok(k_thread_join(&rng_thread, K_SECONDS(2)));

	for (size_t i = 0; i < ARRAY_SIZE(samples); i++) {
		zassert_equal(samples[i], 0x0abc, "sample %u: 0x%x", (unsigned int)i, samples[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(rng_codes); i++) {
		int d = (int)(rng_codes[i] & 0x1fffU) - VBAT_CODE;

		zassert_true(d > -64 && d < 64, "tc32_rng code %u: 0x%x", (unsigned int)i,
			     rng_codes[i]);
	}
	zassert_true(rng_done_ms >= adc_done_ms - 1U, "tc32_rng done at %u ms, the read at %u ms",
		     rng_done_ms, adc_done_ms);
	zassert_equal(atomic_get(&app_locks), locks0 + 1, "the application's lock");
	zassert_equal(atomic_get(&app_unlocks), atomic_get(&app_locks), "its unlock");
	sys_write32(0U, PIN_CODE);
}

ZTEST_SUITE(adc_tlsr8278, NULL, setup, NULL, NULL, NULL);
