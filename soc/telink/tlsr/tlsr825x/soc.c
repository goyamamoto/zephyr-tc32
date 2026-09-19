/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>

#define TLSR8258_REG_RST_CLK0 (*(volatile uint32_t *)0x00800060u)
#define TLSR8258_REG_RST_CLK1 (*(volatile uint32_t *)0x00800064u)

#define TLSR8258_REG_TMR_CTRL (*(volatile uint32_t *)0x00800620u)
#define TLSR8258_FLD_TMR_WD_EN BIT(23)

void soc_prep_hook(void)
{
	/*
	 * Match the direct register part of vendor cpu_wakeup_init(): release
	 * peripheral resets and enable clock gates before PRE_KERNEL devices.
	 */
	TLSR8258_REG_RST_CLK0 = 0xff000000u;
	TLSR8258_REG_RST_CLK1 = 0x0006ffffu;

	/* Match vendor wd_stop()/WATCHDOG_DISABLE before C runtime setup. */
	TLSR8258_REG_TMR_CTRL &= ~TLSR8258_FLD_TMR_WD_EN;
}
