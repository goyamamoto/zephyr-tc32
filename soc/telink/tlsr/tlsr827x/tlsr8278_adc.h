/*
 * The TLSR8278 ADC driver's lock (drivers/adc/adc_tlsr8278.c), for the
 * other user of the ADC and its DMA: tc32_rng's ADC source takes it around
 * each of its uses, so that it never interleaves with a read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR8278_ADC_H_
#define ZEPHYR_SOC_TELINK_TLSR8278_ADC_H_

/*
 * Wait until no read of the ADC driver runs and keep the next one waiting
 * (thread context). Returns 0.
 */
int adc_tlsr8278_lock(void);

/* Let the ADC driver's reads run again. */
void adc_tlsr8278_unlock(void);

#endif /* ZEPHYR_SOC_TELINK_TLSR8278_ADC_H_ */
