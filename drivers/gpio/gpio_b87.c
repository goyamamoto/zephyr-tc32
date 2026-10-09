/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_gpio

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <tlsr827x/irq.h>

#define GPIO_B87_MAX_PIN 7u
#define GPIO_B87_CORE_INTERRUPT_EN BIT(3)

#define GPIO_B87_WAKEUP_IRQ ((volatile uint8_t *)0x008005b5u)

#define TLSR827X_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR827X_REG_ANA_ADDR TLSR827X_REG8(0x008000b8u)
#define TLSR827X_REG_ANA_DATA TLSR827X_REG8(0x008000b9u)
#define TLSR827X_REG_ANA_CTRL TLSR827X_REG8(0x008000bau)

#define TLSR827X_FLD_ANA_BUSY BIT(0)
#define TLSR827X_FLD_ANA_RW   BIT(5)
#define TLSR827X_FLD_ANA_CYC0 BIT(6)

#define GPIO_B87_PULL_FLOAT      0u
#define GPIO_B87_PULL_UP_10K     3u
#define GPIO_B87_PULL_DOWN_100K  2u
#define GPIO_B87_GROUP_B         1u
#define GPIO_B87_GROUP_C         2u
#define GPIO_B87_ANALOG_PB_IE    0xbdu
#define GPIO_B87_ANALOG_PC_IE    0xc0u

struct gpio_b87_regs {
	uint8_t input;
	uint8_t ie;
	uint8_t oen;
	uint8_t output;
	uint8_t polarity;
	uint8_t ds;
	uint8_t func;
	uint8_t irq_en;
};

struct gpio_b87_config {
	struct gpio_driver_config common;
	uintptr_t base;
	uint8_t group;
};

struct gpio_b87_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	gpio_port_pins_t input_en;
	gpio_port_pins_t output_en;
	gpio_port_pins_t pin_int_en;
	gpio_port_pins_t trig_low;
	gpio_port_pins_t trig_both;
};

static volatile struct gpio_b87_regs *gpio_b87_regs(const struct device *dev)
{
	const struct gpio_b87_config *config = dev->config;

	return (volatile struct gpio_b87_regs *)config->base;
}

static bool gpio_b87_pin_valid(gpio_pin_t pin)
{
	return pin <= GPIO_B87_MAX_PIN;
}

static void tlsr827x_analog_wait(void)
{
	while ((TLSR827X_REG_ANA_CTRL & TLSR827X_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr827x_analog_read(uint8_t addr)
{
	uint32_t key = irq_lock();
	uint8_t data;

	TLSR827X_REG_ANA_ADDR = addr;
	TLSR827X_REG_ANA_CTRL = TLSR827X_FLD_ANA_CYC0;
	tlsr827x_analog_wait();
	data = TLSR827X_REG_ANA_DATA;
	TLSR827X_REG_ANA_CTRL = 0u;
	irq_unlock(key);

	return data;
}

static void tlsr827x_analog_write(uint8_t addr, uint8_t value)
{
	uint32_t key = irq_lock();

	TLSR827X_REG_ANA_ADDR = addr;
	TLSR827X_REG_ANA_DATA = value;
	TLSR827X_REG_ANA_CTRL = TLSR827X_FLD_ANA_CYC0 | TLSR827X_FLD_ANA_RW;
	tlsr827x_analog_wait();
	TLSR827X_REG_ANA_CTRL = 0u;
	irq_unlock(key);
}

static uint16_t gpio_b87_vendor_pin(const struct device *dev, gpio_pin_t pin)
{
	const struct gpio_b87_config *config = dev->config;

	return ((uint16_t)config->group << 8) | (uint16_t)BIT(pin);
}

static void gpio_b87_pull(uint16_t vendor_pin, uint8_t pull)
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

	tlsr827x_analog_write(reg, (tlsr827x_analog_read(reg) & mask) | (pull << shift));
}

static void gpio_b87_ie_update(const struct device *dev, uint8_t mask, bool enable)
{
	const struct gpio_b87_config *config = dev->config;
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);
	uint8_t analog_reg;

	/*
	 * Port C's input enable is an analog register (afe_0xc0, DS-TLSR8278
	 * 7.1.1.1). Port B's is analog only without SOC_TLSR8278; on the
	 * TLSR8278 it is the digital register, as for ports A and D.
	 */
	if (config->group == GPIO_B87_GROUP_B && !IS_ENABLED(CONFIG_SOC_TLSR8278)) {
		analog_reg = GPIO_B87_ANALOG_PB_IE;
	} else if (config->group == GPIO_B87_GROUP_C) {
		analog_reg = GPIO_B87_ANALOG_PC_IE;
	} else {
		if (enable) {
			regs->ie |= mask;
		} else {
			regs->ie &= (uint8_t)~mask;
		}
		return;
	}

	if (enable) {
		tlsr827x_analog_write(analog_reg, tlsr827x_analog_read(analog_reg) | mask);
	} else {
		tlsr827x_analog_write(analog_reg,
				      tlsr827x_analog_read(analog_reg) & (uint8_t)~mask);
	}
}

static void gpio_b87_clear_source(void)
{
	tlsr827x_irq_clear_parent(TLSR827X_IRQ_GPIO);
}

/*
 * Fires the callbacks of the pins at their active level (only the both-edge
 * pins if both_only). A both-edge pin gets the opposite polarity first, from
 * the same read, so that an edge while the callbacks run raises the request
 * again instead of being cleared. Returns the both-edge pins handled.
 */
static gpio_port_pins_t gpio_b87_dispatch(const struct device *dev, bool both_only)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);
	struct gpio_b87_data *data = dev->data;
	gpio_port_pins_t input = regs->input;
	gpio_port_pins_t active_high = data->pin_int_en & ~data->trig_low;
	gpio_port_pins_t active_low = data->pin_int_en & data->trig_low;
	gpio_port_pins_t pins = (input & active_high) | ((~input) & active_low);
	gpio_port_pins_t both;

	if (both_only) {
		pins &= data->trig_both;
	}
	both = pins & data->trig_both;

	if (both != 0u) {
		data->trig_low = (data->trig_low & ~both) | (input & both);
		regs->polarity = (regs->polarity & (uint8_t)~both) |
				 (uint8_t)(data->trig_low & both);
	}

	if (pins != 0u) {
		gpio_fire_callbacks(&data->callbacks, dev, pins);
	}

	return both;
}

