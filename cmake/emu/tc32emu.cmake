# SPDX-License-Identifier: Apache-2.0

# TC32-devtools' emulator for TC32 boards that list tc32emu in
# SUPPORTED_EMU_PLATFORMS: the `run` target, which twister runs for a board
# whose simulation is `custom`.
#
# The build writes the TC32 image ${KERNEL_NAME}.tc32.bin from the linked
# Thumb ELF with thumb2tc32.py, or on the direct path (TC32_DIRECT) from the
# TC32 ELF with elf2bin.py, and checks every byte of it against the ELF with
# image_check.py (all from TC32_THUMB_TOOLS). `run` boots that image from
# reset in the emulator and prints the console (LOG_BACKEND_TC32EMU) on stdout
# line by line as the image writes it. The run ends at ztest's last line, at
# TC32EMU_MS milliseconds of simulated time after the last boot, or when the
# caller stops it.
#
# TC32EMU (CMake cache or environment): the emulator's run-boot program, the
# Go build go/bin/run-boot or emulator/run_boot.py of a TC32-devtools checkout.
# TC32EMU_MS (CMake cache or environment): the cap on simulated time per run,
# in milliseconds; 600000 when not set.
# TC32EMU_RESETS (CMake cache or environment): how many watchdog and software
# resets the run follows, as the chip restarts the image after one; 4 when not
# set. Each boot gets TC32EMU_MS again, and the SRAM is kept, as on the chip.
#
# With a TLSR8278 USB device controller driver in the image (UDC_TELINK_B87, or
# USB_DC_TELINK_B87 of an application's module) the run attaches the emulator's model of the controller
# with its host idle (run-boot --usb): the controller's registers then behave
# as on the chip, and no host enumerates the device.

set(tc32emu_elf ${PROJECT_BINARY_DIR}/${KERNEL_ELF_NAME})
set(tc32emu_bin ${PROJECT_BINARY_DIR}/${KERNEL_NAME}.tc32.bin)
if(TC32_DIRECT)
  set(tc32emu_writer ${TC32_THUMB_TOOLS}/elf2bin.py)
else()
  set(tc32emu_writer ${TC32_THUMB_TOOLS}/thumb2tc32.py)
endif()

add_custom_command(
  OUTPUT ${tc32emu_bin}
  COMMAND ${PYTHON_EXECUTABLE} ${tc32emu_writer}
          ${tc32emu_elf} ${tc32emu_bin}.tmp
  COMMAND ${PYTHON_EXECUTABLE} ${TC32_THUMB_TOOLS}/image_check.py
          ${tc32emu_elf} ${tc32emu_bin}.tmp
  COMMAND ${CMAKE_COMMAND} -E rename ${tc32emu_bin}.tmp ${tc32emu_bin}
  DEPENDS ${logical_target_for_zephyr_elf} ${tc32emu_elf}
  COMMENT "Writing and checking the TC32 image ${KERNEL_NAME}.tc32.bin"
)
add_custom_target(tc32emu_image ALL DEPENDS ${tc32emu_bin})

zephyr_get(TC32EMU)
zephyr_get(TC32EMU_MS)
if(NOT TC32EMU_MS)
  set(TC32EMU_MS 600000)
endif()
zephyr_get(TC32EMU_RESETS)
if(NOT DEFINED TC32EMU_RESETS OR TC32EMU_RESETS STREQUAL "")
  set(TC32EMU_RESETS 4)
endif()
set(tc32emu_options)
if(CONFIG_UDC_TELINK_B87)
  list(APPEND tc32emu_options --usb)
endif()

if(NOT TC32EMU)
  set(tc32emu_error "TC32EMU is not set: give the path of TC32-devtools' go/bin/run-boot \
or emulator/run_boot.py in the environment or with -DTC32EMU=<path>")
elseif(NOT EXISTS ${TC32EMU})
  set(tc32emu_error "TC32EMU is ${TC32EMU}, which does not exist")
endif()

if(DEFINED tc32emu_error)
  add_custom_target(run_tc32emu
    COMMAND ${CMAKE_COMMAND} -E echo "${tc32emu_error}"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
  )
  return()
endif()

if(TC32EMU MATCHES "\\.py$")
  set(tc32emu_command ${PYTHON_EXECUTABLE} -B ${TC32EMU})
else()
  set(tc32emu_command ${TC32EMU})
endif()

add_custom_target(run_tc32emu
  COMMAND
  ${tc32emu_command}
  --elf ${tc32emu_elf}
  --bin ${tc32emu_bin}
  --ms ${TC32EMU_MS}
  --resets ${TC32EMU_RESETS}
  ${tc32emu_options}
  --console --console-stream
  --fatal-continue
  --stacks
  --stop-line "PROJECT EXECUTION SUCCESSFUL"
  --stop-line "PROJECT EXECUTION FAILED"
  WORKING_DIRECTORY ${APPLICATION_BINARY_DIR}
  DEPENDS ${tc32emu_bin}
  USES_TERMINAL
)
