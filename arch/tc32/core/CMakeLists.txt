# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
  cpu_idle.c
  compiler_builtins.c
  early_mem.S
  flash_copy.S
  fatal.c
  irq_manage.c
  reboot.c
  reset.S
  switch.S
  thread.c
)

zephyr_library_sources_ifdef(CONFIG_IRQ_OFFLOAD irq_offload.c)