/* Callbacks of all both-edge pins, for a change that no pin shows any more. */
static void gpio_b87_fire_both(const struct device *dev)
{
	struct gpio_b87_data *data = dev->data;
	gpio_port_pins_t pins = data->trig_both & data->pin_int_en;

	if (pins != 0u) {
		gpio_fire_callbacks(&data->callbacks, dev, pins);
	}
}

#define GPIO_B87_MAX_PASSES 8

static void gpio_b87_irq_handler(const void *unused)
{
	gpio_port_pins_t handled;
	bool both_only = false;
	int passes = 0;

	ARG_UNUSED(unused);

	gpio_b87_clear_source();

	/*
	 * The interrupt is an edge of the OR of all pins' requests. A both-edge
	 * pin that changes again after its read above, or while another pin
	 * keeps the request high, makes no new edge: handle those here until
	 * none is left. A pin that keeps changing faster than a pass raises the
	 * request again after the last pass, so the passes can stop.
	 */
	do {
		handled = 0u;
#define GPIO_B87_DISPATCH(n)                                                                  \
	handled |= gpio_b87_dispatch(DEVICE_DT_INST_GET(n), both_only);
		DT_INST_FOREACH_STATUS_OKAY(GPIO_B87_DISPATCH)
#undef GPIO_B87_DISPATCH
		if (!both_only && handled == 0u) {
			/*
			 * An edge with no pin at its active level: a both-edge pin
			 * went and came back (after an earlier read, or before this
			 * one). Its callback reads the level again.
			 */
#define GPIO_B87_FIRE_BOTH(n) gpio_b87_fire_both(DEVICE_DT_INST_GET(n));
			DT_INST_FOREACH_STATUS_OKAY(GPIO_B87_FIRE_BOTH)
#undef GPIO_B87_FIRE_BOTH
		}
		both_only = true;
	} while (handled != 0u && ++passes < GPIO_B87_MAX_PASSES);
}

static void gpio_b87_irq_connect(void)
{
	IRQ_CONNECT(TLSR827X_IRQ_GPIO, 0, gpio_b87_irq_handler, NULL, 0);
}

static bool gpio_b87_any_interrupt_enabled(void)
{
	bool enabled = false;

#define GPIO_B87_CHECK_INT(n)						\
	do {									\
		const struct device *dev = DEVICE_DT_INST_GET(n);		\
		const struct gpio_b87_data *data = dev->data;		\
										\
		enabled = enabled || data->pin_int_en != 0u;			\
	} while (false);
	DT_INST_FOREACH_STATUS_OKAY(GPIO_B87_CHECK_INT)
#undef GPIO_B87_CHECK_INT

	return enabled;
}

static int gpio_b87_init(const struct device *dev)
{
	struct gpio_b87_data *data = dev->data;

	data->input_en = 0u;
	data->output_en = 0u;
	data->pin_int_en = 0u;
	data->trig_low = 0u;
	data->trig_both = 0u;

	gpio_b87_irq_connect();
	return 0;
}

