/*
 * Telink TLSR8278 SPI master (DS-TLSR8278 7.4, Table 7-7).
 *
 * The registers: 0x08 data; 0x09 control (bit 0 the master's own chip select
 * level, bit 1 master mode, bit 2 data output off, bit 3 read, bit 6 busy
 * while the 8 clocks of an octet run); 0x0a bits 6:0 the clock divider, bit
 * 7 the SPI function of the pins; 0x0b the mode (bit 0 CPOL, bit 1 CPHA).
 * The SPI clock is the system clock / ((divider + 1) * 2).
 *
 * The master clocks an octet out or in, not both at once: with 0x09 bit 3
 * clear a write of 0x08 sends the octet, and with bit 3 set and the output
 * off a read of 0x08 gives the octet clocked in last and clocks the next one
 * in. So an octet is either sent or received: a transfer writes the frames
 * that have something to send (a NULL TX buffer sends zeros) and reads the
 * frames that come after the TX buffers end, and refuses one that asks to
 * keep what comes in while it sends (-ENOTSUP). Reading n octets takes n
 * clocked octets: bit 3 is cleared before the last read, so that it clocks
 * nothing more.
 *
 * The chip select is a GPIO (cs-gpios) or, without one, the master's own
 * (0x09 bit 0, low while selected), which the pins give out as SPI_CN when
 * the pin controller routes them so. The registers are set for each
 * transfer: a chip suspend between two keeps them, a deep sleep does not.
 *
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_b87_spi

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spi_tlsr8278, CONFIG_SPI_LOG_LEVEL);

#include "spi_context.h"

#define SPI_DATA(b) ((b) + 0x00U)
#define SPI_CTRL(b) ((b) + 0x01U)
#define SPI_CLK(b)  ((b) + 0x02U)
#define SPI_MODE(b) ((b) + 0x03U)

#define CTRL_CS_HIGH BIT(0)
#define CTRL_MASTER  BIT(1)
#define CTRL_DO_OFF  BIT(2)
#define CTRL_READ    BIT(3)
#define CTRL_BUSY    BIT(6)
#define CLK_FUNC     BIT(7)
#define CLK_DIV_MAX  0x7fU
#define MODE_CPOL    BIT(0)
#define MODE_CPHA    BIT(1)

#define STIMER          0x00800740U
/* An octet takes at most 16 * 128 system clock cycles, 43 us at 48 MHz. */
#define OCTET_WAIT_TICKS (1000U * 16U)

struct spi_tlsr8278_config {
	uintptr_t base;
	const struct device *clk;
	clock_control_subsys_t clk_id;
	const struct pinctrl_dev_config *pcfg;
};

struct spi_tlsr8278_data {
	struct spi_context ctx;
	uint8_t div;
	uint8_t mode;
};

static int octet_wait(uintptr_t base)
{
	uint32_t t0 = sys_read32(STIMER);

	while ((sys_read8(SPI_CTRL(base)) & CTRL_BUSY) != 0U) {
		if (sys_read32(STIMER) - t0 > OCTET_WAIT_TICKS) {
			return -EIO;
		}
	}
	return 0;
}

static void ctrl_set(uintptr_t base, uint8_t mask, uint8_t v)
{
	sys_write8((sys_read8(SPI_CTRL(base)) & (uint8_t)~mask) | (v & mask), SPI_CTRL(base));
}

