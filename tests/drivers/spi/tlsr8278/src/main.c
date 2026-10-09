/*
 * The TLSR8278 SPI master on tlsr8278_generic, in TC32-devtools' emulator,
 * with the emulator's SPI device (its registers 0xffd8 and 0xffdc): the
 * device gets octets only through the pins the pin controller routes to the
 * SPI and while its chip select is active, keeps what it gets and answers
 * with a count. Writes, writes then reads, the refusal of a full-duplex
 * frame, the clock divider and mode registers, a GPIO chip select (active
 * high) and the master's own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>

#define DEV_CFG  0x0080ffd8U /* emulator only */
#define DEV_FIFO 0x0080ffdcU /* emulator only */
#define SPI_CLK  0x0080000aU
#define SPI_MODE 0x0080000bU
#define SPI_CTRL 0x00800009U
#define PD_OUT   0x0080059bU

#define OP (SPI_WORD_SET(8) | SPI_OP_MODE_CONTROLLER)

static const struct spi_dt_spec gpio_cs = SPI_DT_SPEC_GET(DT_NODELABEL(gpio_cs_dev), OP);
static const struct spi_dt_spec own_cs = SPI_DT_SPEC_GET(DT_NODELABEL(own_cs_dev), OP);

/* A device whose chip select is PD5 (active high) or the master's own; it
 * answers with a count from 0x40.
 */
static void device(bool own)
{
	sys_write32((own ? 0xffU << 16 : (BIT(24) | (0x1dU << 16))) | (0x40U << 8) | 2U, DEV_CFG);
}

static uint32_t transactions(void)
{
	return sys_read32(DEV_CFG) >> 16;
}

static uint32_t octets(void)
{
	return sys_read32(DEV_CFG) & 0xffffU;
}

ZTEST(spi_tlsr8278, test_write)
{
	uint8_t tx[] = {0x20, 0x00, 0x11, 0x22};
	struct spi_buf b = {.buf = tx, .len = sizeof(tx)};
	struct spi_buf_set s = {.buffers = &b, .count = 1};

	zassert_true(spi_is_ready_dt(&gpio_cs));
	device(false);
	zassert_ok(spi_write_dt(&gpio_cs, &s));
	zassert_equal(transactions(), 1U);
	zassert_equal(octets(), sizeof(tx));
	for (size_t i = 0; i < sizeof(tx); i++) {
		zassert_equal(sys_read8(DEV_FIFO), tx[i], "octet %u", (unsigned int)i);
	}
	/* 4 MHz from 48 MHz: divider 5, the SPI function on; mode 0; chip select low again. */
	zassert_equal(sys_read8(SPI_CLK), 0x85U, "0x0a 0x%02x", sys_read8(SPI_CLK));
	zassert_equal(sys_read8(SPI_MODE), 0U);
	zassert_equal(sys_read8(PD_OUT) & BIT(5), 0U, "PD5 left high");

	/* A second write is a second transaction. */
	zassert_ok(spi_write_dt(&gpio_cs, &s));
	zassert_equal(transactions(), 2U);
	zassert_equal(octets(), 2U * sizeof(tx));
}

ZTEST(spi_tlsr8278, test_write_then_read)
{
	uint8_t cmd[] = {0x03, 0x10};
	uint8_t in[3] = {0};
	struct spi_buf txb = {.buf = cmd, .len = sizeof(cmd)};
	struct spi_buf rxb[] = {{.buf = NULL, .len = sizeof(cmd)}, {.buf = in, .len = sizeof(in)}};
	struct spi_buf_set tx = {.buffers = &txb, .count = 1};
	struct spi_buf_set rx = {.buffers = rxb, .count = 2};

	device(false);
	zassert_ok(spi_transceive_dt(&gpio_cs, &tx, &rx));
	/* The device counted the two octets it got and answered 0x42-0x44 to the three read. */
	zassert_equal(octets(), 2U);
	zassert_equal(in[0], 0x42, "0x%02x", in[0]);
	zassert_equal(in[1], 0x43);
	zassert_equal(in[2], 0x44);
	/* Exactly five octets clocked: the next answer is 0x45. */
	zassert_ok(spi_transceive_dt(&gpio_cs, NULL, &(struct spi_buf_set){
		.buffers = &(struct spi_buf){.buf = in, .len = 1}, .count = 1}));
	zassert_equal(in[0], 0x45, "an octet more was clocked: 0x%02x", in[0]);
}

ZTEST(spi_tlsr8278, test_full_duplex_refused)
{
	uint8_t out[2] = {1, 2}, in[2];
	struct spi_buf txb = {.buf = out, .len = 2};
	struct spi_buf rxb = {.buf = in, .len = 2};
	struct spi_buf_set tx = {.buffers = &txb, .count = 1};
	struct spi_buf_set rx = {.buffers = &rxb, .count = 1};

	device(false);
	zassert_equal(spi_transceive_dt(&gpio_cs, &tx, &rx), -ENOTSUP);
	zassert_equal(octets(), 0U, "octets sent anyway");
	zassert_equal(transactions(), 0U);
}

ZTEST(spi_tlsr8278, test_own_chip_select)
{
	uint8_t tx[] = {0xa5, 0x5a};
	struct spi_buf b = {.buf = tx, .len = sizeof(tx)};
	struct spi_buf_set s = {.buffers = &b, .count = 1};

	device(true);
	zassert_ok(spi_write_dt(&own_cs, &s));
	zassert_equal(transactions(), 1U);
	zassert_equal(octets(), 2U);
	zassert_equal(sys_read8(DEV_FIFO), 0xa5);
	zassert_equal(sys_read8(DEV_FIFO), 0x5a);
	/* 1 MHz: divider 23; CPOL and CPHA; the own chip select high again. */
	zassert_equal(sys_read8(SPI_CLK), 0x80U | 23U);
	zassert_equal(sys_read8(SPI_MODE), 0x03U);
	zassert_equal(sys_read8(SPI_CTRL) & BIT(0), BIT(0));
}

ZTEST(spi_tlsr8278, test_slowest_clock)
{
	static struct spi_config slow;
	uint8_t tx = 0x77;
	struct spi_buf b = {.buf = &tx, .len = 1};
	struct spi_buf_set s = {.buffers = &b, .count = 1};

	slow = gpio_cs.config;
	slow.frequency = 100000;
	device(false);
	zassert_ok(spi_write(gpio_cs.bus, &slow, &s));
	/* 48 MHz / (2 * 100 kHz) is 240: the divider's largest, 127. */
	zassert_equal(sys_read8(SPI_CLK), 0xffU);
	zassert_equal(octets(), 1U);
}

ZTEST_SUITE(spi_tlsr8278, NULL, NULL, NULL, NULL, NULL);
