# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_TC32_P256_SOFT tc32_p256_soft.c p256-m/p256-m.c)
zephyr_library_include_directories_ifdef(CONFIG_TC32_P256_SOFT p256-m)
