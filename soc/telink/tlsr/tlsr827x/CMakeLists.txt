# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_sources(soc.c)
zephyr_sources_ifdef(CONFIG_SOC_TLSR8278_RADIO tlsr_radio.c)
zephyr_sources_ifdef(CONFIG_SOC_TLSR8278_MSPI tlsr8278_mspi.c)
zephyr_sources_ifdef(CONFIG_SOC_TLSR8278_PKE tlsr_pke.c)
if(CONFIG_SOC_TLSR8278_ASM_SETUP)
  tc32_asm_source(setup_asm setup/tlsr8278_setup.S)
  zephyr_sources(${setup_asm})
endif()

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/tc32/linker.ld CACHE INTERNAL "")