static int gpio_b87_pin_configure(const struct device *dev, gpio_pin_t pin,
				       gpio_flags_t flags)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);
	struct gpio_b87_data *data = dev->data;
	uint16_t vendor_pin;
	uint8_t mask;

	if (!gpio_b87_pin_valid(pin)) {
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
	vendor_pin = gpio_b87_vendor_pin(dev, pin);

	/*
	 * The pad must never drive while it changes over: an input gives up
	 * its output before the pad becomes GPIO, and an output has its level
	 * before its output is enabled. Nothing is assumed of the reset
	 * values of the OEN and GPIO function registers.
	 */
	if ((flags & GPIO_OUTPUT) == 0u) {
		regs->oen |= mask;
		data->output_en &= ~mask;
	}

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0u) {
		regs->output |= mask;
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0u) {
		regs->output &= (uint8_t)~mask;
	}

	regs->func |= mask;

	if ((flags & GPIO_OUTPUT) != 0u) {
		regs->oen &= (uint8_t)~mask;
		data->output_en |= mask;
	}

	if ((flags & GPIO_INPUT) != 0u) {
		gpio_b87_ie_update(dev, mask, true);
		data->input_en |= mask;
	} else {
		gpio_b87_ie_update(dev, mask, false);
		data->input_en &= ~mask;
	}

	if ((flags & GPIO_PULL_UP) != 0u) {
		gpio_b87_pull(vendor_pin, GPIO_B87_PULL_UP_10K);
	} else if ((flags & GPIO_PULL_DOWN) != 0u) {
		gpio_b87_pull(vendor_pin, GPIO_B87_PULL_DOWN_100K);
	} else {
		gpio_b87_pull(vendor_pin, GPIO_B87_PULL_FLOAT);
	}


	return 0;
}


static int gpio_b87_port_get_raw(const struct device *dev,
				      gpio_port_value_t *value)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);

	*value = regs->input;
	return 0;
}

static int gpio_b87_port_set_masked_raw(const struct device *dev,
					     gpio_port_pins_t mask,
					     gpio_port_value_t value)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);

	regs->output = (regs->output & (uint8_t)~mask) | (value & mask);
	return 0;
}

static int gpio_b87_port_set_bits_raw(const struct device *dev,
					   gpio_port_pins_t pins)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);

	regs->output |= pins;
	return 0;
}

static int gpio_b87_port_clear_bits_raw(const struct device *dev,
					     gpio_port_pins_t pins)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);

	regs->output &= (uint8_t)~pins;
	return 0;
}

static int gpio_b87_port_toggle_bits(const struct device *dev,
					  gpio_port_pins_t pins)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);

	regs->output ^= pins;
	return 0;
}

static int gpio_b87_pin_interrupt_configure(const struct device *dev,
						 gpio_pin_t pin,
						 enum gpio_int_mode mode,
						 enum gpio_int_trig trig)
{
	volatile struct gpio_b87_regs *regs = gpio_b87_regs(dev);
	struct gpio_b87_data *data = dev->data;
	unsigned int key;
	uint8_t mask;

	if (!gpio_b87_pin_valid(pin)) {
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
		gpio_b87_clear_source();
		if (!gpio_b87_any_interrupt_enabled()) {
			irq_disable(TLSR827X_IRQ_GPIO);
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

	*GPIO_B87_WAKEUP_IRQ |= GPIO_B87_CORE_INTERRUPT_EN;

	/*
	 * Clear once after the polarity setup and, for a single edge, again
	 * after enabling the pin: some boards latch one stale edge then, and a
	 * pin already at its level must not count as an edge. For both edges
	 * the second clear could drop this pin's change since its read above;
	 * a stale edge only makes the handler run with no pin active, and it
	 * then re-fires the both-edge callbacks, which read the level.
	 */
	gpio_b87_clear_source();
	data->pin_int_en |= mask;
	regs->irq_en |= mask;
	if (trig != GPIO_INT_TRIG_BOTH) {
		gpio_b87_clear_source();
	}
	irq_enable(TLSR827X_IRQ_GPIO);

	irq_unlock(key);
	return 0;
}

static int gpio_b87_manage_callback(const struct device *dev,
					 struct gpio_callback *callback,
					 bool set)
{
	struct gpio_b87_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}


static DEVICE_API(gpio, gpio_b87_api) = {
	.pin_configure = gpio_b87_pin_configure,
	.port_get_raw = gpio_b87_port_get_raw,
	.port_set_masked_raw = gpio_b87_port_set_masked_raw,
	.port_set_bits_raw = gpio_b87_port_set_bits_raw,
	.port_clear_bits_raw = gpio_b87_port_clear_bits_raw,
	.port_toggle_bits = gpio_b87_port_toggle_bits,
	.pin_interrupt_configure = gpio_b87_pin_interrupt_configure,
	.manage_callback = gpio_b87_manage_callback,
};

#define GPIO_B87_INIT(n)							\
	static const struct gpio_b87_config gpio_b87_config_##n = {	\
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),			\
		.base = DT_INST_REG_ADDR(n),					\
		.group = (DT_INST_REG_ADDR(n) - 0x00800580u) / 8u,		\
	};									\
	static struct gpio_b87_data gpio_b87_data_##n;		\
										\
	DEVICE_DT_INST_DEFINE(n, gpio_b87_init, NULL,			\
			      &gpio_b87_data_##n,			\
			      &gpio_b87_config_##n,			\
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,		\
			      &gpio_b87_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_B87_INIT)
