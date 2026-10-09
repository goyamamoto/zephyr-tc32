/*
 * chip_init(0x43, 0) and clock_init(0x20) for the TLSR8278, the
 * routines they call, and the variables they use (at the end of this file).
 *
 * Registers. "Documented" means DS-TLSR8278 v1.0.4 describes the register
 * or bit; for everything else the comments give only what the code writes,
 * reads or waits for, and no meaning:
 * - documented: 0x800060-0x800066 (resets, clock enables, system clock
 *   select), 0x80006f (power-down control), 0x800070 bit 0 (with 0x800066
 *   bit 7, the fast clock's source), 0x800643 (bit 0 interrupt enable, bit 1
 *   reserved), 0x800596 bit 3 (PC3 acts as GPIO), 0x8005b5 bit 3 (GPIO
 *   interrupt enable), 0x800740 and 0x800744 (system timer and compare),
 *   0x80074a bits 0, 1 and 3 (32 kHz timer read/write mode, system timer
 *   enable, 32 kHz calibration enable; bit 2 reserved), 0x80074b bit 6
 *   (32 kHz timer busy; bits 5:0 reserved), 0x80074c (32 kHz timer write
 *   value), 0x800750 (32 kHz timer read value), 0x800780, 0x800782, 0x800798
 *   (PWM enable, PWM clock divider, PWM1 high and cycle time), analog 0x06
 *   bits 1 and 2 (low-power comparator and temperature sensor power-down),
 *   analog 0x0b bit 7 (USB D+ pull-up), analog 0x27-0x2a (pad wakeup
 *   enables), analog 0x3b (kept across all but a power-on reset), analog
 *   0x44 (wakeup status: bit 1 timer, bit 3 pad), the pin PC3 (xtl_32k_in,
 *   also PWM1);
 * - not documented: the analog register port (0x8000b8-0x8000ba), the MSPI
 *   registers (0x80000c, 0x80000d), the divider (0x800660-0x80066b), the
 *   word at 0x8006fc, 0x800074 and 0x80004c, 0x800608 (not used here),
 *   0x80063e, 0x800748, 0x80074b bits 5 and 3, 0x8005a1, 0x8005b5 bit 2, the
 *   DMA registers 0x800c20, 0x800c21 and 0x800c40-0x800c48, SRAM
 *   0x840014-0x840017 as the boot ROM uses them, and the analog registers
 *   0x00, 0x01, 0x02, 0x05, 0x0a, 0x0c, 0x2c, 0x2d, 0x30-0x34, 0x40-0x43,
 *   0x7f, 0x81, 0x82, 0x88, 0x8c, 0xc6-0xcb, 0xcf, 0xe4, and the other bits
 *   of 0x06 and 0x0b.
 * The code writes to or tests these as the comments say; the values are
 * fixed.
 *
 * Instructions: tnand rn, rm sets the flags from rn & rm and writes no
 * register. tadd with a high register (r8-r15) sets no flags.
 *
 * Waits: the count loops (a counter on the stack, counted up to a limit)
 * take a fixed number of instructions per count, given at each loop; their
 * time in microseconds is not measured. tmov r8, r8 (nop) changes nothing
 * and only takes time.
 *
 * soc_early_init_hook() (soc.c) calls chip_init and clock_init
 * through the offsets in tlsr8278_setup.h, so every function keeps its place.
 * Not called from them: udiv/div, mul_add, suspend_enter, suspend_stall,
 * set_32k_tick, call_via_r3_b and scale_count. Run only after analog
 * 0x7f bit 0 reads 0 (a path this image does not take): get_32k_tick and
 * call_via_r3. Run only when tlsr8278_setup_vars + 0x19 is not 0 (it is 0):
 * pwm_pc3_until_moving, pwm_pc3_10ms and read_analog40.
 *
 * A halfword 0x46c0 written as data (.short) pads a literal pool or the next
 * function to a word boundary and is never executed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

	.section .ram_code.tlsr8278_setup, "ax", %progbits
	.balign 4
	.global tlsr8278_setup_blob
	@ A plain label, neither function nor object: the C side adds the offsets
	@ and bit 0 itself, so its value must be the even start address (a Thumb
	@ function symbol would carry bit 0), and thumb2tc32.py leaves a sized
	@ object as data, which this is not.
tlsr8278_setup_blob:

/*
 * 0x000 udiv(a in r0, b in r1): r2 = 0, then div. Not called.
 */
	.global tlsr8278_setup_udiv
	.type tlsr8278_setup_udiv, %function
tlsr8278_setup_udiv:
	tmov	r2, #0		@ mode 0: unsigned quotient
	tj	tlsr8278_setup_div
	.size tlsr8278_setup_udiv, . - tlsr8278_setup_udiv

/*
 * 0x004 div(a in r0, b in r1, mode in r2) -> r0: with the CPSR's I bit set,
 * a to 0x800664, b to 0x800668, the mode byte to 0x800660, a wait until
 * 0x800660 reads 0, then r0 from 0x800664 when the mode is 0 or 1, else from
 * 0x800668; the CPSR back. None of these registers is documented. Not
 * called.
 */
	.global tlsr8278_setup_div
	.type tlsr8278_setup_div, %function
tlsr8278_setup_div:
	tmrcs	r3		@ r3 = CPSR
	tpush	{r3, r4}	@ keep the old CPSR (and r4, used below)
	tmov	r4, #0x80	@ the CPSR's I bit
	tor	r3, r4
	tmcsr	r3		@ CPSR = r3: IRQs masked
	tloadr	r3, .Ldiv_a
	tstorer	r0, [r3]	@ a to 0x800664
	tadd	r3, #4
	tstorer	r1, [r3]	@ b to 0x800668
	tsub	r3, #8
	tstorerb	r2, [r3]	@ the mode byte to 0x800660
1:	tloadrb	r0, [r3]
	tcmp	r0, #0
	tjne	1b		@ until 0x800660 reads 0
	tcmp	r2, #1
	tjls	2f		@ mode 0 or 1: 0x800664
	tadd	r3, #8
	tloadr	r0, [r3]	@ otherwise 0x800668
	tj	3f
2:	tadd	r3, #4
	tloadr	r0, [r3]	@ 0x800664
3:	tpop	{r3, r4}
	tmcsr	r3		@ the old CPSR back
	tjex	lr
.Ldiv_a:	.word	0x00800664
	.word	0x00800660		@ (not loaded)
	.word	0x00800668		@ (not loaded)
	.short	0x46c0			@ pad (never executed)
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_div, . - tlsr8278_setup_div

/*
 * 0x044 mul_add(y in r0, a in r1, n in r2, b in r3): y[0..n-1] += a[0..n-1]
 * * b over 32-bit words with the carry running through, and y[n] = the final
 * carry word. After each tmul the code reads the word at 0x8006fc (not
 * documented) and adds it into the carry as the product's high half. Words
 * go eight at a time, then one at a time. The carry word c is kept in r2:
 * after y + a*b(low) + c, that word and both additions' carries make the
 * next c; tadd r2, r10 (+1, a high register) leaves the flags of the second
 * addition for taddc. Not called.
 */
	.global tlsr8278_setup_mul_add
	.type tlsr8278_setup_mul_add, %function
tlsr8278_setup_mul_add:
	tpush	{r4, r5, r6, r7}
	tmov	r4, r8
	tmov	r5, r9
	tmov	r6, r10
	tmov	r7, r11
	tpush	{r4, r5, r6, r7}	@ the high registers are callee-saved too
	tmov	r8, r2		@ r8 = words left
	tmov	r2, #1
	tmov	r10, r2		@ r10 = 1
	tsub	r2, #9
	tmov	r9, r2		@ r9 = -8, the step of the eight-word loop
	tmov	r2, #0
	tmov	r2, #0		@ carry word c = 0 (set twice)
	tloadr	r6, .Lma_hi	@ r6 = 0x8006fc
	tmov	r11, r2		@ r11 = 0, for the loop compare
	tj	.Lma_loop8
.Lma_hi:	.word	0x008006fc
.Lma_8:	@ eight words: four times two, a[i] in r4 and a[i+1] in r5
	.rept	3
	tmul	r4, r3		@ low half of a[i] * b
	tloadr	r7, [r0]	@ y[i]
	tadd	r4, r4, r2	@ + c
	tsubc	r2, r2		@ r2 = carry - 1
	tadd	r4, r4, r7	@ + y[i]
	tloadr	r7, [r6]	@ the word at 0x8006fc
	tadd	r2, r10	@ r2 = the first addition's carry (flags kept)
	tstorem	r0!, {r4}	@ y[i] done
	taddc	r2, r7		@ c = carry + high + the second carry
	tmul	r5, r3		@ the same for a[i+1]
	tloadr	r7, [r0]
	tadd	r5, r5, r2
	tsubc	r2, r2
	tadd	r5, r5, r7
	tloadr	r7, [r6]
	tadd	r2, r10
	tstorem	r0!, {r5}
	tloadm	r1!, {r4, r5}	@ the next two words of a
	taddc	r2, r7
	.endr
	tmul	r4, r3		@ the fourth pair: as above, and the load
	tloadr	r7, [r0]	@ of the next two words comes at the loop
	tadd	r4, r4, r2	@ head
	tsubc	r2, r2
	tadd	r4, r4, r7
	tloadr	r7, [r6]
	tadd	r2, r10
	tstorem	r0!, {r4}
	taddc	r2, r7
	tmul	r5, r3
	tloadr	r7, [r0]
	tadd	r5, r5, r2
	tsubc	r2, r2
	tadd	r5, r5, r7
	tloadr	r7, [r6]
	tadd	r2, r10
	tstorem	r0!, {r5}
	taddc	r2, r7
