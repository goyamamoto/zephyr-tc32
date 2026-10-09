/*
 * Telink TLSR8278 SAR ADC (DS-TLSR8278 12), the misc channel in auto mode.
 *
 * A conversion of one channel, with the ADC powered down (analog 0xfc bit
 * 5) while its settings change:
 * - the ADC clock 24 MHz / (5 + 1) = 4 MHz (0xf4), the misc channel on with
 *   the state machine's two states (0xf2 = 0x24), a "set" state of 10 and a
 *   "capture" state of 240 cycles of 24 MHz (0xf1, 0xef): one conversion
 *   every 250 cycles, 96 kHz (12.3);
 * - differential mode and 14 bits (0xec = 0x43), the positive and negative
 *   inputs (0xeb; a single-ended channel's negative input is ground, 0xf),
 *   the 1.2 V reference (0xea = 2), the sampling cycles (0xee), the input
 *   scale 1 (0xfa bits 7:6 = 0); with these, 0xfa bits 5:0 = 0x3d and 0xf9
 *   bit 4 = 1, which DS-TLSR8278 does not document;
 * - powered up, the ADC reset (0x61 bit 5, 2.4), and the codes taken from
 *   DFIFO2, the DMA that writes each conversion into the next 32-bit slot
 *   of a ring in SRAM: its address 0xb08 (16 bits; the SRAM's high octet
 *   0x04 stays in 0xb0b), its size in 16-byte units less one 0xb0a, its
 *   write pointer 0xb1e, enabled by 0xb10 bit 2. These registers are not in
 *   DS-TLSR8278; the code writes them so.
 * The first two codes only let the input settle; the third is the result.
 * A slot is read once it is not 0, and waited for at most 10 conversion
 * periods. Then the DMA is off and the ADC powered down.
 *
 * A code is 14 bits, two's complement, in bits 13:0 of its slot. A sequence
 * with resolution 14 gets it sign-extended; with 13, the resolution of a
 * single-ended input, a negative code (below ground) reads 0.
 *
 * tc32_rng converts with the same ADC and DMA (TC32_RNG_ADC):
 * adc_tlsr8278_lock() and adc_tlsr8278_unlock() take and give this
 * driver's context lock, which a read holds until its sequence ends, so the
 * two never interleave.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_adc

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <tlsr8278_adc.h>
#include <tlsr8278_analog.h>

#define ADC_CONTEXT_USES_KERNEL_TIMER
#include "adc_context.h"

#define DFIFO2_ADDR (DT_INST_REG_ADDR(0) + 0x00U) /* 0xb08 */
#define DFIFO2_SIZE (DT_INST_REG_ADDR(0) + 0x02U) /* 0xb0a */
#define DFIFO_MODE  (DT_INST_REG_ADDR(0) + 0x08U) /* 0xb10 */
#define DFIFO2_WPTR (DT_INST_REG_ADDR(0) + 0x16U) /* 0xb1e */
#define DFIFO2_ON   BIT(2)
#define RST1        0x00800061U
#define RST1_ADC    BIT(5)
#define STIMER      0x00800740U

#define ANA_VREF    0xeaU
#define ANA_INPUT   0xebU
#define ANA_RES     0xecU
#define ANA_TSAMP   0xeeU
#define ANA_MAX_MC  0xefU
#define ANA_MAX_S   0xf1U
#define ANA_STATES  0xf2U
#define ANA_CLK_DIV 0xf4U
#define ANA_F9      0xf9U
#define ANA_SCALE   0xfaU
#define ANA_POWER   0xfcU
#define ADC_PD      BIT(5)

#define INPUT_PIN_MIN 1U  /* PB0 */
#define INPUT_PIN_MAX 10U /* PC5 */
#define INPUT_GROUND  0xfU
#define TSAMP_DEFAULT 1U  /* 6 ADC clock cycles */

#define SETTLE_CODES 2U
#define CODE_SIGN    BIT(13)
#define CODE_MASK    0x3fffU
/* 10 conversion periods of 250 cycles of 24 MHz, in system timer ticks (16 MHz). */
#define WAIT_TICKS   (10U * 250U * 16U / 24U)
#define CHANNELS     8U

struct adc_tlsr8278_chan {
	uint8_t input;   /* positive << 4 | negative, as 0xeb takes them */
	uint8_t tsamp;   /* 0xee */
	bool configured;
};

struct adc_tlsr8278_data {
	struct adc_context ctx;
	int16_t *buffer;
	int16_t *repeat_buffer;
	uint8_t channels;
	uint8_t resolution;
	struct adc_tlsr8278_chan chan[CHANNELS];
};

