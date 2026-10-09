/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The TC32 emulator's console as a log backend: log messages as bytes
 * written to address 0x80fff0, which TC32-devtools' run-boot --console
 * collects (printk and stdout: drivers/console/tc32emu_console.c). Not a
 * register of the chip; emulator images only.
 */

#include <stddef.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/sys_io.h>

#define TC32EMU_CONSOLE_ADDR 0x80fff0

static uint8_t tc32emu_log_buf[1];
static uint32_t log_format_current = CONFIG_LOG_BACKEND_TC32EMU_OUTPUT_DEFAULT;

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	for (size_t i = 0; i < length; i++) {
		sys_write8(data[i], TC32EMU_CONSOLE_ADDR);
	}

	return length;
}

LOG_OUTPUT_DEFINE(log_output_tc32emu, char_out, tc32emu_log_buf, sizeof(tc32emu_log_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	uint32_t flags = log_backend_std_get_flags();

	log_format_func_t log_output_func = log_format_func_t_get(log_format_current);

	log_output_func(&log_output_tc32emu, &msg->log, flags);
}

static int format_set(const struct log_backend *const backend, uint32_t log_type)
{
	log_format_current = log_type;
	return 0;
}

static void panic(struct log_backend const *const backend)
{
	log_backend_std_panic(&log_output_tc32emu);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	log_backend_std_dropped(&log_output_tc32emu, cnt);
}

const struct log_backend_api log_backend_tc32emu_api = {
	.process = process,
	.panic = panic,
	.dropped = IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE) ? NULL : dropped,
	.format_set = format_set,
};

LOG_BACKEND_DEFINE(log_backend_tc32emu, log_backend_tc32emu_api, true);
