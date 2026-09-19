# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(soc.c)
zephyr_sources_ifdef(CONFIG_SOC_SERIES_TLSR825X_PM power.c power.S)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/tc32/linker.ld CACHE INTERNAL "")
