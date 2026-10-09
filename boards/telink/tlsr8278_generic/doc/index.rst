.. zephyr:board:: tlsr8278_generic

Overview
********

``tlsr8278_generic`` is a board for the Telink TLSR8278 SoC whose images run in the TC32
emulator of `TC32-devtools`_. It has no flash runner. Tests and samples for the SoC are
built and run with it, also under twister.

Hardware
********

- TLSR8278 SoC (Telink B87 family, ``tlsr827x`` series): TC32 CPU at 48 MHz, 24 MHz crystal,
  32 kHz RC oscillator
- 64 KB SRAM, 1 MB flash; the application at the start of the flash
- System timer, GPIO, USB device controller
- Flash, hwinfo, watchdog, pin control, clock control, ADC and SPI master nodes, enabled on
  this board for the drivers' tests

The board sets ``CONFIG_TIMEOUT_64BIT=y`` so that the kernel tests using absolute timeouts
build.

Supported Features
==================

.. zephyr:board-supported-hw::

Building and running
********************

The build needs clang and ld.lld (Zephyr's ``host/llvm`` toolchain variant) and
`TC32-devtools`_, which translates the port's assembly, writes the TC32 image and runs it.
``TC32.md`` at the top of the tree gives the full requirements.

.. code-block:: console

   export ZEPHYR_BASE=$PWD TC32_DEVTOOLS=/path/to/TC32-devtools
   export TC32EMU=$TC32_DEVTOOLS/go/bin/run-boot
   cmake -S tests/arch/tc32/softfloat -B build -GNinja -DBOARD=tlsr8278_generic \
     -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm -DLLVM_TOOLCHAIN_PATH=/path/to/llvm -DTC32_THUMB=ON
   cmake --build build
   cmake --build build -t run

The ``run`` target boots ``zephyr/zephyr.tc32.bin`` in the emulator and prints the console.
The console is the emulator's (``CONFIG_TC32EMU_CONSOLE``); the board has no UART.

References
**********

.. target-notes::

.. _TC32-devtools: https://github.com/goyamamoto/TC32-devtools
