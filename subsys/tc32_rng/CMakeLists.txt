# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(tc32_aes.c)
zephyr_library_sources_ifdef(CONFIG_TC32_RNG tc32_rng.c)
