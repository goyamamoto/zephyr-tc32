<!-- SPDX-License-Identifier: Apache-2.0 -->

# zephyr-tc32: known gaps

The port's places where the code or its documents say that something is not done, done in part, assumed, or left out, sorted as the keyboard firmware's register sorts them (zmk-tc32 `tc32/docs/known-gaps.md`, which also says how a gap's test is marked as an expected failure):

- **(a)** a gap that can make a product built on the port misbehave: a specification's requirement, safety, lost data;
- **(b)** a deliberate scope limit, with its reason (most are in `TC32.md`, "Not provided");
- **(c)** wording only, counted.

"Keyboards" says whether the keyboard firmware built on this port (zmk-tc32 with tc32-keyboards) uses the code: those images carry their own USB device driver (zmk-tc32 `usb_dc_b87.c`) and their own flash driver (`telink,tlsr-spi-flash`).

## (a)

| id | where | what goes wrong | keyboards | reference | test | status |
|---|---|---|---|---|---|---|
| RNG-ENTROPY-CREDIT | `subsys/tc32_rng/Kconfig` (`TC32_RNG_JITTER_SAMPLES_PER_BIT`, `TC32_RNG_ADC_SAMPLES_PER_BIT`): "an assumption until samples recorded on the chip are assessed" | The 1/16 bit of entropy credited per 32 kHz jitter sample and per ADC sample is not measured. If the chip gives less, the generator reports itself ready too early, and BLE private keys, LTKs and IRKs are weaker than their size. | yes | NIST SP 800-90B 6.3 | none yet: an SP 800-90B non-IID assessment of samples recorded on the chip (`TC32_RNG_JITTER_DUMP`, the ADC dump), kept as fixtures, failing while no assessment exists or its min-entropy is below the credit | open, hardware |
| XTAL-WAIT | `soc/telink/tlsr/tlsr827x/Kconfig` `SOC_TLSR8278_ASM_SETUP` (default y): "The wait for analog 0x88 bit 7 has no bound"; `SOC_TLSR8278_EARLY_WATCHDOG_MS` defaults to 0 | A crystal that never reports settled (cold, a low battery, a fault) hangs the boot until a power cycle, as the early watchdog is off by default. The C path (`soc.c`) bounds the wait. | no: the keyboards set `SOC_TLSR8278_EARLY_WATCHDOG_MS` to 4000 | DS-TLSR8278 5.1.6 | none yet: a tc32emu option that never sets 0x88 bit 7, and a boot test with the defaults that expects a reset; or a build check that `SOC_TLSR8278_ASM_SETUP` needs the early watchdog | open |
| UDC-CLEAR-HALT | `drivers/usb/udc/udc_b87.c`: the controller answers SET_INTERFACE and SET/CLEAR_FEATURE itself; the driver keeps the data PID (`s->toggle`) | After a host's CLEAR_FEATURE(ENDPOINT_HALT) the PID is not reset to DATA0, so the host can drop the next IN packet as a repeat; an endpoint the stack halted stays halted until a bus reset. Whether the controller resets the PID itself is not known. | no | USB 2.0 9.4.5, 8.6.4 | none yet: a `tests/drivers/udc` case with `udc_b87_sim.c` answering a CLEAR_FEATURE as the controller does, expecting DATA0 next; first the controller's behaviour on hardware | open |
| UDC-ANALOG-WAIT | `drivers/usb/udc/udc_b87.c`: the waits for the analog port (`ANALOG_PORT_BUSY`) have no bound | A port that stays busy hangs the caller; `soc.c` bounds the same wait. | no | none | none yet: the `udc_b87_sim.c` model holding the busy bit, expecting an error return | open |
| FLASH-ERASE-IRQ-OFF | `drivers/flash/soc_flash_b87.c`, `soc/telink/tlsr/tlsr827x/tlsr8278_mspi.c`: "a sector erase keeps them locked for its whole time" | Interrupts are off for each sector erase (up to hundreds of ms): a radio link layer or a USB poll of the application misses its time; a BLE link can end by supervision timeout. | no | Bluetooth Core 5.4 Vol 6 Part B 4.5.2 | none yet: `tests/drivers/flash/tlsr8278` with the erase time at the part's maximum, measuring the longest window with interrupts off | open |

## (b)

- What `TC32.md` lists under "Not provided": the legacy USB device stack, an entropy driver, CPU fault exceptions, direct interrupts and interrupt priorities or nesting, Zephyr's PM subsystem, UART, I2C, PWM, counter, QDEC, the audio path, the AES-CCM hardware, a BT controller, IEEE 802.15.4, and hardware coverage beyond what the keyboard firmware uses.
- The link layer is the application's; the port has no RF interrupt handler (`TC32.md`).
- The public key engine's operand slots come from Telink's B91 driver and are confirmed with known answers; its time per operation and its behaviour for a scalar of 0 are not in the datasheet (0 is refused before the engine runs) (`tlsr_pke.c`).
- The UDC has no isochronous endpoints and no SOF events (the controller has none), so `tc32_rng`'s USB SOF source gets nothing from it (`drivers/usb/udc/Kconfig.b87`).
- SPI is half duplex only (`spi_b87.c`): the master clocks an octet out or in, not both.
- ADC: gain 1, the internal reference, no oversampling, no VBAT through the API (`adc_b87.c`).
- Watchdog: one timeout, no callback, no RESET_NONE, no pause for a debugger; Timer2 stops in suspend (`wdt_b87.c`).
- GPIO: no open drain, no level interrupts (`gpio_b87.c`); the boards poll the matrix.
- hwinfo: a software reset or a RESETB reset reads as RESET_POR (the chip documents no flag for them).
- No deep retention; `sys_clock_set_timeout()` is the caller's after a suspend; `TIMEOUT_64BIT` off by default (the cost per tick).
- The compiler options: no jump tables and no load/store merging (how the TC32 reads PC and treats `ldm` with the base register in the list is not documented); division by zero in software.
- The default idle loop spins instead of stalling the core until the wake sources are established on hardware (`cpu_idle.c`).
- No software-triggered interrupts: `irq_offload` borrows TMR0, and the shared-IRQ test skips.
- `.ramfunc` must be empty (nothing copies it); flash addresses are physical whatever slot the boot ROM maps to 0.
- `tc32_rng_fill()` works before the generator is ready; the TRNG and events get no credit by default (`tc32_rng.h`, `subsys/tc32_rng/Kconfig`).
- p256-m's own key generation and signing are not used; the software multiplication runs to its end inside the start call.
- The emulator console and log backend are for emulator images only.
- About 50 register writes and reads follow vendor code where DS-TLSR8278 is silent; each is noted where it is made.

## Counts

| class | count |
|---|---|
| (a) | 5 |
| (b) | 33 |
| (c) | 22, with tc32-keyboards' |
