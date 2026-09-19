/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_gpio

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>

#define TLSR8258_GPIO_MAX_PIN 7u
#define TLSR8258_GPIO_CORE_INTERRUPT_EN BIT(3)

#define TLSR8258_GPIO_WAKEUP_IRQ ((volatile uint8_t *)0x008005b5u)

#define TLSR8258_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)

#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

#define TLSR8258_GPIO_PULL_FLOAT      0u
#define TLSR8258_GPIO_PULL_UP_10K     3u
#define TLSR8258_GPIO_PULL_DOWN_100K  2u
#define TLSR8258_GPIO_GROUP_A         0u
#define TLSR8258_GPIO_GROUP_B         1u
#define TLSR8258_GPIO_GROUP_C         2u
#define TLSR8258_GPIO_ANALOG_PB_IE    0xbdu
#define TLSR8258_GPIO_ANALOG_PC_IE    0xc0u

struct tlsr8258_gpio_regs {
	uint8_t input;
	uint8_t ie;
	uint8_t oen;
	uint8_t output;
	uint8_t polarity;
	uint8_t ds;
	uint8_t func;
	uint8_t irq_en;
};

struct tlsr8258_gpio_config {
	struct gpio_driver_config common;
	uintptr_t base;
	uint8_t group;
};

struct tlsr8258_gpio_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	gpio_port_pins_t input_en;
	gpio_port_pins_t output_en;
	gpio_port_pins_t pin_int_en;
	gpio_port_pins_t trig_low;
	gpio_port_pins_t trig_both;
#ifdef CONFIG_GPIO_GET_CONFIG
	gpio_flags_t pin_flags[TLSR8258_GPIO_MAX_PIN + 1u];
#endif
};

static volatile struct tlsr8258_gpio_regs *tlsr8258_gpio_regs(const struct device *dev)
{
	const struct tlsr8258_gpio_config *config = dev->config;

	return (volatile struct tlsr8258_gpio_regs *)config->base;
}

static bool tlsr8258_gpio_pin_valid(gpio_pin_t pin)
{
	return pin <= TLSR8258_GPIO_MAX_PIN;
}

static void tlsr8258_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_analog_read(uint8_t addr)
{
	uint32_t key = irq_lock();
	uint8_t data;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	tlsr8258_analog_wait();
	data = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);

	return data;
}

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	uint32_t key = irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);
}

static uint16_t tlsr8258_gpio_vendor_pin(const struct device *dev, gpio_pin_t pin)
{
	const struct tlsr8258_gpio_config *config = dev->config;

	return ((uint16_t)config->group << 8) | (uint16_t)BIT(pin);
}

static void tlsr8258_gpio_pull(uint16_t vendor_pin, uint8_t pull)
{
	uint8_t reg = 0x0eu + ((vendor_pin >> 8) << 1) +
		      (((vendor_pin & 0xf0u) != 0u) ? 1u : 0u);
	uint8_t shift;
	uint8_t mask;

	if ((vendor_pin & 0x11u) != 0u) {
		shift = 0u;
		mask = 0xfcu;
	} else if ((vendor_pin & 0x22u) != 0u) {
		shift = 2u;
		mask = 0xf3u;
	} else if ((vendor_pin & 0x44u) != 0u) {
		shift = 4u;
		mask = 0xcfu;
	} else if ((vendor_pin & 0x88u) != 0u) {
		shift = 6u;
		mask = 0x3fu;
	} else {
		return;
	}

	tlsr8258_analog_write(reg, (tlsr8258_analog_read(reg) & mask) | (pull << shift));
}