static volatile uint32_t ring[8] __aligned(16);
static struct adc_tlsr8278_data adc_data = {
	ADC_CONTEXT_INIT_TIMER(adc_data, ctx),
	ADC_CONTEXT_INIT_LOCK(adc_data, ctx),
	ADC_CONTEXT_INIT_SYNC(adc_data, ctx),
};

static void analog_setup(const struct adc_tlsr8278_chan *c)
{
	tlsr8278_analog_update(ANA_POWER, ADC_PD, ADC_PD);
	tlsr8278_analog_write(ANA_CLK_DIV, 0x05U);
	tlsr8278_analog_write(ANA_STATES, 0x24U);
	tlsr8278_analog_write(ANA_MAX_MC, 0xf0U);
	tlsr8278_analog_write(ANA_MAX_S, 0x0aU);
	tlsr8278_analog_write(ANA_RES, 0x43U);
	tlsr8278_analog_write(ANA_INPUT, c->input);
	tlsr8278_analog_write(ANA_VREF, 0x02U);
	tlsr8278_analog_write(ANA_TSAMP, c->tsamp);
	tlsr8278_analog_write(ANA_SCALE, 0x3dU);
	tlsr8278_analog_update(ANA_F9, BIT(4), BIT(4));
	tlsr8278_analog_update(ANA_POWER, ADC_PD, 0U);
}

/* One channel's code; -EIO when the DMA brings none. */
static int convert(const struct adc_tlsr8278_chan *c, uint32_t *code)
{
	uint32_t slot = 0U, v = 0U;
	uint8_t rst;
	int err = 0;

	analog_setup(c);
	rst = sys_read8(RST1);
	sys_write8(rst | RST1_ADC, RST1);
	sys_write8(rst & (uint8_t)~RST1_ADC, RST1);
	for (size_t i = 0U; i < ARRAY_SIZE(ring); i++) {
		ring[i] = 0U;
	}
	sys_write16((uint16_t)(uintptr_t)ring, DFIFO2_ADDR);
	sys_write8(sizeof(ring) / 16U - 1U, DFIFO2_SIZE);
	sys_write16(0U, DFIFO2_WPTR);
	sys_write8(sys_read8(DFIFO_MODE) | DFIFO2_ON, DFIFO_MODE);
	for (uint32_t n = 0U; n <= SETTLE_CODES; n++) {
		uint32_t t0 = sys_read32(STIMER);

		while ((v = ring[slot]) == 0U) {
			if (sys_read32(STIMER) - t0 > WAIT_TICKS) {
				break;
			}
		}
		if (v == 0U) {
			err = -EIO;
			break;
		}
		ring[slot] = 0U;
		slot = (slot + 1U) % ARRAY_SIZE(ring);
	}
	sys_write8(sys_read8(DFIFO_MODE) & (uint8_t)~DFIFO2_ON, DFIFO_MODE);
	tlsr8278_analog_update(ANA_POWER, ADC_PD, ADC_PD);
	*code = v & CODE_MASK;
	return err;
}

static int16_t sample(uint32_t code, uint8_t resolution)
{
	if ((code & CODE_SIGN) == 0U) {
		return (int16_t)code;
	}
	return resolution == 13U ? 0 : (int16_t)(code | ~CODE_MASK);
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct adc_tlsr8278_data *data = CONTAINER_OF(ctx, struct adc_tlsr8278_data, ctx);

	data->repeat_buffer = data->buffer;
	for (uint8_t ch = 0U; ch < CHANNELS; ch++) {
		uint32_t code;

		if ((data->channels & BIT(ch)) == 0U) {
			continue;
		}
		if (convert(&data->chan[ch], &code) != 0) {
			adc_context_complete(ctx, -EIO);
			return;
		}
		*data->buffer++ = sample(code, data->resolution);
	}
	adc_context_on_sampling_done(ctx, DEVICE_DT_INST_GET(0));
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx, bool repeat_sampling)
{
	struct adc_tlsr8278_data *data = CONTAINER_OF(ctx, struct adc_tlsr8278_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->repeat_buffer;
	}
}

