/*
 * TLSR8278 pin control for Zephyr's pinctrl API (drivers/pinctrl/
 * pinctrl_tlsr8278.c): one 32-bit word per pin, the devicetree's pinmux value
 * (<zephyr/dt-bindings/pinctrl/b87-pinctrl.h>) with the pull resistor
 * and the input enable of the pin controller node added.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_TELINK_TLSR8278_PINCTRL_SOC_H_
#define ZEPHYR_SOC_TELINK_TLSR8278_PINCTRL_SOC_H_

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/pinctrl/b87-pinctrl.h>

typedef uint32_t pinctrl_soc_pin_t;

/* The pull field is the 2-bit code of analog 0x0e-0x15 (DS-TLSR8278 Table 7-5). */
#define TLSR8278_PULL_NONE      0U
#define TLSR8278_PULL_UP_1M     1U
#define TLSR8278_PULL_DOWN_100K 2U
#define TLSR8278_PULL_UP_10K    3U
#define TLSR8278_PULL_POS       16
#define TLSR8278_PULL_MSK       0x3U
#define TLSR8278_INPUT_EN       (1U << 20)

#define Z_PINCTRL_TLSR8278_PULL(node)                                                              \
	(DT_PROP(node, bias_pull_up)                                                               \
		 ? TLSR8278_PULL_UP_10K                                                            \
		 : (DT_PROP(node, bias_pull_down) ? TLSR8278_PULL_DOWN_100K : TLSR8278_PULL_NONE))

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	((uint32_t)DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx), pinmux) |                           \
	 ((uint32_t)Z_PINCTRL_TLSR8278_PULL(DT_PROP_BY_IDX(node_id, prop, idx))                    \
	  << TLSR8278_PULL_POS) |                                                                  \
	 (DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx), input_enable) ? TLSR8278_INPUT_EN : 0U)),

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT)}

#endif /* ZEPHYR_SOC_TELINK_TLSR8278_PINCTRL_SOC_H_ */