static void tlsr8258_gpio_ie_update(const struct device *dev, uint8_t mask, bool enable)
{
	const struct tlsr8258_gpio_config *config = dev->config;
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	uint8_t analog_reg;

	if (config->group == TLSR8258_GPIO_GROUP_B) {
		analog_reg = TLSR8258_GPIO_ANALOG_PB_IE;
	} else if (config->group == TLSR8258_GPIO_GROUP_C) {
		analog_reg = TLSR8258_GPIO_ANALOG_PC_IE;
	} else {
		if (enable) {
			regs->ie |= mask;
		} else {
			regs->ie &= (uint8_t)~mask;
		}
		return;
	}

	if (enable) {
		tlsr8258_analog_write(analog_reg, tlsr8258_analog_read(analog_reg) | mask);
	} else {
		tlsr8258_analog_write(analog_reg, tlsr8258_analog_read(analog_reg) & (uint8_t)~mask);
	}
}

static void tlsr8258_gpio_clear_source(void)
{
	tlsr8258_irq_clear_parent(TLSR8258_IRQ_GPIO);
}

static void tlsr8258_gpio_dispatch(const struct device *dev)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	struct tlsr8258_gpio_data *data = dev->data;
	gpio_port_pins_t input = regs->input;
	gpio_port_pins_t active_high = data->pin_int_en & ~data->trig_low;
	gpio_port_pins_t active_low = data->pin_int_en & data->trig_low;
	gpio_port_pins_t pins = (input & active_high) | ((~input) & active_low);

	if (pins != 0u) {
		gpio_fire_callbacks(&data->callbacks, dev, pins);
	}

	if ((pins & data->trig_both) != 0u) {
		data->trig_low = (data->trig_low & ~pins) | (input & pins);
		regs->polarity = (regs->polarity & (uint8_t)~pins) |
				 (uint8_t)(data->trig_low & pins);
		tlsr8258_gpio_clear_source();
	}
}

static void tlsr8258_gpio_irq_handler(const void *unused)
{
	ARG_UNUSED(unused);

	tlsr8258_gpio_clear_source();

#define TLSR8258_GPIO_DISPATCH(n) tlsr8258_gpio_dispatch(DEVICE_DT_INST_GET(n));
	DT_INST_FOREACH_STATUS_OKAY(TLSR8258_GPIO_DISPATCH)
#undef TLSR8258_GPIO_DISPATCH
}

static void tlsr8258_gpio_irq_connect(void)
{
	IRQ_CONNECT(TLSR8258_IRQ_GPIO, 0, tlsr8258_gpio_irq_handler, NULL, 0);
}

static bool tlsr8258_gpio_any_interrupt_enabled(void)
{
	bool enabled = false;

#define TLSR8258_GPIO_CHECK_INT(n)						\
	do {									\
		const struct device *dev = DEVICE_DT_INST_GET(n);		\
		const struct tlsr8258_gpio_data *data = dev->data;		\
										\
		enabled = enabled || data->pin_int_en != 0u;			\
	} while (false);
	DT_INST_FOREACH_STATUS_OKAY(TLSR8258_GPIO_CHECK_INT)
#undef TLSR8258_GPIO_CHECK_INT

	return enabled;
}

static int tlsr8258_gpio_init(const struct device *dev)
{
	struct tlsr8258_gpio_data *data = dev->data;

	data->input_en = 0u;
	data->output_en = 0u;
	data->pin_int_en = 0u;
	data->trig_low = 0u;
	data->trig_both = 0u;
#ifdef CONFIG_GPIO_GET_CONFIG
	for (uint8_t pin = 0u; pin <= TLSR8258_GPIO_MAX_PIN; pin++) {
		data->pin_flags[pin] = 0u;
	}
#endif

	tlsr8258_gpio_irq_connect();
	return 0;
}