.Lma_loop8:
	tloadm	r1!, {r4, r5}	@ a[i], a[i+1]
	tadd	r8, r9	@ eight words fewer left
	tcmp	r8, r11
	tjge	.Lma_8		@ while eight or more were left
	tmov	r5, r8
	tadd	r5, #8		@ r5 = the 0-7 words still to do
	tsub	r1, #8		@ back over the two words loaded ahead
	tj	.Lma_loop1
.Lma_1:	@ one word: as one half of the pair above
	tmul	r4, r3
	tloadr	r7, [r0]
	tadd	r4, r4, r2
	tsubc	r2, r2
	tadd	r4, r4, r7
	tloadr	r7, [r6]
	tadd	r2, r10
	tstorem	r0!, {r4}
	taddc	r2, r7
.Lma_loop1:
	tloadm	r1!, {r4}
	tsub	r5, #1
	tcmp	r5, #0
	tjge	.Lma_1
	tstorem	r0!, {r2}	@ y[n] = the last carry word
	tpop	{r4, r5, r6, r7}
	tmov	r8, r4
	tmov	r9, r5
	tmov	r10, r6
	tmov	r11, r7
	tpop	{r4, r5, r6, r7}
	tjex	lr
	tmov	r8, r8		@ never executed (after the return)
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_mul_add, . - tlsr8278_setup_mul_add

/*
 * The analog register port (0x8000b8-0x8000ba, not documented). A read:
 * the address to 0x8000b8, 0x40 to 0x8000ba, a wait until 0x8000ba bit 0
 * reads 0, the byte from 0x8000b9, 0 to 0x8000ba. A write: the address to
 * 0x8000b8, the byte to 0x8000b9, 0x60 to 0x8000ba, the same wait, 0 to
 * 0x8000ba. The whole byte 0x800643 (bit 0 the interrupt enable) is saved,
 * set to 0 for the access and written back, so no interrupt handler runs
 * between the steps.
 */

/* 0x13c analog_read(addr in r0) -> the byte in r0. */
	.global tlsr8278_setup_analog_read
	.type tlsr8278_setup_analog_read, %function
tlsr8278_setup_analog_read:
	tpush	{r4, lr}
	tshftl	r0, r0, #24
	tshftr	r0, r0, #24	@ the address is a byte
	tloadr	r3, .Lar_irq
	tloadrb	r4, [r3]	@ keep 0x800643
	tmov	r2, #0
	tstorerb	r2, [r3]	@ 0x800643 = 0: interrupts off
	tloadr	r3, .Lar_addr
	tstorerb	r0, [r3]	@ 0x8000b8 = address
	tloadr	r1, .Lar_ctrl
	tmov	r3, #0x40
	tstorerb	r3, [r1]	@ 0x8000ba = 0x40
	tmov	r2, #1
1:	tloadrb	r3, [r1]
	tand	r3, r2
	tjne	1b		@ until 0x8000ba bit 0 reads 0 (r3 = 0 after)
	tloadr	r2, .Lar_data
	tloadrb	r0, [r2]	@ the byte from 0x8000b9
	tadd	r2, #1
	tstorerb	r3, [r2]	@ 0x8000ba = 0
	tloadr	r3, .Lar_irq
	tstorerb	r4, [r3]	@ 0x800643 as it was
	tpop	{r4, pc}
	.short	0x46c0			@ pad (never executed)
.Lar_irq:	.word	0x00800643
.Lar_addr:	.word	0x008000b8
.Lar_ctrl:	.word	0x008000ba
.Lar_data:	.word	0x008000b9
	.size tlsr8278_setup_analog_read, . - tlsr8278_setup_analog_read

/* 0x17c analog_write(addr in r0, value in r1). */
	.global tlsr8278_setup_analog_write
	.type tlsr8278_setup_analog_write, %function
tlsr8278_setup_analog_write:
	tpush	{r4, lr}
	tshftl	r0, r0, #24
	tshftr	r0, r0, #24	@ address and value are bytes
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tloadr	r3, .Law_irq
	tloadrb	r4, [r3]	@ keep 0x800643
	tmov	r2, #0
	tstorerb	r2, [r3]	@ 0x800643 = 0: interrupts off
	tloadr	r3, .Law_addr
	tstorerb	r0, [r3]	@ 0x8000b8 = address
	tadd	r3, #1
	tstorerb	r1, [r3]	@ 0x8000b9 = value
	tloadr	r1, .Law_ctrl
	tmov	r3, #0x60
	tstorerb	r3, [r1]	@ 0x8000ba = 0x60
	tmov	r2, #1
1:	tloadrb	r3, [r1]
	tand	r3, r2
	tjne	1b		@ until 0x8000ba bit 0 reads 0 (r3 = 0 after)
	tloadr	r2, .Law_ctrl
	tstorerb	r3, [r2]	@ 0x8000ba = 0
	tloadr	r3, .Law_irq
	tstorerb	r4, [r3]	@ 0x800643 as it was
	tpop	{r4, pc}
	.short	0x46c0			@ pad (never executed)
.Law_irq:	.word	0x00800643
.Law_addr:	.word	0x008000b8
.Law_ctrl:	.word	0x008000ba
	.size tlsr8278_setup_analog_write, . - tlsr8278_setup_analog_write

/*
 * 0x1bc clock_init(sel in r0): the system clock select register 0x800066
 * takes sel, and sel is kept in the variable at tlsr8278_setup_vars + 4. soc.c
 * passes 0x20: bits 6:5 = 01 select the fast clock (FHS), which with bit 7
 * = 0 and 0x800070 bit 0 at its reset value 0 is the 48 MHz clock doubled
 * from the 24 MHz crystal. Then seq_c7 runs, unless the byte at
 * tlsr8278_setup_wake_flags + 0 is not 0 (set only on the path taken after
 * analog 0x7f bit 0 reads 0).
 */
	.global tlsr8278_setup_clock_init
	.type tlsr8278_setup_clock_init, %function
tlsr8278_setup_clock_init:
	tpush	{lr}
	tshftl	r0, r0, #24
	tshftr	r0, r0, #24
	tloadr	r3, .Lci_sel
	tstorerb	r0, [r3]	@ system clock select
	tloadr	r3, .Lci_var
	tstorerb	r0, [r3]	@ tlsr8278_setup_vars + 4
	tloadr	r3, .Lci_pm
	tloadrb	r3, [r3]	@ tlsr8278_setup_wake_flags + 0
	tcmp	r3, #0
	tjne	1f
	tjl	tlsr8278_setup_seq_c7
1:	tpop	{pc}
.Lci_sel:	.word	0x00800066	@ system clock select
.Lci_var:	.word	tlsr8278_setup_vars + 0x4
.Lci_pm:	.word	tlsr8278_setup_wake_flags + 0x0
	.size tlsr8278_setup_clock_init, . - tlsr8278_setup_clock_init

/*
 * The flash is driven through 0x80000c and 0x80000d (not documented). The
 * code writes a command, address or 0 byte to 0x80000c and then waits until
 * 0x80000d bit 4 reads 0; it writes 1 to 0x80000d before a command (for 1
 * us) and after the last byte, and 0 to it before the command byte; before
 * reading data it writes 0x0a to 0x80000d, and then reads each data byte
 * from 0x80000c. The flash commands are the flash chip's: 0x9f (JEDEC ID),
 * 0xb9 (deep power-down), 0xab (release from deep power-down).
 */

/*
 * 0x1e4 flash_send_addr(addr in r0): the 24-bit address to 0x80000c, high
 * byte first, each byte followed by the wait for 0x80000d bit 4 = 0.
 */
	.global tlsr8278_setup_flash_send_addr
	.type tlsr8278_setup_flash_send_addr, %function
tlsr8278_setup_flash_send_addr:
	tshftl	r2, r0, #8
	tshftr	r2, r2, #24	@ bits 23:16
	tloadr	r3, .Lsa_data
	tstorerb	r2, [r3]
	tloadr	r1, .Lsa_ctrl
	tmov	r2, #0x10	@ 0x80000d bit 4
1:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjne	1b
	tshftl	r2, r0, #16
	tshftr	r2, r2, #24	@ bits 15:8
	tloadr	r3, .Lsa_data
	tstorerb	r2, [r3]
	tloadr	r1, .Lsa_ctrl
	tmov	r2, #0x10
1:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjne	1b
	tshftl	r0, r0, #24
	tshftr	r0, r0, #24	@ bits 7:0
	tloadr	r3, .Lsa_data
	tstorerb	r0, [r3]
	tloadr	r1, .Lsa_ctrl
	tmov	r2, #0x10
1:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjne	1b
	tjex	lr
.Lsa_data:	.word	0x0080000c
.Lsa_ctrl:	.word	0x0080000d
	.size tlsr8278_setup_flash_send_addr, . - tlsr8278_setup_flash_send_addr

/*
 * 0x224 flash_send_cmd(cmd in r0): 0x80000d = 1, sleep_us(1), 0x80000d = 0,
 * the command byte to 0x80000c, the wait for 0x80000d bit 4 = 0.
 */
	.global tlsr8278_setup_flash_send_cmd
	.type tlsr8278_setup_flash_send_cmd, %function
tlsr8278_setup_flash_send_cmd:
	tpush	{r4, r5, lr}
	tmov	r5, r0		@ keep the command
	tloadr	r4, .Lsc_ctrl
	tmov	r3, #1
	tstorerb	r3, [r4]	@ 0x80000d = 1
	tmov	r0, #1
	tjl	tlsr8278_setup_sleep_us	@ for 1 us
	tmov	r3, #0
	tstorerb	r3, [r4]	@ 0x80000d = 0
	tloadr	r3, .Lsc_data
	tstorerb	r5, [r3]	@ the command byte to 0x80000c
	tmov	r2, #0x10
