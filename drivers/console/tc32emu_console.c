/*
 * Copyright (c) 2026 Go Yamamoto
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The TC32 emulator's console: printk and stdout as bytes written to
 * address 0x80fff0, which TC32-devtools' run-boot --console collects. Not a
 * register of the chip; emulator images only.
 */

#include <zephyr/init.h>
#include <zephyr/sys/libc-hooks.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/sys/sys_io.h>

#define TC32EMU_CONSOLE_ADDR 0x80fff0

static int tc32emu_console_out(int c)
{
	sys_write8((uint8_t)c, TC32EMU_CONSOLE_ADDR);
	return c;
}

static int tc32emu_console_init(void)
{
#ifdef CONFIG_PRINTK
	__printk_hook_install(tc32emu_console_out);
#endif
	__stdout_hook_install(tc32emu_console_out);
	return 0;
}

SYS_INIT(tc32emu_console_init, PRE_KERNEL_1, CONFIG_CONSOLE_INIT_PRIORITY);