static int tlsr8258_gpio_pin_configure(const struct device *dev, gpio_pin_t pin,
				       gpio_flags_t flags)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	struct tlsr8258_gpio_data *data = dev->data;
	uint16_t vendor_pin;
	uint8_t mask;

	if (!tlsr8258_gpio_pin_valid(pin)) {
		return -EINVAL;
	}

	if ((flags & (GPIO_SINGLE_ENDED | GPIO_PULL_UP | GPIO_PULL_DOWN)) ==
	    (GPIO_PULL_UP | GPIO_PULL_DOWN)) {
		return -EINVAL;
	}

	if ((flags & GPIO_SINGLE_ENDED) != 0u) {
		return -ENOTSUP;
	}

	mask = (uint8_t)BIT(pin);
	vendor_pin = tlsr8258_gpio_vendor_pin(dev, pin);

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0u) {
		regs->output |= mask;
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0u) {
		regs->output &= (uint8_t)~mask;
	}

	regs->func |= mask;

	if ((flags & GPIO_OUTPUT) != 0u) {
		regs->oen &= (uint8_t)~mask;
		data->output_en |= mask;
	} else {
		regs->oen |= mask;
		data->output_en &= ~mask;
	}

	if ((flags & GPIO_INPUT) != 0u) {
		tlsr8258_gpio_ie_update(dev, mask, true);
		data->input_en |= mask;
	} else {
		tlsr8258_gpio_ie_update(dev, mask, false);
		data->input_en &= ~mask;
	}

	if ((flags & GPIO_PULL_UP) != 0u) {
		tlsr8258_gpio_pull(vendor_pin, TLSR8258_GPIO_PULL_UP_10K);
	} else if ((flags & GPIO_PULL_DOWN) != 0u) {
		tlsr8258_gpio_pull(vendor_pin, TLSR8258_GPIO_PULL_DOWN_100K);
	} else {
		tlsr8258_gpio_pull(vendor_pin, TLSR8258_GPIO_PULL_FLOAT);
	}

#ifdef CONFIG_GPIO_GET_CONFIG
	data->pin_flags[pin] = flags;
#endif

	return 0;
}

#ifdef CONFIG_GPIO_GET_CONFIG
static int tlsr8258_gpio_pin_get_config(const struct device *dev, gpio_pin_t pin,
					gpio_flags_t *flags)
{
	struct tlsr8258_gpio_data *data = dev->data;

	if (!tlsr8258_gpio_pin_valid(pin)) {
		return -EINVAL;
	}

	*flags = data->pin_flags[pin];
	return 0;
}
#endif

static int tlsr8258_gpio_port_get_raw(const struct device *dev,
				      gpio_port_value_t *value)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	*value = regs->input;
	return 0;
}

static int tlsr8258_gpio_port_set_masked_raw(const struct device *dev,
					     gpio_port_pins_t mask,
					     gpio_port_value_t value)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output = (regs->output & (uint8_t)~mask) | (value & mask);
	return 0;
}

static int tlsr8258_gpio_port_set_bits_raw(const struct device *dev,
					   gpio_port_pins_t pins)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output |= pins;
	return 0;
}

static int tlsr8258_gpio_port_clear_bits_raw(const struct device *dev,
					     gpio_port_pins_t pins)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output &= (uint8_t)~pins;
	return 0;
}

static int tlsr8258_gpio_port_toggle_bits(const struct device *dev,
					  gpio_port_pins_t pins)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output ^= pins;
	return 0;
}

