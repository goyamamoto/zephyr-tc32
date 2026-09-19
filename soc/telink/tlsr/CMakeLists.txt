# Copyright (c) 2024 Nordic Semiconductor
# SPDX-License-Identifier: Apache-2.0

add_subdirectory(${CONFIG_SOC_SERIES})

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_SOC_SERIES})