static int spi_tlsr8278_configure(const struct device *dev, const struct spi_config *config)
{
	const struct spi_tlsr8278_config *cfg = dev->config;
	struct spi_tlsr8278_data *data = dev->data;
	uint32_t rate, div;
	int err;

	if (spi_context_configured(&data->ctx, config)) {
		return 0;
	}
	if (SPI_OP_MODE_GET(config->operation) != SPI_OP_MODE_CONTROLLER ||
	    SPI_WORD_SIZE_GET(config->operation) != 8U ||
	    (config->operation & SPI_TRANSFER_LSB) != 0U ||
	    (IS_ENABLED(CONFIG_SPI_EXTENDED_MODES) &&
	     (config->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) ||
	    (config->operation & SPI_MODE_LOOP) != 0U) {
		return -ENOTSUP;
	}
	if (!spi_cs_is_gpio(config) && (config->operation & SPI_CS_ACTIVE_HIGH) != 0U) {
		return -ENOTSUP;
	}
	if (config->frequency == 0U) {
		return -EINVAL;
	}
	err = clock_control_get_rate(cfg->clk, cfg->clk_id, &rate);
	if (err != 0) {
		return err;
	}
	/* The fastest clock not above the one asked for, or the slowest the divider gives. */
	div = DIV_ROUND_UP(rate, 2U * config->frequency);
	div = CLAMP(div, 1U, CLK_DIV_MAX + 1U) - 1U;

	data->div = (uint8_t)div;
	data->mode = ((SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) != 0U ? MODE_CPOL : 0U) |
		     ((SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) != 0U ? MODE_CPHA : 0U);
	data->ctx.config = config;
	return 0;
}

/* Every frame of the TX buffers and every RX frame after them; -ENOTSUP for an RX buffer
 * that would take what comes in while an octet is sent.
 */
static int check_buffers(const struct spi_buf_set *tx, const struct spi_buf_set *rx)
{
	size_t tx_len = 0U, pos = 0U;

	if (tx != NULL) {
		for (size_t i = 0U; i < tx->count; i++) {
			tx_len += tx->buffers[i].len;
		}
	}
	if (rx != NULL) {
		for (size_t i = 0U; i < rx->count; i++) {
			if (rx->buffers[i].buf != NULL && rx->buffers[i].len != 0U &&
			    pos < tx_len) {
				return -ENOTSUP;
			}
			pos += rx->buffers[i].len;
		}
	}
	return 0;
}

static int transfer(const struct device *dev)
{
	const struct spi_tlsr8278_config *cfg = dev->config;
	struct spi_tlsr8278_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uintptr_t base = cfg->base;
	int err = 0;

	sys_write8(data->div, SPI_CLK(base));
	sys_write8(data->div | CLK_FUNC, SPI_CLK(base));
	sys_write8(data->mode, SPI_MODE(base));
	ctrl_set(base, CTRL_MASTER | CTRL_DO_OFF | CTRL_READ | CTRL_CS_HIGH,
		 CTRL_MASTER | CTRL_CS_HIGH);

	if (!spi_cs_is_gpio(ctx->config)) {
		ctrl_set(base, CTRL_CS_HIGH, 0U);
	}
	spi_context_cs_control(ctx, true);

	/* The frames to send; an RX frame that comes with one keeps nothing. */
	while (err == 0 && spi_context_tx_on(ctx)) {
		sys_write8(spi_context_tx_buf_on(ctx) ? *ctx->tx_buf : 0U, SPI_DATA(base));
		err = octet_wait(base);
		spi_context_update_tx(ctx, 1, 1);
		if (spi_context_rx_on(ctx)) {
			spi_context_update_rx(ctx, 1, 1);
		}
	}

	/* The frames to receive: the first read only starts the first octet. */
	if (err == 0 && spi_context_rx_on(ctx)) {
		ctrl_set(base, CTRL_DO_OFF | CTRL_READ, CTRL_DO_OFF | CTRL_READ);
		(void)sys_read8(SPI_DATA(base));
		err = octet_wait(base);
		while (err == 0 && spi_context_rx_on(ctx)) {
			uint8_t v;

			if (spi_context_rx_len_left(ctx, 1) == 1U) {
				/* The last octet: this read clocks no other in. */
				ctrl_set(base, CTRL_READ, 0U);
			}
			v = sys_read8(SPI_DATA(base));
			if (spi_context_rx_buf_on(ctx)) {
				*ctx->rx_buf = v;
			}
			spi_context_update_rx(ctx, 1, 1);
			if (spi_context_rx_on(ctx)) {
				err = octet_wait(base);
			}
		}
		ctrl_set(base, CTRL_DO_OFF | CTRL_READ, 0U);
	}

	if ((ctx->config->operation & SPI_HOLD_ON_CS) == 0U) {
		spi_context_cs_control(ctx, false);
		if (!spi_cs_is_gpio(ctx->config)) {
			ctrl_set(base, CTRL_CS_HIGH, CTRL_CS_HIGH);
		}
	}
	return err;
}

static int spi_tlsr8278_transceive(const struct device *dev, const struct spi_config *config,
				   const struct spi_buf_set *tx_bufs,
				   const struct spi_buf_set *rx_bufs)
{
	struct spi_tlsr8278_data *data = dev->data;
	int err;

	spi_context_lock(&data->ctx, false, NULL, NULL, config);
	err = spi_tlsr8278_configure(dev, config);
	if (err == 0) {
		err = check_buffers(tx_bufs, rx_bufs);
	}
	if (err == 0) {
		spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);
		err = transfer(dev);
	}
	spi_context_release(&data->ctx, err);
	return err;
}

static int spi_tlsr8278_release(const struct device *dev, const struct spi_config *config)
{
	struct spi_tlsr8278_data *data = dev->data;

	if (!spi_context_configured(&data->ctx, config)) {
		return -EINVAL;
	}
	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static DEVICE_API(spi, spi_tlsr8278_api) = {
	.transceive = spi_tlsr8278_transceive,
	.release = spi_tlsr8278_release,
};

static int spi_tlsr8278_init(const struct device *dev)
{
	const struct spi_tlsr8278_config *cfg = dev->config;
	struct spi_tlsr8278_data *data = dev->data;
	int err;

	if (!device_is_ready(cfg->clk)) {
		return -ENODEV;
	}
	err = clock_control_on(cfg->clk, cfg->clk_id);
	if (err != 0) {
		return err;
	}
	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err != 0) {
		return err;
	}
	err = spi_context_cs_configure_all(&data->ctx);
	if (err != 0) {
		return err;
	}
	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

#define SPI_TLSR8278_INIT(n)                                                                       \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static const struct spi_tlsr8278_config spi_tlsr8278_cfg_##n = {                           \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.clk = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                      \
		.clk_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id),                      \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
	};                                                                                         \
	static struct spi_tlsr8278_data spi_tlsr8278_data_##n = {                                  \
		SPI_CONTEXT_INIT_LOCK(spi_tlsr8278_data_##n, ctx),                                 \
		SPI_CONTEXT_INIT_SYNC(spi_tlsr8278_data_##n, ctx),                                 \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx)};                             \
	SPI_DEVICE_DT_INST_DEFINE(n, spi_tlsr8278_init, NULL, &spi_tlsr8278_data_##n,              \
				  &spi_tlsr8278_cfg_##n, POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,    \
				  &spi_tlsr8278_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_TLSR8278_INIT)