static int tlsr8258_gpio_pin_interrupt_configure(const struct device *dev,
						 gpio_pin_t pin,
						 enum gpio_int_mode mode,
						 enum gpio_int_trig trig)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	struct tlsr8258_gpio_data *data = dev->data;
	unsigned int key;
	uint8_t mask;

	if (!tlsr8258_gpio_pin_valid(pin)) {
		return -EINVAL;
	}

	if (mode == GPIO_INT_MODE_LEVEL) {
		return -ENOTSUP;
	}

	mask = (uint8_t)BIT(pin);
	key = irq_lock();

	regs->irq_en &= (uint8_t)~mask;
	data->pin_int_en &= ~mask;
	data->trig_both &= ~mask;

	if (mode == GPIO_INT_MODE_DISABLED) {
		tlsr8258_gpio_clear_source();
		if (!tlsr8258_gpio_any_interrupt_enabled()) {
			irq_disable(TLSR8258_IRQ_GPIO);
		}
		irq_unlock(key);
		return 0;
	}

	if (trig == GPIO_INT_TRIG_HIGH) {
		data->trig_low &= ~mask;
		regs->polarity &= (uint8_t)~mask;
	} else if (trig == GPIO_INT_TRIG_LOW) {
		data->trig_low |= mask;
		regs->polarity |= mask;
	} else if (trig == GPIO_INT_TRIG_BOTH) {
		uint8_t input = regs->input;

		data->trig_both |= mask;
		if ((input & mask) != 0u) {
			data->trig_low |= mask;
			regs->polarity |= mask;
		} else {
			data->trig_low &= ~mask;
			regs->polarity &= (uint8_t)~mask;
		}
	} else {
		irq_unlock(key);
		return -ENOTSUP;
	}

	*TLSR8258_GPIO_WAKEUP_IRQ |= TLSR8258_GPIO_CORE_INTERRUPT_EN;

	/*
	 * Clear once after polarity setup and again after enabling the GPIO pin
	 * source. Some boards latch one parent edge while the pin source is
	 * enabled; keep the parent mask disabled until that stale edge is gone.
	 */
	tlsr8258_gpio_clear_source();
	data->pin_int_en |= mask;
	regs->irq_en |= mask;
	tlsr8258_gpio_clear_source();
	irq_enable(TLSR8258_IRQ_GPIO);

	irq_unlock(key);
	return 0;
}

static int tlsr8258_gpio_manage_callback(const struct device *dev,
					 struct gpio_callback *callback,
					 bool set)
{
	struct tlsr8258_gpio_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int tlsr8258_gpio_port_get_direction(const struct device *dev,
					    gpio_port_pins_t map,
					    gpio_port_pins_t *inputs,
					    gpio_port_pins_t *outputs)
{
	struct tlsr8258_gpio_data *data = dev->data;

	if (inputs != NULL) {
		*inputs = data->input_en & map;
	}
	if (outputs != NULL) {
		*outputs = data->output_en & map;
	}

	return 0;
}
#endif

static DEVICE_API(gpio, tlsr8258_gpio_api) = {
	.pin_configure = tlsr8258_gpio_pin_configure,
#ifdef CONFIG_GPIO_GET_CONFIG
	.pin_get_config = tlsr8258_gpio_pin_get_config,
#endif
	.port_get_raw = tlsr8258_gpio_port_get_raw,
	.port_set_masked_raw = tlsr8258_gpio_port_set_masked_raw,
	.port_set_bits_raw = tlsr8258_gpio_port_set_bits_raw,
	.port_clear_bits_raw = tlsr8258_gpio_port_clear_bits_raw,
	.port_toggle_bits = tlsr8258_gpio_port_toggle_bits,
	.pin_interrupt_configure = tlsr8258_gpio_pin_interrupt_configure,
	.manage_callback = tlsr8258_gpio_manage_callback,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = tlsr8258_gpio_port_get_direction,
#endif
};

#define TLSR8258_GPIO_INIT(n)							\
	static const struct tlsr8258_gpio_config tlsr8258_gpio_config_##n = {	\
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),			\
		.base = DT_INST_REG_ADDR(n),					\
		.group = (DT_INST_REG_ADDR(n) - 0x00800580u) / 8u,		\
	};									\
	static struct tlsr8258_gpio_data tlsr8258_gpio_data_##n;		\
										\
	DEVICE_DT_INST_DEFINE(n, tlsr8258_gpio_init, NULL,			\
			      &tlsr8258_gpio_data_##n,			\
			      &tlsr8258_gpio_config_##n,			\
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,		\
			      &tlsr8258_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_GPIO_INIT)
