# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
  cpu_idle.c
  compiler_builtins.c
  fatal.c
  irq_manage.c
  thread.c
  udiv64_const.c
)
tc32_asm_sources(
  reset.S
  switch.S
)
# The vectors and the startup from setup/tlsr8278_startup.S; reset.S then
# keeps the interrupt entry only. The file has Telink mnemonics (the entry at
# 0x98, the call to z_cstart), so it goes the way reset.S goes.
if(CONFIG_SOC_TLSR8278_STARTUP)
  tc32_asm_sources(${ZEPHYR_BASE}/soc/telink/tlsr/tlsr827x/setup/tlsr8278_startup.S)
endif()
zephyr_library_sources(aeabi_thumb.S softfloat_aeabi.c)

# Soft-float arithmetic, comparisons and conversions (softfloat/README.txt).
zephyr_library_sources(
  softfloat/adddf3.c
  softfloat/addsf3.c
  softfloat/comparedf2.c
  softfloat/comparesf2.c
  softfloat/divdf3.c
  softfloat/divsf3.c
  softfloat/extendsfdf2.c
  softfloat/fixdfdi.c
  softfloat/fixdfsi.c
  softfloat/fixsfdi.c
  softfloat/fixsfsi.c
  softfloat/fixunsdfdi.c
  softfloat/fixunsdfsi.c
  softfloat/fixunssfdi.c
  softfloat/fixunssfsi.c
  softfloat/floatdidf.c
  softfloat/floatdisf.c
  softfloat/floatsidf.c
  softfloat/floatsisf.c
  softfloat/floatundidf.c
  softfloat/floatundisf.c
  softfloat/floatunsidf.c
  softfloat/floatunsisf.c
  softfloat/fp_mode.c
  softfloat/muldf3.c
  softfloat/mulsf3.c
  softfloat/negdf2.c
  softfloat/negsf2.c
  softfloat/subdf3.c
  softfloat/subsf3.c
  softfloat/truncdfsf2.c
)
file(GLOB tc32_softfloat_sources ${CMAKE_CURRENT_SOURCE_DIR}/softfloat/*.c)
set_source_files_properties(${tc32_softfloat_sources} PROPERTIES COMPILE_OPTIONS -Wno-double-promotion)

zephyr_library_sources_ifdef(CONFIG_IRQ_OFFLOAD irq_offload.c)
