# SPDX-License-Identifier: Apache-2.0

set_property(GLOBAL PROPERTY PROPERTY_OUTPUT_FORMAT "elf32-littletc32")

zephyr_compile_options(-mcpu=tc32)
zephyr_ld_options(-mcpu=tc32)

add_subdirectory(core)
zephyr_include_directories(include)
zephyr_include_directories(${ZEPHYR_BASE}/soc)
