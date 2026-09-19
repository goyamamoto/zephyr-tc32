/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

FUNC_NORETURN void z_tc32_fatal_error(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_ERR("fatal error on TC32: %u", reason);
	z_fatal_error(reason, esf);
	CODE_UNREACHABLE;
}