1:	tloadrb	r3, [r4]
	tnand	r2, r3
	tjne	1b		@ until 0x80000d bit 4 reads 0
	tpop	{r4, r5, pc}
	.short	0x46c0			@ pad (never executed)
.Lsc_ctrl:	.word	0x0080000d
.Lsc_data:	.word	0x0080000c
	.size tlsr8278_setup_flash_send_cmd, . - tlsr8278_setup_flash_send_cmd

/*
 * 0x250 flash_read_cmd(cmd in r0, addr in r1, with_addr in r2, zeros in r3,
 * buf and len on the stack): with 0x800643 = 0 throughout, flash_send_cmd,
 * then flash_send_addr when with_addr is not 0, then zeros 0 bytes to
 * 0x80000c, one more 0, the wait for 0x80000d bit 4 = 0, 0x80000d = 0x0a,
 * the same wait, then len bytes read from 0x80000c into buf (each read
 * followed by the wait), 0x80000d = 1. The code runs from RAM (.ram_code).
 */
	.global tlsr8278_setup_flash_read_cmd
	.type tlsr8278_setup_flash_read_cmd, %function
tlsr8278_setup_flash_read_cmd:
	tpush	{r4, r5, r6, r7, lr}
	tmov	r7, r10
	tmov	r6, r8
	tpush	{r6, r7}
	tmov	r10, r1		@ r10 = addr
	tloadr	r6, [sp, #0x1c]	@ r6 = buf (fifth argument)
	tloadr	r4, [sp, #0x20]	@ r4 = len (sixth argument)
	tshftl	r0, r0, #24
	tshftr	r0, r0, #24	@ command byte
	tshftl	r2, r2, #24
	tshftr	r7, r2, #24	@ r7 = with_addr
	tshftl	r5, r3, #24
	tshftr	r5, r5, #24	@ r5 = zeros
	tloadr	r3, .Lrd_irq
	tloadrb	r2, [r3]
	tmov	r8, r2		@ keep 0x800643
	tmov	r2, #0
	tstorerb	r2, [r3]	@ 0x800643 = 0: interrupts off
	tjl	tlsr8278_setup_flash_send_cmd
	tcmp	r7, #0
	tjne	.Lrd_addr	@ send the address first (and come back)
.Lrd_dummy:
	tcmp	r5, #0
	tjeq	2f
	tmov	r0, #0		@ zeros: count r0 up to r5
	tloadr	r2, .Lrd_data
	tmov	r12, r2
	tmov	r7, #0
	tloadr	r1, .Lrd_ctrl
	tmov	r2, #0x10
1:	tmov	r3, r12
	tstorerb	r7, [r3]	@ 0 to 0x80000c
3:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjne	3b		@ until 0x80000d bit 4 reads 0
	tadd	r0, #1
	tcmp	r0, r5
	tjlt	1b
2:	tmov	r2, #0
	tloadr	r3, .Lrd_data
	tstorerb	r2, [r3]	@ one more 0 to 0x80000c
	tloadr	r0, .Lrd_ctrl
	tmov	r1, #0x10
1:	tloadr	r2, .Lrd_ctrl
	tloadrb	r3, [r0]
	tnand	r1, r3
	tjne	1b		@ until 0x80000d bit 4 reads 0
	tmov	r3, #0x0a
	tstorerb	r3, [r2]	@ 0x80000d = 0x0a
	tmov	r1, #0x10
1:	tloadrb	r3, [r2]
	tnand	r1, r3
	tjne	1b		@ until 0x80000d bit 4 reads 0
	tcmp	r4, #0
	tjeq	2f
	tmov	r0, #0		@ the data: count r0 up to len
	tloadr	r5, .Lrd_data
	tloadr	r1, .Lrd_ctrl
	tmov	r2, #0x10
1:	tloadrb	r3, [r5]	@ a byte from 0x80000c
	tstorerb	r3, [r6, r0]
3:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjne	3b		@ until 0x80000d bit 4 reads 0
	tadd	r0, #1
	tcmp	r0, r4
	tjne	1b
2:	tmov	r2, #1
	tloadr	r3, .Lrd_ctrl
	tstorerb	r2, [r3]	@ 0x80000d = 1
	tloadr	r3, .Lrd_irq
	tmov	r2, r8
	tstorerb	r2, [r3]	@ 0x800643 as it was
	tpop	{r2, r3}
	tmov	r8, r2
	tmov	r10, r3
	tpop	{r4, r5, r6, r7, pc}
.Lrd_addr:
	tmov	r0, r10
	tjl	tlsr8278_setup_flash_send_addr
	tj	.Lrd_dummy
	.short	0x46c0			@ pad (never executed)
.Lrd_irq:	.word	0x00800643
.Lrd_data:	.word	0x0080000c
.Lrd_ctrl:	.word	0x0080000d
	.size tlsr8278_setup_flash_read_cmd, . - tlsr8278_setup_flash_read_cmd

/*
 * 0x300 sleep_us(us in r0): wait on the 32-bit system timer (0x800740,
 * 16 ticks per microsecond) until more than us * 16 ticks have passed. The
 * difference now - start is right across the counter's wrap.
 */
	.global tlsr8278_setup_sleep_us
	.type tlsr8278_setup_sleep_us, %function
tlsr8278_setup_sleep_us:
	tloadr	r2, .Lsl_tick
	tloadr	r1, [r2]	@ start
	tshftl	r0, r0, #4	@ us * 16
1:	tloadr	r3, [r2]
	tsub	r3, r3, r1	@ ticks since the start
	tcmp	r0, r3
	tjhs	1b
	tjex	lr
.Lsl_tick:	.word	0x00800740	@ system timer
	.size tlsr8278_setup_sleep_us, . - tlsr8278_setup_sleep_us

/*
 * 0x314 suspend_stall: two nops, 0x81 to 0x80006f (bit 0 suspend enable,
 * bit 7 stall the CPU: with bit 0 set the chip goes into suspend), then,
 * once awake, a byte on the stack counted from 0 to 48 (8 instructions per
 * count). Called by suspend_enter only.
 */
	.global tlsr8278_setup_suspend_stall
	.type tlsr8278_setup_suspend_stall, %function
tlsr8278_setup_suspend_stall:
	tloadr	r3, .Lss_pwdn
	tmov	r2, #0x81
	tsub	sp, #4
	nop
	nop
	tstorerb	r2, [r3]	@ 0x80006f = 0x81: suspend
	tmov	r3, sp
	tadd	r3, #3		@ the counter byte on the stack
	tmov	r2, #0
	tj	2f
1:	tloadrb	r2, [r3]
	tadd	r2, #1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
2:	tstorerb	r2, [r3]
	tloadrb	r2, [r3]
	tcmp	r2, #0x2f
	tjls	1b		@ up to 48
	tadd	sp, #4
	tjex	lr
.Lss_pwdn:	.word	0x0080006f	@ power-down control
	.size tlsr8278_setup_suspend_stall, . - tlsr8278_setup_suspend_stall

/*
 * 0x340 analog88_check: analog 0x81 bit 6 set; then up to three times: a byte
 * on the stack counted from 0 to 30 (9 instructions per count, one of them
 * a nop), and analog 0x88 bit 5 tested. When it reads 1, or on the third
 * time, analog 0x81 bit 6 is cleared and the function returns. After the
 * first 0 analog 0x01 = 0x4e, after the second analog 0x01 = 0x4b
 * (chip_init wrote 0x4d there).
 */
	.global tlsr8278_setup_analog88_check
	.type tlsr8278_setup_analog88_check, %function
tlsr8278_setup_analog88_check:
	tpush	{r4, r5, r6, r7, lr}
	tmov	r7, r10
	tmov	r6, r8
	tpush	{r6, r7}
	tsub	sp, #4
	tmov	r0, #0x81
	tjl	tlsr8278_setup_analog_read
	tmov	r4, r0		@ r4 = analog 0x81
	tmov	r1, #0x40
	tor	r1, r0
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #0x81
	tjl	tlsr8278_setup_analog_write	@ analog 0x81 bit 6 set
	tmov	r3, #0xbf
	tand	r3, r4
	tmov	r10, r3		@ r10 = analog 0x81 with bit 6 clear
	tmov	r6, #1		@ r6 = the test's number (1, 2, 3)
	tmov	r5, #0		@ r5 = tests that read 0 so far
	tmov	r4, sp
	tadd	r4, #3		@ a counter byte on the stack
	tmov	r7, #0
	tmov	r3, #0x20
	tmov	r8, r3		@ r8 = bit 5
.Lxc_check:
	tstorerb	r7, [r4]	@ count 0 to 30
	tloadrb	r3, [r4]
	tcmp	r3, #0x1d
	tjhi	2f
1:	nop
	tloadrb	r3, [r4]
	tadd	r3, #1
	tshftl	r3, r3, #24
	tshftr	r3, r3, #24
	tstorerb	r3, [r4]
	tloadrb	r3, [r4]
	tcmp	r3, #0x1d
	tjls	1b
2:	tmov	r0, #0x88
	tjl	tlsr8278_setup_analog_read
	tmov	r3, r8
	tnand	r3, r0
	tjne	.Lxc_ok		@ analog 0x88 bit 5 = 1: done
	tshftl	r3, r5, #24
	tshftr	r3, r3, #24
	tcmp	r3, #0
	tjeq	.Lxc_first
	tcmp	r3, #1
	tjeq	.Lxc_second
	tmov	r0, #0x81	@ the third 0: stop
	tmov	r1, r10
	tjl	tlsr8278_setup_analog_write	@ analog 0x81 bit 6 clear
	tcmp	r6, #2		@ (r6 is 3 here, so this never loops)
	tjls	.Lxc_next
.Lxc_out:
	tadd	sp, #4
	tpop	{r2, r3}
	tmov	r8, r2
	tmov	r10, r3
	tpop	{r4, r5, r6, r7, pc}
.Lxc_first:
	tmov	r0, #1
	tmov	r1, #0x4e
	tjl	tlsr8278_setup_analog_write	@ analog 0x01 = 0x4e
.Lxc_next:
	tadd	r5, #1
	tadd	r6, #1
	tshftl	r6, r6, #24
	tshftr	r6, r6, #24
	tj	.Lxc_check
.Lxc_second:
	tmov	r0, #1
	tmov	r1, #0x4b
	tjl	tlsr8278_setup_analog_write	@ analog 0x01 = 0x4b
	tadd	r5, #1
	tadd	r6, #1
	tshftl	r6, r6, #24
	tshftr	r6, r6, #24
	tj	.Lxc_check
.Lxc_ok:
	tmov	r0, #0x81
	tmov	r1, r10
	tjl	tlsr8278_setup_analog_write	@ analog 0x81 bit 6 clear
	tj	.Lxc_out
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_analog88_check, . - tlsr8278_setup_analog88_check

/*
 * 0x3ec suspend_enter: into suspend and back. Before: analog 0x34 = 0x87,
 * or 0x85 when the byte at tlsr8278_setup_vars + 0x14 is 0; analog 0xe4 =
 * 0x10; analog 0x82 = 0x85; 0x80000d = 0, flash command 0xb9 to 0x80000c,
 * a word on the stack counted to 2 (6 instructions per count; 0x80000d bit
 * 4 is not tested here), 0x80000d = 1; 0x8005a1 = 0; suspend_stall. After:
 * 0x8005a1 = 0x0f; 0x80000d = 0, command 0xab, the same count to 2,
 * 0x80000d = 1; analog 0x2c bit 5 and analog 0x05 bit 3 each set and
 * cleared again; analog 0x82 = 0x65, 0xe4 = 0, 0x34 = 0x80 (the values
 * chip_init writes); a count to 300 (7 instructions per count, one a nop);
 * three waits in a row until analog 0x88 bit 7 reads 1; analog88_check. Not
 * called.
 */
	.global tlsr8278_setup_suspend_enter
	.type tlsr8278_setup_suspend_enter, %function
tlsr8278_setup_suspend_enter:
	tpush	{r4, r5, r6, lr}
	tsub	sp, #4		@ a counter word on the stack
	tloadr	r3, .Lse_vars
	tloadrb	r3, [r3, #4]	@ tlsr8278_setup_vars + 0x14
	tmov	r1, #0x85
	tcmp	r3, #0
	tjeq	1f
	tmov	r1, #0x87
1:	tmov	r0, #0x34
	tjl	tlsr8278_setup_analog_write	@ analog 0x34 = 0x85 or 0x87
	tmov	r0, #0xe4
	tmov	r1, #0x10
	tjl	tlsr8278_setup_analog_write	@ analog 0xe4 = 0x10
	tmov	r0, #0x82
	tmov	r1, #0x85
	tjl	tlsr8278_setup_analog_write	@ analog 0x82 = 0x85
	tmov	r3, #0
	tloadr	r2, .Lse_ctrl
	tstorerb	r3, [r2]	@ 0x80000d = 0
	tmov	r1, #0xb9
	tsub	r2, #1
	tstorerb	r1, [r2]	@ 0xb9 to 0x80000c
	tstorer	r3, [sp]	@ count 0 to 2
	tloadr	r3, [sp]
	tcmp	r3, #1
	tjhi	2f
1:	tloadr	r3, [sp]
	tadd	r3, #1
	tstorer	r3, [sp]
	tloadr	r3, [sp]
	tcmp	r3, #1
	tjls	1b
2:	tloadr	r5, .Lse_ctrl
	tmov	r3, #1
	tstorerb	r3, [r5]	@ 0x80000d = 1
	tloadr	r6, .Lse_pe_ie
	tmov	r4, #0
	tstorerb	r4, [r6]	@ 0x8005a1 = 0
	tjl	tlsr8278_setup_suspend_stall
	tmov	r3, #0xf
	tstorerb	r3, [r6]	@ 0x8005a1 = 0x0f
	tstorerb	r4, [r5]	@ 0x80000d = 0
	tmov	r2, #0xab
	tloadr	r3, .Lse_data
	tstorerb	r2, [r3]	@ 0xab to 0x80000c
	tstorer	r4, [sp]	@ count 0 to 2
	tloadr	r3, [sp]
	tcmp	r3, #1
	tjhi	2f
1:	tloadr	r3, [sp]
	tadd	r3, #1
	tstorer	r3, [sp]
	tloadr	r3, [sp]
	tcmp	r3, #1
	tjls	1b
2:	tmov	r2, #1
	tloadr	r3, .Lse_ctrl
	tstorerb	r2, [r3]	@ 0x80000d = 1
	tmov	r0, #0x2c
	tjl	tlsr8278_setup_analog_read
	tmov	r4, r0
	tmov	r1, #0x20
	tor	r1, r0
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #0x2c
	tjl	tlsr8278_setup_analog_write	@ analog 0x2c bit 5 set
	tmov	r1, #0xdf
	tand	r1, r4
	tmov	r0, #0x2c
	tjl	tlsr8278_setup_analog_write	@ and clear
	tmov	r0, #5
	tjl	tlsr8278_setup_analog_read
	tmov	r4, r0
	tmov	r1, #8
	tor	r1, r0
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #5
	tjl	tlsr8278_setup_analog_write	@ analog 0x05 bit 3 set
	tmov	r1, #0xf7
	tand	r1, r4
	tmov	r0, #5
	tjl	tlsr8278_setup_analog_write	@ and clear
	tmov	r0, #0x82
	tmov	r1, #0x65
	tjl	tlsr8278_setup_analog_write	@ analog 0x82 = 0x65
	tmov	r0, #0xe4
	tmov	r1, #0
	tjl	tlsr8278_setup_analog_write	@ analog 0xe4 = 0
	tmov	r0, #0x34
	tmov	r1, #0x80
	tjl	tlsr8278_setup_analog_write	@ analog 0x34 = 0x80
	tmov	r3, #0
	tstorer	r3, [sp]	@ count 0 to 300
	tloadr	r3, [sp]
	tloadr	r2, .Lse_300
	tcmp	r3, r2
	tjhi	2f
1:	nop
	tloadr	r3, [sp]
	tadd	r3, #1
	tstorer	r3, [sp]
	tloadr	r3, [sp]
	tcmp	r3, r2
	tjls	1b
2:	tmov	r0, #0x88
	tjl	tlsr8278_setup_analog_read
	tshftl	r3, r0, #24
	tjpl	2b		@ until analog 0x88 bit 7
1:	tmov	r0, #0x88
	tjl	tlsr8278_setup_analog_read
	tshftl	r3, r0, #24
	tjpl	1b		@ again
1:	tmov	r0, #0x88
	tjl	tlsr8278_setup_analog_read
	tshftl	r3, r0, #24
	tjpl	1b		@ and again
	tjl	tlsr8278_setup_analog88_check
	tadd	sp, #4
	tpop	{r4, r5, r6, pc}
.Lse_vars:	.word	tlsr8278_setup_vars + 0x10
.Lse_ctrl:	.word	0x0080000d
.Lse_pe_ie:	.word	0x008005a1
.Lse_data:	.word	0x0080000c
.Lse_300:	.word	0x0000012b	@ 299: the count runs to 300
	.size tlsr8278_setup_suspend_enter, . - tlsr8278_setup_suspend_enter

/*
 * 0x514 read_analog40 -> r0: a word from analog 0x43 (high byte) down to
 * 0x40, read one byte after another; the word is read again until two reads
 * in a row are equal or the second is one more (the second is returned), or
 * they differ in bit 0 only (the first is returned).
 */
	.global tlsr8278_setup_read_analog40
	.type tlsr8278_setup_read_analog40, %function
tlsr8278_setup_read_analog40:
	tpush	{r4, r5, r6, lr}
	tmov	r4, #0		@ r4 = reads so far
	tmov	r6, #0		@ r6 = the read before
	tj	.Lrc_read
.Lrc_differ:
	tmov	r3, r5
	txor	r3, r6
	tcmp	r3, #1
	tjeq	.Lrc_earlier	@ only bit 0 differs
.Lrc_again:
	tadd	r4, #1
	tmov	r6, r5
.Lrc_read:
	tmov	r0, #0x43
	tjl	tlsr8278_setup_analog_read
	tshftl	r5, r0, #8
	tmov	r0, #0x42
	tjl	tlsr8278_setup_analog_read
	tadd	r5, r5, r0
	tshftl	r5, r5, #8
	tmov	r0, #0x41
	tjl	tlsr8278_setup_analog_read
	tadd	r5, r5, r0
	tshftl	r5, r5, #8
	tmov	r0, #0x40
	tjl	tlsr8278_setup_analog_read
	tadd	r5, r5, r0	@ r5 = this read
	tcmp	r4, #0
	tjeq	.Lrc_again	@ the first read: read again
	tsub	r3, r5, r6
	tcmp	r3, #1
	tjhi	.Lrc_differ	@ not the same or one up
.Lrc_out:
	tmov	r0, r5
	tpop	{r4, r5, r6, pc}
.Lrc_earlier:
	tmov	r5, r6
	tj	.Lrc_out
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_read_analog40, . - tlsr8278_setup_read_analog40

/*
 * 0x560 get_32k_tick -> r0: 0x800748 bit 2 cleared; 0x80074a bits 3:0
 * cleared (bit 0: read mode; bit 1: the system timer stopped; bit 3: the
 * 32 kHz calibration off; bit 2, reserved, cleared too); a wait until
 * 0x80074b bit 5 (reserved) reads 0 and then 1; 0x80074a bit 2 set; the
 * 32 kHz timer's read value from 0x800750.
 */
	.global tlsr8278_setup_get_32k_tick
	.type tlsr8278_setup_get_32k_tick, %function
tlsr8278_setup_get_32k_tick:
	tloadr	r3, .Lgt_mask
	tloadrb	r2, [r3]
	tmov	r1, #4
	tbclr	r2, r1
	tstorerb	r2, [r3]	@ 0x800748 bit 2 clear
	tadd	r3, #2
	tloadrb	r2, [r3]
	tmov	r1, #0xf
	tbclr	r2, r1
	tstorerb	r2, [r3]	@ 0x80074a bits 3:0 clear
	tloadr	r1, .Lgt_status
	tmov	r2, #0x20
1:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjne	1b		@ until 0x80074b bit 5 is low
	tloadr	r1, .Lgt_status
	tmov	r2, #0x20
1:	tloadrb	r3, [r1]
	tnand	r2, r3
	tjeq	1b		@ and high again
	tloadr	r3, .Lgt_ctrl
	tloadrb	r1, [r3]
	tmov	r2, #4
	tor	r2, r1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
	tstorerb	r2, [r3]	@ 0x80074a bit 2 set
	tadd	r3, #6
	tloadr	r0, [r3]	@ 0x800750: the 32 kHz timer read value
	tjex	lr
.Lgt_mask:	.word	0x00800748
.Lgt_status:	.word	0x0080074b
.Lgt_ctrl:	.word	0x0080074a
	.size tlsr8278_setup_get_32k_tick, . - tlsr8278_setup_get_32k_tick

/*
 * 0x5a8 set_32k_tick(tick in r0): 0x80074a bit 0 set (32 kHz timer write
 * mode), a wait while 0x80074b bit 6 (busy) reads 1, tick to 0x80074c (the
 * write value), 8 to 0x80074b (bit 3, not documented; the datasheet lists
 * 0x80074b as read-only), 16 nops, and a wait until 0x80074b bit 3 reads 0.
 * Not called.
 */
	.global tlsr8278_setup_set_32k_tick
	.type tlsr8278_setup_set_32k_tick, %function
tlsr8278_setup_set_32k_tick:
	tpush	{r4, lr}
	tloadr	r3, .Lst_ctrl
	tloadrb	r1, [r3]
	tmov	r2, #1
	tor	r2, r1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
	tstorerb	r2, [r3]	@ 0x80074a bit 0 set
	tloadr	r4, .Lst_status
	tmov	r1, #0x40
1:	tloadr	r2, .Lst_status
	tloadrb	r3, [r4]
	tnand	r1, r3
	tjne	1b		@ while 0x80074b bit 6 reads 1
	tloadr	r3, .Lst_wr
	tstorer	r0, [r3]	@ tick to 0x80074c
	tmov	r3, #8
	tstorerb	r3, [r2]	@ 0x80074b = 8
	.rept	16
	nop
	.endr
	tmov	r1, #8
1:	tloadrb	r3, [r2]
	tnand	r1, r3
	tjne	1b		@ until 0x80074b bit 3 reads 0
	tpop	{r4, pc}
	.short	0x46c0			@ pad (never executed)
.Lst_ctrl:	.word	0x0080074a
.Lst_status:	.word	0x0080074b
.Lst_wr:	.word	0x0080074c
	.size tlsr8278_setup_set_32k_tick, . - tlsr8278_setup_set_32k_tick

/*
 * 0x604 chip_init(mode in r0, cap in r1): the first setup after
 * reset; soc.c calls it with (0x43, 0).
 * 1. Resets and clocks: 0x800060, 0x800061, 0x800062 = 0, 0, 8 (bit 3 of
 *    0x800062 is the random number generator's reset); 0x800063, 0x800064,
 *    0x800065 = 0xff, 0xff, 0xf7 (all clock enables but bit 3 of 0x800065,
 *    the random number generator's); 0x800066 = 6, the system clock
 *    select's reset value (clock_init changes it).
 * 2. Analog writes, in this order: 0x06 = 0xce (bits 1 and 2 power the
 *    low-power comparator and the temperature sensor down; the other bits
 *    not documented); 0x2c bit 5 and 0x05 bit 3 each set and cleared
 *    again; 0x82 = 0x65; 0x34 = 0x80; 0x0b bits 6:0 = 0x38 with bit 7 (the
 *    USB D+ pull-up) kept; 0x8c = 0x02; 0x00 bit 7 cleared; 0x02 = 0xa4;
 *    0x01 = 0x4d; when cap is 1, 0x2d bit 6 set; 0x0a = mode; 0x0c = 0xa4
 *    unless mode is 0x40; 0x27-0x2a = 0 (the pad wakeup enables, all off).
 * 3. 0x800c40-0x800c48 = 0x04 each; 0x800c20 = 0, 0x800c21 = 0; 0x8005b5
 *    bits 2 and 3 set (bit 3: GPIO interrupt enable).
 * 4. A wait, with no time limit, until analog 0x88 bit 7 reads 1; then
 *    analog88_check.
 * 5. When analog 0x7f bit 0 reads 1 and the byte at tlsr8278_setup_wake_flags + 0
 *    is 0 (it is: .bss, cleared by the startup, and written only on the
 *    other path): 0x80074a bits 1 and 3 set (the system timer on, the
 *    32 kHz calibration on), chip_init_tail, 0x800748 bit 2 set.
 *    Otherwise (analog 0x7f bit 0 reads 0, or that byte is not 0): the byte
 *    at tlsr8278_setup_wake_flags + 0 = 1, + 2 = analog 0x44, + 1 = 1 when analog
 *    0x44 has bit 3 (pad) set and bit 1 (timer) clear; get_32k_tick; a call
 *    through the word at tlsr8278_setup_wake_flags + 4, which nothing sets (0: a
 *    branch to address 0, the reset vector); the result to the system timer
 *    (0x800740); 0x80074a bits 1 and 3 and 0x800748 bit 2 set. This image
 *    does not take this path.
 */
	.global tlsr8278_setup_chip_init
	.type tlsr8278_setup_chip_init, %function
tlsr8278_setup_chip_init:
	tpush	{r4, r5, r6, r7, lr}
	tmov	r7, r8
	tpush	{r7}
	tshftl	r7, r0, #24
	tshftr	r7, r7, #24	@ r7 = mode
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r8, r1		@ r8 = cap
	tmov	r3, #0
	tloadr	r2, .Lcw_rst0
	tstorerb	r3, [r2]	@ 0x800060 = 0
	tadd	r2, #1
	tstorerb	r3, [r2]	@ 0x800061 = 0
	tmov	r2, #8
	tloadr	r3, .Lcw_rst2
	tstorerb	r2, [r3]	@ 0x800062 = 8
	tmov	r3, #0xff
	tloadr	r2, .Lcw_clk0
	tstorerb	r3, [r2]	@ 0x800063 = 0xff
	tadd	r2, #1
	tstorerb	r3, [r2]	@ 0x800064 = 0xff
	tmov	r5, #0xf7
	tloadr	r3, .Lcw_clk2
	tstorerb	r5, [r3]	@ 0x800065 = 0xf7
	tmov	r2, #6
	tadd	r3, #1
	tstorerb	r2, [r3]	@ 0x800066 = 6
	tmov	r0, #6
	tmov	r1, #0xce
	tjl	tlsr8278_setup_analog_write	@ analog 0x06 = 0xce
	tmov	r0, #0x2c
	tjl	tlsr8278_setup_analog_read
	tmov	r6, r0		@ r6 = analog 0x2c
	tmov	r0, #5
	tjl	tlsr8278_setup_analog_read
	tmov	r4, r0		@ r4 = analog 0x05
	tmov	r1, #0x20
	tor	r1, r6
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #0x2c
	tjl	tlsr8278_setup_analog_write	@ analog 0x2c bit 5 set
	tmov	r1, #0xdf
	tand	r1, r6
	tmov	r0, #0x2c
	tjl	tlsr8278_setup_analog_write	@ and clear
	tmov	r1, #8
	tor	r1, r4
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #5
	tjl	tlsr8278_setup_analog_write	@ analog 0x05 bit 3 set
	tmov	r1, r5
	tand	r1, r4
	tmov	r0, #5
	tjl	tlsr8278_setup_analog_write	@ and clear (r5 = 0xf7)
	tmov	r0, #0x82
	tmov	r1, #0x65
	tjl	tlsr8278_setup_analog_write	@ analog 0x82 = 0x65
	tmov	r0, #0x34
	tmov	r1, #0x80
	tjl	tlsr8278_setup_analog_write	@ analog 0x34 = 0x80
	tmov	r0, #0xb
	tjl	tlsr8278_setup_analog_read
	tmov	r4, #0x7f
	tmov	r1, r0
	tbclr	r1, r4
	tmov	r3, #0x38
	tor	r1, r3
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #0xb
	tjl	tlsr8278_setup_analog_write	@ analog 0x0b = bit 7 kept | 0x38
	tmov	r0, #0x8c
	tmov	r1, #2
	tjl	tlsr8278_setup_analog_write	@ analog 0x8c = 0x02
	tmov	r0, #0
	tjl	tlsr8278_setup_analog_read
	tmov	r1, r4
	tand	r1, r0
	tmov	r0, #0
	tjl	tlsr8278_setup_analog_write	@ analog 0x00 bit 7 clear (r4 = 0x7f)
	tmov	r0, #2
	tmov	r1, #0xa4
	tjl	tlsr8278_setup_analog_write	@ analog 0x02 = 0xa4
	tmov	r0, #1
	tmov	r1, #0x4d
	tjl	tlsr8278_setup_analog_write	@ analog 0x01 = 0x4d
	tmov	r3, #1
	tcmp	r8, r3
	tjne	.Lcw_mode
	tj	.Lcw_extcap	@ cap = 1
.Lcw_mode:
	tmov	r0, #0xa
	tmov	r1, r7
	tjl	tlsr8278_setup_analog_write	@ analog 0x0a = mode
	tcmp	r7, #0x40
	tjeq	1f
	tmov	r0, #0xc
	tmov	r1, #0xa4
	tjl	tlsr8278_setup_analog_write	@ analog 0x0c = 0xa4 (mode not 0x40)
1:	tmov	r0, #0x27
	tmov	r1, #0
	tjl	tlsr8278_setup_analog_write	@ pad wakeup enables off
	tmov	r0, #0x28
	tmov	r1, #0
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0x29
	tmov	r1, #0
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0x2a
	tmov	r1, #0
	tjl	tlsr8278_setup_analog_write
	tloadr	r3, .Lcw_dma_hi
	tloadr	r2, .Lcw_dma0
	tstorer	r3, [r2]	@ 0x800c40-43 = 0x04
	tadd	r2, #4
	tstorer	r3, [r2]	@ 0x800c44-47 = 0x04
	tmov	r2, #4
	tloadr	r3, .Lcw_dma7
	tstorerb	r2, [r3]	@ 0x800c48 = 0x04
	tmov	r3, #0
	tloadr	r2, .Lcw_dma_en
	tstorerb	r3, [r2]	@ 0x800c20 = 0
	tadd	r2, #1
	tstorerb	r3, [r2]	@ 0x800c21 = 0
	tloadr	r3, .Lcw_gpio_wk
	tloadrb	r1, [r3]
	tmov	r2, #0xc
	tor	r2, r1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
	tstorerb	r2, [r3]	@ 0x8005b5 bits 2 and 3 set
1:	tmov	r0, #0x88
	tjl	tlsr8278_setup_analog_read
	tshftl	r3, r0, #24
	tjpl	1b		@ until analog 0x88 bit 7 reads 1
	tjl	tlsr8278_setup_analog88_check
	tmov	r0, #0x7f
	tjl	tlsr8278_setup_analog_read
	tmov	r3, #1
	tnand	r3, r0
	tjne	.Lcw_power_on	@ analog 0x7f bit 0 = 1
	tloadr	r4, .Lcw_pm
	tstorerb	r3, [r4]	@ wake_flags + 0 = 1 (r3 still 1: tnand writes no register)
.Lcw_retention:
	tmov	r0, #0x44
	tjl	tlsr8278_setup_analog_read
	tstorerb	r0, [r4, #2]	@ the wake status
	tmov	r0, #0x44
	tjl	tlsr8278_setup_analog_read
	tmov	r3, #0xa
	tand	r0, r3
	tcmp	r0, #8
	tjeq	.Lcw_pad	@ bit 3 (pad) set, bit 1 (timer) clear
.Lcw_recover:
	tjl	tlsr8278_setup_get_32k_tick
	tloadr	r3, .Lcw_recover_fn
	tloadr	r3, [r3]	@ the word at wake_flags + 4 (0)
	tjl	tlsr8278_setup_call_via_r3
	tloadr	r3, .Lcw_tick
	tstorer	r0, [r3]	@ system timer
	tadd	r3, #0xa
	tloadrb	r1, [r3]
	tmov	r2, #0xa
	tor	r2, r1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
	tstorerb	r2, [r3]	@ 0x80074a bits 1 and 3 set
.Lcw_done:
	tloadr	r3, .Lcw_mask
	tloadrb	r1, [r3]
	tmov	r2, #4
	tor	r2, r1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
	tstorerb	r2, [r3]	@ 0x800748 bit 2 set
	tpop	{r2}
	tmov	r8, r2
	tpop	{r4, r5, r6, r7, pc}
.Lcw_power_on:
	tloadr	r4, .Lcw_pm
	tloadrb	r3, [r4]
	tcmp	r3, #0
	tjne	.Lcw_retention	@ (0: .bss, cleared by the startup)
	tloadr	r3, .Lcw_ctrl
	tloadrb	r1, [r3]
	tmov	r2, #0xa
	tor	r2, r1
	tshftl	r2, r2, #24
	tshftr	r2, r2, #24
	tstorerb	r2, [r3]	@ 0x80074a bits 1 and 3 set
	tjl	tlsr8278_setup_chip_init_tail
	tj	.Lcw_done
.Lcw_pad:
	tmov	r3, #1
	tstorerb	r3, [r4, #1]	@ wake_flags + 1 = 1
	tj	.Lcw_recover
.Lcw_extcap:
	tmov	r0, #0x2d
	tjl	tlsr8278_setup_analog_read
	tmov	r3, #0x40
	tmov	r1, r0
	tor	r1, r3
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #0x2d
	tjl	tlsr8278_setup_analog_write	@ analog 0x2d bit 6 set
	tj	.Lcw_mode
.Lcw_rst0:	.word	0x00800060	@ reset 0 (1 follows)
.Lcw_rst2:	.word	0x00800062	@ reset 2
.Lcw_clk0:	.word	0x00800063	@ clock enable 0 (1 follows)
.Lcw_clk2:	.word	0x00800065	@ clock enable 2 (clock select follows)
.Lcw_dma_hi:	.word	0x04040404	@ four bytes of 0x04
.Lcw_dma0:	.word	0x00800c40
.Lcw_dma7:	.word	0x00800c48
.Lcw_dma_en:	.word	0x00800c20
.Lcw_gpio_wk:	.word	0x008005b5
.Lcw_pm:	.word	tlsr8278_setup_wake_flags + 0x0
.Lcw_recover_fn:	.word	tlsr8278_setup_wake_flags + 0x4
.Lcw_tick:	.word	0x00800740	@ system timer
.Lcw_mask:	.word	0x00800748
.Lcw_ctrl:	.word	0x0080074a	@ system timer control
	.size tlsr8278_setup_chip_init, . - tlsr8278_setup_chip_init

/*
 * 0x810 call_via_r3: jump to the address in r3; a bl here calls through a
 * function pointer (ARMv4T has no blx), and the called function returns
 * straight to the bl's caller.
 */
	.global tlsr8278_setup_call_via_r3
	.type tlsr8278_setup_call_via_r3, %function
tlsr8278_setup_call_via_r3:
	tjex	r3
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_call_via_r3, . - tlsr8278_setup_call_via_r3

/* 0x814 call_via_r3_b: a second copy of the same. Not called. */
	.global tlsr8278_setup_call_via_r3_b
	.type tlsr8278_setup_call_via_r3_b, %function
tlsr8278_setup_call_via_r3_b:
	tjex	r3
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_call_via_r3_b, . - tlsr8278_setup_call_via_r3_b

/*
 * 0x818 scale_count(count in r0) -> r0: (count - b) * k / 16 + a,
 * where k is the word at tlsr8278_setup_vars + 0x2c, and a and b are the words
 * at + 0x20 and + 0x24 when the word at + 0x28 is 3, else those at + 0x30
 * and + 0x34. Not called.
 */
	.global tlsr8278_setup_scale_count
	.type tlsr8278_setup_scale_count, %function
tlsr8278_setup_scale_count:
	tloadr	r3, .Ltr_saved
	tloadr	r2, [r3, #8]
	tcmp	r2, #3
	tjeq	2f
	tloadr	r3, .Ltr_count
	tloadr	r3, [r3]
	tsub	r0, r0, r3
	tloadr	r3, .Ltr_ratio
	tloadr	r3, [r3]
	tmul	r0, r3
	tshftr	r0, r0, #4
	tloadr	r3, .Ltr_tick
	tloadr	r3, [r3]
	tadd	r0, r0, r3
1:	tjex	lr
2:	tloadr	r2, [r3, #4]
	tsub	r0, r0, r2
	tloadr	r2, .Ltr_ratio
	tloadr	r2, [r2]
	tmul	r0, r2
	tshftr	r0, r0, #4
	tloadr	r3, [r3]
	tadd	r0, r0, r3
	tj	1b
.Ltr_saved:	.word	tlsr8278_setup_vars + 0x20
.Ltr_count:	.word	tlsr8278_setup_vars + 0x34
.Ltr_ratio:	.word	tlsr8278_setup_vars + 0x2c
.Ltr_tick:	.word	tlsr8278_setup_vars + 0x30
	.size tlsr8278_setup_scale_count, . - tlsr8278_setup_scale_count

/*
 * 0x858 seq_c7: analog 0xc8 = 0x80; analog 0x30 bit 7 set; analog
 * 0xc7 = 0x0e, then 0x0f; a wait until analog 0xcf bit 7 reads 1; analog
 * 0xcb copied to analog 0x33; analog 0x30 bit 7 cleared; analog 0xc7 =
 * 0x0e; analog 0x33 kept in the byte at tlsr8278_setup_vars + 0x1c.
 */
	.global tlsr8278_setup_seq_c7
	.type tlsr8278_setup_seq_c7, %function
tlsr8278_setup_seq_c7:
	tpush	{lr}
	tmov	r0, #0xc8
	tmov	r1, #0x80
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0x30
	tjl	tlsr8278_setup_analog_read
	tmov	r3, #0x80
	tneg	r3, r3		@ r3 = 0xffffff80: bit 7 (the byte store drops the rest)
	tor	r3, r0
	tshftl	r1, r3, #24
	tshftr	r1, r1, #24
	tmov	r0, #0x30
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xc7
	tmov	r1, #0xe
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xc7
	tmov	r1, #0xf
	tjl	tlsr8278_setup_analog_write
1:	tmov	r0, #0xcf
	tjl	tlsr8278_setup_analog_read
	tshftl	r3, r0, #24
	tjpl	1b
	tmov	r0, #0xcb
	tjl	tlsr8278_setup_analog_read
	tmov	r1, r0
	tmov	r0, #0x33
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0x30
	tjl	tlsr8278_setup_analog_read
	tmov	r1, #0x7f
	tand	r1, r0
	tmov	r0, #0x30
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xc7
	tmov	r1, #0xe
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0x33
	tjl	tlsr8278_setup_analog_read
	tloadr	r3, .Lr24_var
	tstorerb	r0, [r3]
	tpop	{pc}
.Lr24_var:	.word	tlsr8278_setup_vars + 0x1c
	.size tlsr8278_setup_seq_c7, . - tlsr8278_setup_seq_c7

/*
 * 0x8c8 seq_c6: analog 0x30 = 0x60; analog 0xc6 = 0xf6, then 0xf7; a
 * wait until analog 0xcf bit 6 reads 1; analog 0xc9 copied to analog 0x32
 * and analog 0xca to analog 0x31; analog 0xc6 = 0xf6; analog 0x30 = 0x20.
 */
	.global tlsr8278_setup_seq_c6
	.type tlsr8278_setup_seq_c6, %function
tlsr8278_setup_seq_c6:
	tpush	{r4, lr}
	tmov	r0, #0x30
	tmov	r1, #0x60
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xc6
	tmov	r1, #0xf6
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xc6
	tmov	r1, #0xf7
	tjl	tlsr8278_setup_analog_write
	tmov	r4, #0x40
1:	tmov	r0, #0xcf
	tjl	tlsr8278_setup_analog_read
	tnand	r4, r0
	tjeq	1b
	tmov	r0, #0xc9
	tjl	tlsr8278_setup_analog_read
	tmov	r1, r0
	tmov	r0, #0x32
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xca
	tjl	tlsr8278_setup_analog_read
	tmov	r1, r0
	tmov	r0, #0x31
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0xc6
	tmov	r1, #0xf6
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #0x30
	tmov	r1, #0x20
	tjl	tlsr8278_setup_analog_write
	tpop	{r4, pc}
	.size tlsr8278_setup_seq_c6, . - tlsr8278_setup_seq_c6

/*
 * 0x91c flash_read_jedec_id -> r0: the flash's JEDEC ID (command 0x9f, no
 * address, no dummy bytes, 3 bytes read into a zeroed word on the stack):
 * manufacturer in bits 7:0, then memory type and capacity.
 */
	.global tlsr8278_setup_flash_read_jedec_id
	.type tlsr8278_setup_flash_read_jedec_id, %function
tlsr8278_setup_flash_read_jedec_id:
	tpush	{lr}
	tsub	sp, #0xc
	tmov	r3, #0
	tstorer	r3, [sp, #8]	@ the ID word = 0
	tadd	r3, sp, #8
	tstorer	r3, [sp]	@ buf = &ID word (fifth argument)
	tmov	r3, #3
	tstorer	r3, [sp, #4]	@ len = 3 (sixth argument)
	tmov	r0, #0x9f
	tmov	r1, #0
	tmov	r2, #0
	tmov	r3, #0
	tjl	tlsr8278_setup_flash_read_cmd
	tloadr	r0, [sp, #8]
	tadd	sp, #0xc
	tpop	{pc}
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_flash_read_jedec_id, . - tlsr8278_setup_flash_read_jedec_id

/*
 * 0x940 flash_is_zbit -> r0: 1 when the flash is a Zbit part (JEDEC ID
 * 0x14325e, 0x13325e, 0x12325e or 0x11325e), else 0. Each compare adds the
 * ID's negative.
 */
	.global tlsr8278_setup_flash_is_zbit
	.type tlsr8278_setup_flash_is_zbit, %function
tlsr8278_setup_flash_is_zbit:
	tpush	{lr}
	tjl	tlsr8278_setup_flash_read_jedec_id
	tloadr	r2, .Lzb_14
	tadd	r3, r0, r2
	tcmp	r3, #0
	tjeq	2f
	tloadr	r2, .Lzb_13
	tadd	r3, r0, r2
	tcmp	r3, #0
	tjeq	2f
	tloadr	r3, .Lzb_12
	tadd	r2, r0, r3
	tneg	r3, r2
	taddc	r2, r3		@ r2 = (ID == 0x12325e)
	tloadr	r3, .Lzb_11
	tadd	r0, r0, r3
	tneg	r3, r0
	taddc	r3, r0		@ r3 = (ID == 0x11325e)
	tmov	r0, r2
	tor	r0, r3
1:	tpop	{pc}
2:	tmov	r0, #1
	tj	1b
.Lzb_14:	.word	0xffebcda2	@ -0x14325e
.Lzb_13:	.word	0xffeccda2	@ -0x13325e
.Lzb_12:	.word	0xffedcda2	@ -0x12325e
.Lzb_11:	.word	0xffeecda2	@ -0x11325e
	.size tlsr8278_setup_flash_is_zbit, . - tlsr8278_setup_flash_is_zbit

/*
 * 0x980 pwm_pc3_10ms: a square wave on PC3 (the pin xtl_32k_in, also
 * PWM1) for 10 ms: 0x800066 = 0x43 (bits 6:5 = 10: the fast clock divided
 * by bits 4:0 = 3, so 16 MHz with 0x800070 bit 0 at its reset value 0);
 * 0x800596 = 0xf7 (the whole byte: every PC pin acts as GPIO except PC3);
 * 0x800798 = 0x00020001 (PWM1 high time 1, cycle time 2); 0x800780 = 2 (PWM1
 * on); 0x800782 = 0xf3 (PWM clock: 244 system clocks, so the output is
 * 16 MHz / 244 / 2 = 32.79 kHz); sleep_us(10000). Then 0x800066 = 6 (its reset value),
 * 0x800596 = 0xff, 0x800798 = 0, 0x800780 = 0, 0x800782 = 0.
 */
	.global tlsr8278_setup_pwm_pc3_10ms
	.type tlsr8278_setup_pwm_pc3_10ms, %function
tlsr8278_setup_pwm_pc3_10ms:
	tpush	{r4, r5, r6, r7, lr}
	tmov	r7, r8
	tpush	{r7}
	tloadr	r6, .Lpk_clk
	tmov	r3, #0x43
	tstorerb	r3, [r6]	@ system clock 16 MHz
	tloadr	r5, .Lpk_pc_gpio
	tmov	r3, #0xf7
	tstorerb	r3, [r5]	@ 0x800596 = 0xf7
	tloadr	r4, .Lpk_pwm1
	tloadr	r3, .Lpk_cycle
	tstorer	r3, [r4]	@ PWM1: high time 1, cycle time 2
	tloadr	r7, .Lpk_pwm_en
	tmov	r3, #2
	tstorerb	r3, [r7]	@ PWM1 on
	tloadr	r2, .Lpk_pwm_clk
	tmov	r8, r2
	tmov	r3, #0xf3
	tstorerb	r3, [r2]	@ PWM clock: 244 system clocks
	tloadr	r0, .Lpk_10ms
	tjl	tlsr8278_setup_sleep_us
	tmov	r3, #6
	tstorerb	r3, [r6]	@ clock select back to 6
	tmov	r3, #0xff
	tstorerb	r3, [r5]	@ 0x800596 = 0xff
	tmov	r3, #0
	tstorer	r3, [r4]	@ 0x800798 = 0
	tstorerb	r3, [r7]	@ PWM off
	tmov	r2, r8
	tstorerb	r3, [r2]	@ PWM clock divider 0
	tpop	{r2}
	tmov	r8, r2
	tpop	{r4, r5, r6, r7, pc}
.Lpk_clk:	.word	0x00800066	@ system clock select
.Lpk_pc_gpio:	.word	0x00800596	@ port C pins act as GPIO
.Lpk_pwm1:	.word	0x00800798	@ PWM1 high time (15:0), cycle time (31:16)
.Lpk_cycle:	.word	0x00020001	@ cycle time 2, high time 1
.Lpk_pwm_en:	.word	0x00800780	@ PWM enable
.Lpk_pwm_clk:	.word	0x00800782	@ PWM clock divider
.Lpk_10ms:	.word	10000		@ microseconds
	.size tlsr8278_setup_pwm_pc3_10ms, . - tlsr8278_setup_pwm_pc3_10ms

/*
 * 0x9e0 read_word_4c -> r0: 0x800074 = 0x62, the word at 0x80004c,
 * 0x800074 = 0 (neither register is documented). chip_init_tail uses
 * the low byte.
 */
	.global tlsr8278_setup_read_word_4c
	.type tlsr8278_setup_read_word_4c, %function
tlsr8278_setup_read_word_4c:
	tloadr	r3, .Ltw_key
	tmov	r2, #0x62
	tstorerb	r2, [r3]
	tloadr	r2, .Ltw_word
	tloadr	r0, [r2]
	tmov	r2, #0
	tstorerb	r2, [r3]
	tjex	lr
.Ltw_key:	.word	0x00800074
.Ltw_word:	.word	0x0080004c
	.size tlsr8278_setup_read_word_4c, . - tlsr8278_setup_read_word_4c

/*
 * 0x9f8 pwm_pc3_until_moving: analog 0x2d = 0x95, analog 0x05 = 0, then up to ten
 * times: pwm_pc3_10ms, read_analog40, sleep_us(305),
 * read_analog40; stop when the two reads differ.
 */
	.global tlsr8278_setup_pwm_pc3_until_moving
	.type tlsr8278_setup_pwm_pc3_until_moving, %function
tlsr8278_setup_pwm_pc3_until_moving:
	tpush	{r4, r5, lr}
	tmov	r0, #0x2d
	tmov	r1, #0x95
	tjl	tlsr8278_setup_analog_write
	tmov	r0, #5
	tmov	r1, #0
	tjl	tlsr8278_setup_analog_write
	tmov	r4, #0xa	@ ten tries
	tj	2f
1:	tsub	r4, #1
	tcmp	r4, #0
	tjeq	3f
2:	tjl	tlsr8278_setup_pwm_pc3_10ms
	tjl	tlsr8278_setup_read_analog40
	tmov	r5, r0
	tloadr	r0, .Lxs_wait
	tjl	tlsr8278_setup_sleep_us
	tjl	tlsr8278_setup_read_analog40
	tcmp	r5, r0
	tjeq	1b		@ equal: again
3:	tpop	{r4, r5, pc}
	.short	0x46c0			@ pad (never executed)
.Lxs_wait:	.word	0x00000131	@ 305 us
	.size tlsr8278_setup_pwm_pc3_until_moving, . - tlsr8278_setup_pwm_pc3_until_moving

/*
 * 0xa34 chip_init_tail: the rest of chip_init on its first
 * path.
 * 1. When the byte at tlsr8278_setup_vars + 0x19 is 0 (it is): seq_c6.
 *    Otherwise: when analog 0x3b (kept across all but a power-on reset)
 *    reads 0, its bit 0 is set and pwm_pc3_until_moving runs; when it is not 0,
 *    sleep_us(6000).
 * 2. When the low byte of read_word_4c is not 0 and the halfword at
 *    tlsr8278_setup_vars + 2 is not 0, the halfword at tlsr8278_setup_vars + 0
 *    (1175 initially) becomes 1047 + that byte. Nothing in this image reads
 *    it.
 * 3. When 0x80063e (a halfword) reads 0: the word at tlsr8278_setup_boot_vars
 *    + 0x18 takes the one at + 4. Otherwise: 0x80063e is written with its
 *    bits 2:0 kept and (tlsr8278_setup_boot_vars + 0) / 4 * 8 above them; the
 *    halfword 0xa55a to SRAM 0x840014 and the same 0x80063e value to
 *    0x840016; the word at + 0x18 = 0. What 0x80063e holds, and what the
 *    boot ROM does with SRAM 0x840014-0x840017, is not documented.
 * 4. flash_is_zbit's answer kept in the byte at tlsr8278_setup_vars + 9.
 */
	.global tlsr8278_setup_chip_init_tail
	.type tlsr8278_setup_chip_init_tail, %function
tlsr8278_setup_chip_init_tail:
	tpush	{lr}
	tloadr	r3, .Lwt_vars_10
	tloadrb	r3, [r3, #9]	@ tlsr8278_setup_vars + 0x19
	tcmp	r3, #0
	tjeq	.Lwt_rc
	tmov	r0, #0x3b
	tjl	tlsr8278_setup_analog_read
	tcmp	r0, #0
	tjne	.Lwt_running
	tmov	r0, #0x3b
	tjl	tlsr8278_setup_analog_read
	tmov	r3, #1
	tmov	r1, r0
	tor	r1, r3
	tshftl	r1, r1, #24
	tshftr	r1, r1, #24
	tmov	r0, #0x3b
	tjl	tlsr8278_setup_analog_write	@ analog 0x3b bit 0 set
	tjl	tlsr8278_setup_pwm_pc3_until_moving
	tjl	tlsr8278_setup_read_word_4c
	tmov	r3, #0xff
	tnand	r3, r0
	tjne	.Lwt_vref
.Lwt_slot:
	tloadr	r2, .Lwt_slot_reg
	tloadrh	r3, [r2]
	tcmp	r3, #0
	tjne	.Lwt_second
.Lwt_first:
	tloadr	r3, .Lwt_boot_4
	tloadr	r2, [r3]
	tloadr	r3, .Lwt_boot_18
	tstorer	r2, [r3]	@ boot_vars + 0x18 = boot_vars + 4
.Lwt_flash:
	tjl	tlsr8278_setup_flash_is_zbit
	tloadr	r3, .Lwt_zb
	tstorerb	r0, [r3]
	tpop	{pc}
.Lwt_rc:
	tjl	tlsr8278_setup_seq_c6
.Lwt_word:
	tjl	tlsr8278_setup_read_word_4c
	tmov	r3, #0xff
	tnand	r3, r0
	tjeq	.Lwt_slot	@ the low byte is 0
.Lwt_vref:
	tloadr	r3, .Lwt_vars
	tloadrb	r2, [r3, #2]
	tloadrb	r1, [r3, #3]
	tshftl	r1, r1, #8
	tor	r1, r2		@ the halfword at vars + 2
	tjeq	.Lwt_slot
	tshftl	r0, r0, #24
	tshftr	r0, r0, #8	@ the low byte << 16
	tloadr	r2, .Lwt_1047
	tadd	r0, r0, r2	@ (1047 + the byte) << 16
	tshftr	r2, r0, #16
	tstorerb	r2, [r3]
	tshftr	r0, r0, #24
	tstorerb	r0, [r3, #1]	@ vars + 0 = 1047 + the byte
	tloadr	r2, .Lwt_slot_reg
	tloadrh	r3, [r2]
	tcmp	r3, #0
	tjeq	.Lwt_first
.Lwt_second:
	tloadr	r1, .Lwt_boot_0
	tloadr	r1, [r1]
	tasr	r1, r1, #2
	tshftl	r1, r1, #3	@ boot_vars + 0, / 4 * 8
	tmov	r0, #7
	tand	r3, r0
	tor	r3, r1
	tshftl	r3, r3, #16
	tshftr	r3, r3, #16
	tstorerh	r3, [r2]	@ 0x80063e
	tloadr	r1, .Lwt_marker
	tloadr	r2, .Lwt_header
	tstorerh	r1, [r2]	@ 0x840014 = 0xa55a
	tadd	r2, #2
	tstorerh	r3, [r2]	@ 0x840016 = the same halfword
	tmov	r2, #0
	tloadr	r3, .Lwt_boot_18
	tstorer	r2, [r3]	@ boot_vars + 0x18 = 0
	tj	.Lwt_flash
.Lwt_running:
	tloadr	r0, .Lwt_6ms
	tjl	tlsr8278_setup_sleep_us
	tj	.Lwt_word
	.short	0x46c0			@ pad (never executed)
.Lwt_vars_10:	.word	tlsr8278_setup_vars + 0x10
.Lwt_slot_reg:	.word	0x0080063e
.Lwt_boot_4:	.word	tlsr8278_setup_boot_vars + 0x4
.Lwt_boot_18:	.word	tlsr8278_setup_boot_vars + 0x18
.Lwt_zb:	.word	tlsr8278_setup_vars + 0x9
.Lwt_vars:	.word	tlsr8278_setup_vars + 0x0
.Lwt_1047:	.word	0x04170000	@ 1047 << 16
.Lwt_boot_0:	.word	tlsr8278_setup_boot_vars + 0x0
.Lwt_marker:	.word	0xffffa55a	@ its low half is stored
.Lwt_header:	.word	0x00840014	@ SRAM 0x840014
.Lwt_6ms:	.word	6000		@ microseconds
	.size tlsr8278_setup_chip_init_tail, . - tlsr8278_setup_chip_init_tail

/*
 * The variables these functions use, with their initial values. Only the
 * bytes named are used here; nothing else reads the others.
 */
	.section .data.tlsr8278_setup, "aw"
	.balign 4
	.global tlsr8278_setup_vars
	.type tlsr8278_setup_vars, %object
tlsr8278_setup_vars:
	.short	1175			@ + 0x00: set by chip_init_tail; not read here
	.short	1			@ + 0x02: not 0: chip_init_tail sets + 0x00
	.byte	0			@ + 0x04: the clock select clock_init wrote
	.byte	0, 0, 0, 0
	.byte	0			@ + 0x09: flash_is_zbit's answer
	.byte	0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0
	.byte	1			@ + 0x14: not 0: suspend_enter writes 0x87 to analog 0x34
	.byte	0, 0, 0, 0
	.byte	0			@ + 0x19: 0: chip_init_tail runs seq_c6
	.byte	0, 0
	.byte	0x80			@ + 0x1c: analog 0x33 as seq_c7 left it
	.byte	0, 0, 0
	.word	0			@ + 0x20: scale_count's a when + 0x28 is 3
	.word	0			@ + 0x24: its b when + 0x28 is 3
	.word	0			@ + 0x28
	.word	0			@ + 0x2c: its k
	.word	0			@ + 0x30: its a otherwise
	.word	0			@ + 0x34: its b otherwise
	.size tlsr8278_setup_vars, . - tlsr8278_setup_vars

	.section .data.tlsr8278_setup, "aw"
	.balign 4
	.global tlsr8278_setup_boot_vars
	.type tlsr8278_setup_boot_vars, %object
tlsr8278_setup_boot_vars:
	.word	0x80			@ + 0x00: / 4 * 8 goes to 0x80063e (0x80063e not 0)
	.word	0x20000			@ + 0x04: copied to + 0x18 (0x80063e 0)
	.word	0, 0, 0
	.word	30000000		@ + 0x14: not used here
	.word	0			@ + 0x18: set by chip_init_tail
	.size tlsr8278_setup_boot_vars, . - tlsr8278_setup_boot_vars

	.section .bss.tlsr8278_setup, "aw"
	.balign 4
	.global tlsr8278_setup_wake_flags
	.type tlsr8278_setup_wake_flags, %object
tlsr8278_setup_wake_flags:
	.space	1			@ + 0: 1 on chip_init's second path
	.space	1			@ + 1: 1 when analog 0x44 showed bit 3 and not bit 1
	.space	1			@ + 2: analog 0x44 as read there
	.space	1
	.space	4			@ + 4: called through there; nothing sets it
	.size tlsr8278_setup_wake_flags, . - tlsr8278_setup_wake_flags