static int adc_tlsr8278_channel_setup(const struct device *dev,
				      const struct adc_channel_cfg *cfg)
{
	struct adc_tlsr8278_data *data = dev->data;
	struct adc_tlsr8278_chan c = {.configured = true};
	uint8_t neg = INPUT_GROUND;

	if (cfg->channel_id >= CHANNELS) {
		return -EINVAL;
	}
	/* The chip's other input scale, 1/8, has no gain value in the API. */
	if (cfg->gain != ADC_GAIN_1) {
		return -EINVAL;
	}
	if (cfg->reference != ADC_REF_INTERNAL) {
		return -EINVAL;
	}
	if (cfg->acquisition_time == ADC_ACQ_TIME_DEFAULT) {
		c.tsamp = TSAMP_DEFAULT;
	} else {
		uint16_t ticks = ADC_ACQ_TIME_VALUE(cfg->acquisition_time);

		if (ADC_ACQ_TIME_UNIT(cfg->acquisition_time) != ADC_ACQ_TIME_TICKS ||
		    ticks < 3U || ticks > 48U || (ticks % 3U) != 0U) {
			return -EINVAL;
		}
		c.tsamp = (uint8_t)(ticks / 3U - 1U);
	}
	if (cfg->input_positive < INPUT_PIN_MIN || cfg->input_positive > INPUT_PIN_MAX) {
		return -EINVAL;
	}
	if (cfg->differential) {
		if (cfg->input_negative < INPUT_PIN_MIN || cfg->input_negative > INPUT_PIN_MAX) {
			return -EINVAL;
		}
		neg = cfg->input_negative;
	}
	c.input = (uint8_t)((cfg->input_positive << 4) | neg);
	data->chan[cfg->channel_id] = c;
	return 0;
}

static int start_read(const struct device *dev, const struct adc_sequence *seq)
{
	struct adc_tlsr8278_data *data = dev->data;
	size_t needed = 0U;

	if (seq->channels == 0U || (seq->channels & ~BIT_MASK(CHANNELS)) != 0U) {
		return -EINVAL;
	}
	for (uint8_t ch = 0U; ch < CHANNELS; ch++) {
		if ((seq->channels & BIT(ch)) == 0U) {
			continue;
		}
		if (!data->chan[ch].configured) {
			return -EINVAL;
		}
		needed += sizeof(int16_t);
	}
	if (seq->resolution != 13U && seq->resolution != 14U) {
		return -EINVAL;
	}
	if (seq->oversampling != 0U) {
		return -ENOTSUP;
	}
	if (seq->options != NULL) {
		needed *= 1U + seq->options->extra_samplings;
	}
	if (seq->buffer_size < needed) {
		return -ENOMEM;
	}
	data->channels = (uint8_t)seq->channels;
	data->resolution = seq->resolution;
	data->buffer = seq->buffer;
	adc_context_start_read(&data->ctx, seq);
	return adc_context_wait_for_completion(&data->ctx);
}

static int adc_tlsr8278_read(const struct device *dev, const struct adc_sequence *seq)
{
	struct adc_tlsr8278_data *data = dev->data;
	int err;

	adc_context_lock(&data->ctx, false, NULL);
	err = start_read(dev, seq);
	adc_context_release(&data->ctx, err);
	return err;
}

#ifdef CONFIG_ADC_ASYNC
static int adc_tlsr8278_read_async(const struct device *dev, const struct adc_sequence *seq,
				   struct k_poll_signal *async)
{
	struct adc_tlsr8278_data *data = dev->data;
	int err;

	adc_context_lock(&data->ctx, true, async);
	err = start_read(dev, seq);
	adc_context_release(&data->ctx, err);
	return err;
}
#endif

int adc_tlsr8278_lock(void)
{
	return k_sem_take(&adc_data.ctx.lock, K_FOREVER);
}

void adc_tlsr8278_unlock(void)
{
	k_sem_give(&adc_data.ctx.lock);
}

static int adc_tlsr8278_init(const struct device *dev)
{
	struct adc_tlsr8278_data *data = dev->data;

	/* DFIFO2 reaches the SRAM at 0x84xxxx only. */
	if ((((uintptr_t)ring) >> 16) != 0x84U) {
		return -EIO;
	}
	adc_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static DEVICE_API(adc, adc_tlsr8278_api) = {
	.channel_setup = adc_tlsr8278_channel_setup,
	.read = adc_tlsr8278_read,
#ifdef CONFIG_ADC_ASYNC
	.read_async = adc_tlsr8278_read_async,
#endif
	.ref_internal = 1200,
};

DEVICE_DT_INST_DEFINE(0, adc_tlsr8278_init, NULL, &adc_data, NULL, POST_KERNEL,
		      CONFIG_ADC_INIT_PRIORITY, &adc_tlsr8278_api);
