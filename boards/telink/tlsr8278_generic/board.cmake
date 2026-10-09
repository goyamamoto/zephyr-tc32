# SPDX-License-Identifier: Apache-2.0
# A build and emulator target, without a flash runner. The SoC code it builds
# (startup, setup, GPIO, system timer, USB device controller, suspend) runs on
# TLSR8278 hardware in keyboard firmware built on this tree. Images built for
# this board run in TC32-devtools' emulator (`run`, cmake/emu/tc32emu.cmake).
set(SUPPORTED_EMU_PLATFORMS tc32emu)
