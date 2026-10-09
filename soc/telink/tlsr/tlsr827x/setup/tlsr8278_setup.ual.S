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
 * Instructions: tst rn, rm sets the flags from rn & rm and writes no
 * register. add with a high register (r8-r15) sets no flags.
 *
 * Waits: the count loops (a counter on the stack, counted up to a limit)
 * take a fixed number of instructions per count, given at each loop; their
 * time in microseconds is not measured. mov r8, r8 (nop) changes nothing
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

/*
 * The standard-syntax twin of tlsr8278_setup.S: the same code and data in
 * unified Thumb syntax, the TC32-only instructions as .inst.n with the Thumb
 * value that stands for each (the Telink instruction in the comment). The
 * direct path (TC32_DIRECT) assembles this file with clang -mcpu=tc32; the
 * Thumb path assembles tlsr8278_setup.S through tc32asm2thumb.py. Every
 * build assembles both with the same flags and compares the objects
 * (asm_twin_check.py).
 */
	.syntax unified
	.thumb

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
	movs r2, #0	@ mode 0: unsigned quotient
	b tlsr8278_setup_div
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
	.inst.n 0xbbcb	@ tmrcs r3; r3 = CPSR
	push {r3, r4}	@ keep the old CPSR (and r4, used below)
	movs r4, #0x80	@ the CPSR's I bit
	orrs r3, r4
	.inst.n 0xbbc3	@ tmcsr r3; CPSR = r3: IRQs masked
	ldr r3, .Ldiv_a
	str r0, [r3]	@ a to 0x800664
	adds r3, #4
	str r1, [r3]	@ b to 0x800668
	subs r3, #8
	strb r2, [r3]	@ the mode byte to 0x800660
1:	ldrb r0, [r3]
	cmp r0, #0
	bne 1b	@ until 0x800660 reads 0
	cmp r2, #1
	bls 2f	@ mode 0 or 1: 0x800664
	adds r3, #8
	ldr r0, [r3]	@ otherwise 0x800668
	b 3f
2:	adds r3, #4
	ldr r0, [r3]	@ 0x800664
3:	pop {r3, r4}
	.inst.n 0xbbc3	@ tmcsr r3; the old CPSR back
	bx lr
.Ldiv_a:	.word	0x00800664
	.word	0x00800660		@ (not loaded)
	.word	0x00800668		@ (not loaded)
	.short	0x46c0			@ pad (never executed)
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_div, . - tlsr8278_setup_div

/*
 * 0x044 mul_add(y in r0, a in r1, n in r2, b in r3): y[0..n-1] += a[0..n-1]
 * * b over 32-bit words with the carry running through, and y[n] = the final
 * carry word. After each muls the code reads the word at 0x8006fc (not
 * documented) and adds it into the carry as the product's high half. Words
 * go eight at a time, then one at a time. The carry word c is kept in r2:
 * after y + a*b(low) + c, that word and both additions' carries make the
 * next c; add r2, r10 (+1, a high register) leaves the flags of the second
 * addition for adcs. Not called.
 */
	.global tlsr8278_setup_mul_add
	.type tlsr8278_setup_mul_add, %function
tlsr8278_setup_mul_add:
	push {r4, r5, r6, r7}
	mov r4, r8
	mov r5, r9
	mov r6, r10
	mov r7, r11
	push {r4, r5, r6, r7}	@ the high registers are callee-saved too
	mov r8, r2	@ r8 = words left
	movs r2, #1
	mov r10, r2	@ r10 = 1
	subs r2, #9
	mov r9, r2	@ r9 = -8, the step of the eight-word loop
	movs r2, #0
	movs r2, #0	@ carry word c = 0 (set twice)
	ldr r6, .Lma_hi	@ r6 = 0x8006fc
	mov r11, r2	@ r11 = 0, for the loop compare
	b .Lma_loop8
.Lma_hi:	.word	0x008006fc
.Lma_8:	@ eight words: four times two, a[i] in r4 and a[i+1] in r5
	.rept	3
	muls r4, r3	@ low half of a[i] * b
	ldr r7, [r0]	@ y[i]
	adds r4, r4, r2	@ + c
	sbcs r2, r2	@ r2 = carry - 1
	adds r4, r4, r7	@ + y[i]
	ldr r7, [r6]	@ the word at 0x8006fc
	add r2, r10	@ r2 = the first addition's carry (flags kept)
	stm r0!, {r4}	@ y[i] done
	adcs r2, r7	@ c = carry + high + the second carry
	muls r5, r3	@ the same for a[i+1]
	ldr r7, [r0]
	adds r5, r5, r2
	sbcs r2, r2
	adds r5, r5, r7
	ldr r7, [r6]
	add r2, r10
	stm r0!, {r5}
	ldm r1!, {r4, r5}	@ the next two words of a
	adcs r2, r7
	.endr
	muls r4, r3	@ the fourth pair: as above, and the load
	ldr r7, [r0]	@ of the next two words comes at the loop
	adds r4, r4, r2	@ head
	sbcs r2, r2
	adds r4, r4, r7
	ldr r7, [r6]
	add r2, r10
	stm r0!, {r4}
	adcs r2, r7
	muls r5, r3
	ldr r7, [r0]
	adds r5, r5, r2
	sbcs r2, r2
	adds r5, r5, r7
	ldr r7, [r6]
	add r2, r10
	stm r0!, {r5}
	adcs r2, r7
.Lma_loop8:
	ldm r1!, {r4, r5}	@ a[i], a[i+1]
	add r8, r9	@ eight words fewer left
	cmp r8, r11
	bge .Lma_8	@ while eight or more were left
	mov r5, r8
	adds r5, #8	@ r5 = the 0-7 words still to do
	subs r1, #8	@ back over the two words loaded ahead
	b .Lma_loop1
.Lma_1:	@ one word: as one half of the pair above
	muls r4, r3
	ldr r7, [r0]
	adds r4, r4, r2
	sbcs r2, r2
	adds r4, r4, r7
	ldr r7, [r6]
	add r2, r10
	stm r0!, {r4}
	adcs r2, r7
.Lma_loop1:
	ldm r1!, {r4}
	subs r5, #1
	cmp r5, #0
	bge .Lma_1
	stm r0!, {r2}	@ y[n] = the last carry word
	pop {r4, r5, r6, r7}
	mov r8, r4
	mov r9, r5
	mov r10, r6
	mov r11, r7
	pop {r4, r5, r6, r7}
	bx lr
	mov r8, r8	@ never executed (after the return)
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
	push {r4, lr}
	lsls r0, r0, #24
	lsrs r0, r0, #24	@ the address is a byte
	ldr r3, .Lar_irq
	ldrb r4, [r3]	@ keep 0x800643
	movs r2, #0
	strb r2, [r3]	@ 0x800643 = 0: interrupts off
	ldr r3, .Lar_addr
	strb r0, [r3]	@ 0x8000b8 = address
	ldr r1, .Lar_ctrl
	movs r3, #0x40
	strb r3, [r1]	@ 0x8000ba = 0x40
	movs r2, #1
1:	ldrb r3, [r1]
	ands r3, r2
	bne 1b	@ until 0x8000ba bit 0 reads 0 (r3 = 0 after)
	ldr r2, .Lar_data
	ldrb r0, [r2]	@ the byte from 0x8000b9
	adds r2, #1
	strb r3, [r2]	@ 0x8000ba = 0
	ldr r3, .Lar_irq
	strb r4, [r3]	@ 0x800643 as it was
	pop {r4, pc}
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
	push {r4, lr}
	lsls r0, r0, #24
	lsrs r0, r0, #24	@ address and value are bytes
	lsls r1, r1, #24
	lsrs r1, r1, #24
	ldr r3, .Law_irq
	ldrb r4, [r3]	@ keep 0x800643
	movs r2, #0
	strb r2, [r3]	@ 0x800643 = 0: interrupts off
	ldr r3, .Law_addr
	strb r0, [r3]	@ 0x8000b8 = address
	adds r3, #1
	strb r1, [r3]	@ 0x8000b9 = value
	ldr r1, .Law_ctrl
	movs r3, #0x60
	strb r3, [r1]	@ 0x8000ba = 0x60
	movs r2, #1
1:	ldrb r3, [r1]
	ands r3, r2
	bne 1b	@ until 0x8000ba bit 0 reads 0 (r3 = 0 after)
	ldr r2, .Law_ctrl
	strb r3, [r2]	@ 0x8000ba = 0
	ldr r3, .Law_irq
	strb r4, [r3]	@ 0x800643 as it was
	pop {r4, pc}
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
	push {lr}
	lsls r0, r0, #24
	lsrs r0, r0, #24
	ldr r3, .Lci_sel
	strb r0, [r3]	@ system clock select
	ldr r3, .Lci_var
	strb r0, [r3]	@ tlsr8278_setup_vars + 4
	ldr r3, .Lci_pm
	ldrb r3, [r3]	@ tlsr8278_setup_wake_flags + 0
	cmp r3, #0
	bne 1f
	bl tlsr8278_setup_seq_c7
1:	pop {pc}
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
	lsls r2, r0, #8
	lsrs r2, r2, #24	@ bits 23:16
	ldr r3, .Lsa_data
	strb r2, [r3]
	ldr r1, .Lsa_ctrl
	movs r2, #0x10	@ 0x80000d bit 4
1:	ldrb r3, [r1]
	tst r2, r3
	bne 1b
	lsls r2, r0, #16
	lsrs r2, r2, #24	@ bits 15:8
	ldr r3, .Lsa_data
	strb r2, [r3]
	ldr r1, .Lsa_ctrl
	movs r2, #0x10
1:	ldrb r3, [r1]
	tst r2, r3
	bne 1b
	lsls r0, r0, #24
	lsrs r0, r0, #24	@ bits 7:0
	ldr r3, .Lsa_data
	strb r0, [r3]
	ldr r1, .Lsa_ctrl
	movs r2, #0x10
1:	ldrb r3, [r1]
	tst r2, r3
	bne 1b
	bx lr
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
	push {r4, r5, lr}
	adds r5, r0, #0	@ keep the command
	ldr r4, .Lsc_ctrl
	movs r3, #1
	strb r3, [r4]	@ 0x80000d = 1
	movs r0, #1
	bl tlsr8278_setup_sleep_us	@ for 1 us
	movs r3, #0
	strb r3, [r4]	@ 0x80000d = 0
	ldr r3, .Lsc_data
	strb r5, [r3]	@ the command byte to 0x80000c
	movs r2, #0x10
1:	ldrb r3, [r4]
	tst r2, r3
	bne 1b	@ until 0x80000d bit 4 reads 0
	pop {r4, r5, pc}
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
	push {r4, r5, r6, r7, lr}
	mov r7, r10
	mov r6, r8
	push {r6, r7}
	mov r10, r1	@ r10 = addr
	ldr r6, [sp, #0x1c]	@ r6 = buf (fifth argument)
	ldr r4, [sp, #0x20]	@ r4 = len (sixth argument)
	lsls r0, r0, #24
	lsrs r0, r0, #24	@ command byte
	lsls r2, r2, #24
	lsrs r7, r2, #24	@ r7 = with_addr
	lsls r5, r3, #24
	lsrs r5, r5, #24	@ r5 = zeros
	ldr r3, .Lrd_irq
	ldrb r2, [r3]
	mov r8, r2	@ keep 0x800643
	movs r2, #0
	strb r2, [r3]	@ 0x800643 = 0: interrupts off
	bl tlsr8278_setup_flash_send_cmd
	cmp r7, #0
	bne .Lrd_addr	@ send the address first (and come back)
.Lrd_dummy:
	cmp r5, #0
	beq 2f
	movs r0, #0	@ zeros: count r0 up to r5
	ldr r2, .Lrd_data
	mov r12, r2
	movs r7, #0
	ldr r1, .Lrd_ctrl
	movs r2, #0x10
1:	mov r3, r12
	strb r7, [r3]	@ 0 to 0x80000c
3:	ldrb r3, [r1]
	tst r2, r3
	bne 3b	@ until 0x80000d bit 4 reads 0
	adds r0, #1
	cmp r0, r5
	blt 1b
2:	movs r2, #0
	ldr r3, .Lrd_data
	strb r2, [r3]	@ one more 0 to 0x80000c
	ldr r0, .Lrd_ctrl
	movs r1, #0x10
1:	ldr r2, .Lrd_ctrl
	ldrb r3, [r0]
	tst r1, r3
	bne 1b	@ until 0x80000d bit 4 reads 0
	movs r3, #0x0a
	strb r3, [r2]	@ 0x80000d = 0x0a
	movs r1, #0x10
1:	ldrb r3, [r2]
	tst r1, r3
	bne 1b	@ until 0x80000d bit 4 reads 0
	cmp r4, #0
	beq 2f
	movs r0, #0	@ the data: count r0 up to len
	ldr r5, .Lrd_data
	ldr r1, .Lrd_ctrl
	movs r2, #0x10
1:	ldrb r3, [r5]	@ a byte from 0x80000c
	strb r3, [r6, r0]
3:	ldrb r3, [r1]
	tst r2, r3
	bne 3b	@ until 0x80000d bit 4 reads 0
	adds r0, #1
	cmp r0, r4
	bne 1b
2:	movs r2, #1
	ldr r3, .Lrd_ctrl
	strb r2, [r3]	@ 0x80000d = 1
	ldr r3, .Lrd_irq
	mov r2, r8
	strb r2, [r3]	@ 0x800643 as it was
	pop {r2, r3}
	mov r8, r2
	mov r10, r3
	pop {r4, r5, r6, r7, pc}
.Lrd_addr:
	mov r0, r10
	bl tlsr8278_setup_flash_send_addr
	b .Lrd_dummy
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
	ldr r2, .Lsl_tick
	ldr r1, [r2]	@ start
	lsls r0, r0, #4	@ us * 16
1:	ldr r3, [r2]
	subs r3, r3, r1	@ ticks since the start
	cmp r0, r3
	bhs 1b
	bx lr
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
	ldr r3, .Lss_pwdn
	movs r2, #0x81
	sub sp, #4
	nop
	nop
	strb r2, [r3]	@ 0x80006f = 0x81: suspend
	mov r3, sp
	adds r3, #3	@ the counter byte on the stack
	movs r2, #0
	b 2f
1:	ldrb r2, [r3]
	adds r2, #1
	lsls r2, r2, #24
	lsrs r2, r2, #24
2:	strb r2, [r3]
	ldrb r2, [r3]
	cmp r2, #0x2f
	bls 1b	@ up to 48
	add sp, #4
	bx lr
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
	push {r4, r5, r6, r7, lr}
	mov r7, r10
	mov r6, r8
	push {r6, r7}
	sub sp, #4
	movs r0, #0x81
	bl tlsr8278_setup_analog_read
	adds r4, r0, #0	@ r4 = analog 0x81
	movs r1, #0x40
	orrs r1, r0
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #0x81
	bl tlsr8278_setup_analog_write	@ analog 0x81 bit 6 set
	movs r3, #0xbf
	ands r3, r4
	mov r10, r3	@ r10 = analog 0x81 with bit 6 clear
	movs r6, #1	@ r6 = the test's number (1, 2, 3)
	movs r5, #0	@ r5 = tests that read 0 so far
	mov r4, sp
	adds r4, #3	@ a counter byte on the stack
	movs r7, #0
	movs r3, #0x20
	mov r8, r3	@ r8 = bit 5
.Lxc_check:
	strb r7, [r4]	@ count 0 to 30
	ldrb r3, [r4]
	cmp r3, #0x1d
	bhi 2f
1:	nop
	ldrb r3, [r4]
	adds r3, #1
	lsls r3, r3, #24
	lsrs r3, r3, #24
	strb r3, [r4]
	ldrb r3, [r4]
	cmp r3, #0x1d
	bls 1b
2:	movs r0, #0x88
	bl tlsr8278_setup_analog_read
	mov r3, r8
	tst r3, r0
	bne .Lxc_ok	@ analog 0x88 bit 5 = 1: done
	lsls r3, r5, #24
	lsrs r3, r3, #24
	cmp r3, #0
	beq .Lxc_first
	cmp r3, #1
	beq .Lxc_second
	movs r0, #0x81	@ the third 0: stop
	mov r1, r10
	bl tlsr8278_setup_analog_write	@ analog 0x81 bit 6 clear
	cmp r6, #2	@ (r6 is 3 here, so this never loops)
	bls .Lxc_next
.Lxc_out:
	add sp, #4
	pop {r2, r3}
	mov r8, r2
	mov r10, r3
	pop {r4, r5, r6, r7, pc}
.Lxc_first:
	movs r0, #1
	movs r1, #0x4e
	bl tlsr8278_setup_analog_write	@ analog 0x01 = 0x4e
.Lxc_next:
	adds r5, #1
	adds r6, #1
	lsls r6, r6, #24
	lsrs r6, r6, #24
	b .Lxc_check
.Lxc_second:
	movs r0, #1
	movs r1, #0x4b
	bl tlsr8278_setup_analog_write	@ analog 0x01 = 0x4b
	adds r5, #1
	adds r6, #1
	lsls r6, r6, #24
	lsrs r6, r6, #24
	b .Lxc_check
.Lxc_ok:
	movs r0, #0x81
	mov r1, r10
	bl tlsr8278_setup_analog_write	@ analog 0x81 bit 6 clear
	b .Lxc_out
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
	push {r4, r5, r6, lr}
	sub sp, #4	@ a counter word on the stack
	ldr r3, .Lse_vars
	ldrb r3, [r3, #4]	@ tlsr8278_setup_vars + 0x14
	movs r1, #0x85
	cmp r3, #0
	beq 1f
	movs r1, #0x87
1:	movs r0, #0x34
	bl tlsr8278_setup_analog_write	@ analog 0x34 = 0x85 or 0x87
	movs r0, #0xe4
	movs r1, #0x10
	bl tlsr8278_setup_analog_write	@ analog 0xe4 = 0x10
	movs r0, #0x82
	movs r1, #0x85
	bl tlsr8278_setup_analog_write	@ analog 0x82 = 0x85
	movs r3, #0
	ldr r2, .Lse_ctrl
	strb r3, [r2]	@ 0x80000d = 0
	movs r1, #0xb9
	subs r2, #1
	strb r1, [r2]	@ 0xb9 to 0x80000c
	str r3, [sp]	@ count 0 to 2
	ldr r3, [sp]
	cmp r3, #1
	bhi 2f
1:	ldr r3, [sp]
	adds r3, #1
	str r3, [sp]
	ldr r3, [sp]
	cmp r3, #1
	bls 1b
2:	ldr r5, .Lse_ctrl
	movs r3, #1
	strb r3, [r5]	@ 0x80000d = 1
	ldr r6, .Lse_pe_ie
	movs r4, #0
	strb r4, [r6]	@ 0x8005a1 = 0
	bl tlsr8278_setup_suspend_stall
	movs r3, #0xf
	strb r3, [r6]	@ 0x8005a1 = 0x0f
	strb r4, [r5]	@ 0x80000d = 0
	movs r2, #0xab
	ldr r3, .Lse_data
	strb r2, [r3]	@ 0xab to 0x80000c
	str r4, [sp]	@ count 0 to 2
	ldr r3, [sp]
	cmp r3, #1
	bhi 2f
1:	ldr r3, [sp]
	adds r3, #1
	str r3, [sp]
	ldr r3, [sp]
	cmp r3, #1
	bls 1b
2:	movs r2, #1
	ldr r3, .Lse_ctrl
	strb r2, [r3]	@ 0x80000d = 1
	movs r0, #0x2c
	bl tlsr8278_setup_analog_read
	adds r4, r0, #0
	movs r1, #0x20
	orrs r1, r0
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #0x2c
	bl tlsr8278_setup_analog_write	@ analog 0x2c bit 5 set
	movs r1, #0xdf
	ands r1, r4
	movs r0, #0x2c
	bl tlsr8278_setup_analog_write	@ and clear
	movs r0, #5
	bl tlsr8278_setup_analog_read
	adds r4, r0, #0
	movs r1, #8
	orrs r1, r0
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #5
	bl tlsr8278_setup_analog_write	@ analog 0x05 bit 3 set
	movs r1, #0xf7
	ands r1, r4
	movs r0, #5
	bl tlsr8278_setup_analog_write	@ and clear
	movs r0, #0x82
	movs r1, #0x65
	bl tlsr8278_setup_analog_write	@ analog 0x82 = 0x65
	movs r0, #0xe4
	movs r1, #0
	bl tlsr8278_setup_analog_write	@ analog 0xe4 = 0
	movs r0, #0x34
	movs r1, #0x80
	bl tlsr8278_setup_analog_write	@ analog 0x34 = 0x80
	movs r3, #0
	str r3, [sp]	@ count 0 to 300
	ldr r3, [sp]
	ldr r2, .Lse_300
	cmp r3, r2
	bhi 2f
1:	nop
	ldr r3, [sp]
	adds r3, #1
	str r3, [sp]
	ldr r3, [sp]
	cmp r3, r2
	bls 1b
2:	movs r0, #0x88
	bl tlsr8278_setup_analog_read
	lsls r3, r0, #24
	bpl 2b	@ until analog 0x88 bit 7
1:	movs r0, #0x88
	bl tlsr8278_setup_analog_read
	lsls r3, r0, #24
	bpl 1b	@ again
1:	movs r0, #0x88
	bl tlsr8278_setup_analog_read
	lsls r3, r0, #24
	bpl 1b	@ and again
	bl tlsr8278_setup_analog88_check
	add sp, #4
	pop {r4, r5, r6, pc}
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
	push {r4, r5, r6, lr}
	movs r4, #0	@ r4 = reads so far
	movs r6, #0	@ r6 = the read before
	b .Lrc_read
.Lrc_differ:
	adds r3, r5, #0
	eors r3, r6
	cmp r3, #1
	beq .Lrc_earlier	@ only bit 0 differs
.Lrc_again:
	adds r4, #1
	adds r6, r5, #0
.Lrc_read:
	movs r0, #0x43
	bl tlsr8278_setup_analog_read
	lsls r5, r0, #8
	movs r0, #0x42
	bl tlsr8278_setup_analog_read
	adds r5, r5, r0
	lsls r5, r5, #8
	movs r0, #0x41
	bl tlsr8278_setup_analog_read
	adds r5, r5, r0
	lsls r5, r5, #8
	movs r0, #0x40
	bl tlsr8278_setup_analog_read
	adds r5, r5, r0	@ r5 = this read
	cmp r4, #0
	beq .Lrc_again	@ the first read: read again
	subs r3, r5, r6
	cmp r3, #1
	bhi .Lrc_differ	@ not the same or one up
.Lrc_out:
	adds r0, r5, #0
	pop {r4, r5, r6, pc}
.Lrc_earlier:
	adds r5, r6, #0
	b .Lrc_out
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
	ldr r3, .Lgt_mask
	ldrb r2, [r3]
	movs r1, #4
	bics r2, r1
	strb r2, [r3]	@ 0x800748 bit 2 clear
	adds r3, #2
	ldrb r2, [r3]
	movs r1, #0xf
	bics r2, r1
	strb r2, [r3]	@ 0x80074a bits 3:0 clear
	ldr r1, .Lgt_status
	movs r2, #0x20
1:	ldrb r3, [r1]
	tst r2, r3
	bne 1b	@ until 0x80074b bit 5 is low
	ldr r1, .Lgt_status
	movs r2, #0x20
1:	ldrb r3, [r1]
	tst r2, r3
	beq 1b	@ and high again
	ldr r3, .Lgt_ctrl
	ldrb r1, [r3]
	movs r2, #4
	orrs r2, r1
	lsls r2, r2, #24
	lsrs r2, r2, #24
	strb r2, [r3]	@ 0x80074a bit 2 set
	adds r3, #6
	ldr r0, [r3]	@ 0x800750: the 32 kHz timer read value
	bx lr
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
	push {r4, lr}
	ldr r3, .Lst_ctrl
	ldrb r1, [r3]
	movs r2, #1
	orrs r2, r1
	lsls r2, r2, #24
	lsrs r2, r2, #24
	strb r2, [r3]	@ 0x80074a bit 0 set
	ldr r4, .Lst_status
	movs r1, #0x40
1:	ldr r2, .Lst_status
	ldrb r3, [r4]
	tst r1, r3
	bne 1b	@ while 0x80074b bit 6 reads 1
	ldr r3, .Lst_wr
	str r0, [r3]	@ tick to 0x80074c
	movs r3, #8
	strb r3, [r2]	@ 0x80074b = 8
	.rept	16
	nop
	.endr
	movs r1, #8
1:	ldrb r3, [r2]
	tst r1, r3
	bne 1b	@ until 0x80074b bit 3 reads 0
	pop {r4, pc}
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
	push {r4, r5, r6, r7, lr}
	mov r7, r8
	push {r7}
	lsls r7, r0, #24
	lsrs r7, r7, #24	@ r7 = mode
	lsls r1, r1, #24
	lsrs r1, r1, #24
	mov r8, r1	@ r8 = cap
	movs r3, #0
	ldr r2, .Lcw_rst0
	strb r3, [r2]	@ 0x800060 = 0
	adds r2, #1
	strb r3, [r2]	@ 0x800061 = 0
	movs r2, #8
	ldr r3, .Lcw_rst2
	strb r2, [r3]	@ 0x800062 = 8
	movs r3, #0xff
	ldr r2, .Lcw_clk0
	strb r3, [r2]	@ 0x800063 = 0xff
	adds r2, #1
	strb r3, [r2]	@ 0x800064 = 0xff
	movs r5, #0xf7
	ldr r3, .Lcw_clk2
	strb r5, [r3]	@ 0x800065 = 0xf7
	movs r2, #6
	adds r3, #1
	strb r2, [r3]	@ 0x800066 = 6
	movs r0, #6
	movs r1, #0xce
	bl tlsr8278_setup_analog_write	@ analog 0x06 = 0xce
	movs r0, #0x2c
	bl tlsr8278_setup_analog_read
	adds r6, r0, #0	@ r6 = analog 0x2c
	movs r0, #5
	bl tlsr8278_setup_analog_read
	adds r4, r0, #0	@ r4 = analog 0x05
	movs r1, #0x20
	orrs r1, r6
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #0x2c
	bl tlsr8278_setup_analog_write	@ analog 0x2c bit 5 set
	movs r1, #0xdf
	ands r1, r6
	movs r0, #0x2c
	bl tlsr8278_setup_analog_write	@ and clear
	movs r1, #8
	orrs r1, r4
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #5
	bl tlsr8278_setup_analog_write	@ analog 0x05 bit 3 set
	adds r1, r5, #0
	ands r1, r4
	movs r0, #5
	bl tlsr8278_setup_analog_write	@ and clear (r5 = 0xf7)
	movs r0, #0x82
	movs r1, #0x65
	bl tlsr8278_setup_analog_write	@ analog 0x82 = 0x65
	movs r0, #0x34
	movs r1, #0x80
	bl tlsr8278_setup_analog_write	@ analog 0x34 = 0x80
	movs r0, #0xb
	bl tlsr8278_setup_analog_read
	movs r4, #0x7f
	adds r1, r0, #0
	bics r1, r4
	movs r3, #0x38
	orrs r1, r3
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #0xb
	bl tlsr8278_setup_analog_write	@ analog 0x0b = bit 7 kept | 0x38
	movs r0, #0x8c
	movs r1, #2
	bl tlsr8278_setup_analog_write	@ analog 0x8c = 0x02
	movs r0, #0
	bl tlsr8278_setup_analog_read
	adds r1, r4, #0
	ands r1, r0
	movs r0, #0
	bl tlsr8278_setup_analog_write	@ analog 0x00 bit 7 clear (r4 = 0x7f)
	movs r0, #2
	movs r1, #0xa4
	bl tlsr8278_setup_analog_write	@ analog 0x02 = 0xa4
	movs r0, #1
	movs r1, #0x4d
	bl tlsr8278_setup_analog_write	@ analog 0x01 = 0x4d
	movs r3, #1
	cmp r8, r3
	bne .Lcw_mode
	b .Lcw_extcap	@ cap = 1
.Lcw_mode:
	movs r0, #0xa
	adds r1, r7, #0
	bl tlsr8278_setup_analog_write	@ analog 0x0a = mode
	cmp r7, #0x40
	beq 1f
	movs r0, #0xc
	movs r1, #0xa4
	bl tlsr8278_setup_analog_write	@ analog 0x0c = 0xa4 (mode not 0x40)
1:	movs r0, #0x27
	movs r1, #0
	bl tlsr8278_setup_analog_write	@ pad wakeup enables off
	movs r0, #0x28
	movs r1, #0
	bl tlsr8278_setup_analog_write
	movs r0, #0x29
	movs r1, #0
	bl tlsr8278_setup_analog_write
	movs r0, #0x2a
	movs r1, #0
	bl tlsr8278_setup_analog_write
	ldr r3, .Lcw_dma_hi
	ldr r2, .Lcw_dma0
	str r3, [r2]	@ 0x800c40-43 = 0x04
	adds r2, #4
	str r3, [r2]	@ 0x800c44-47 = 0x04
	movs r2, #4
	ldr r3, .Lcw_dma7
	strb r2, [r3]	@ 0x800c48 = 0x04
	movs r3, #0
	ldr r2, .Lcw_dma_en
	strb r3, [r2]	@ 0x800c20 = 0
	adds r2, #1
	strb r3, [r2]	@ 0x800c21 = 0
	ldr r3, .Lcw_gpio_wk
	ldrb r1, [r3]
	movs r2, #0xc
	orrs r2, r1
	lsls r2, r2, #24
	lsrs r2, r2, #24
	strb r2, [r3]	@ 0x8005b5 bits 2 and 3 set
1:	movs r0, #0x88
	bl tlsr8278_setup_analog_read
	lsls r3, r0, #24
	bpl 1b	@ until analog 0x88 bit 7 reads 1
	bl tlsr8278_setup_analog88_check
	movs r0, #0x7f
	bl tlsr8278_setup_analog_read
	movs r3, #1
	tst r3, r0
	bne .Lcw_power_on	@ analog 0x7f bit 0 = 1
	ldr r4, .Lcw_pm
	strb r3, [r4]	@ wake_flags + 0 = 1 (r3 still 1: tst writes no register)
.Lcw_retention:
	movs r0, #0x44
	bl tlsr8278_setup_analog_read
	strb r0, [r4, #2]	@ the wake status
	movs r0, #0x44
	bl tlsr8278_setup_analog_read
	movs r3, #0xa
	ands r0, r3
	cmp r0, #8
	beq .Lcw_pad	@ bit 3 (pad) set, bit 1 (timer) clear
.Lcw_recover:
	bl tlsr8278_setup_get_32k_tick
	ldr r3, .Lcw_recover_fn
	ldr r3, [r3]	@ the word at wake_flags + 4 (0)
	bl tlsr8278_setup_call_via_r3
	ldr r3, .Lcw_tick
	str r0, [r3]	@ system timer
	adds r3, #0xa
	ldrb r1, [r3]
	movs r2, #0xa
	orrs r2, r1
	lsls r2, r2, #24
	lsrs r2, r2, #24
	strb r2, [r3]	@ 0x80074a bits 1 and 3 set
.Lcw_done:
	ldr r3, .Lcw_mask
	ldrb r1, [r3]
	movs r2, #4
	orrs r2, r1
	lsls r2, r2, #24
	lsrs r2, r2, #24
	strb r2, [r3]	@ 0x800748 bit 2 set
	pop {r2}
	mov r8, r2
	pop {r4, r5, r6, r7, pc}
.Lcw_power_on:
	ldr r4, .Lcw_pm
	ldrb r3, [r4]
	cmp r3, #0
	bne .Lcw_retention	@ (0: .bss, cleared by the startup)
	ldr r3, .Lcw_ctrl
	ldrb r1, [r3]
	movs r2, #0xa
	orrs r2, r1
	lsls r2, r2, #24
	lsrs r2, r2, #24
	strb r2, [r3]	@ 0x80074a bits 1 and 3 set
	bl tlsr8278_setup_chip_init_tail
	b .Lcw_done
.Lcw_pad:
	movs r3, #1
	strb r3, [r4, #1]	@ wake_flags + 1 = 1
	b .Lcw_recover
.Lcw_extcap:
	movs r0, #0x2d
	bl tlsr8278_setup_analog_read
	movs r3, #0x40
	adds r1, r0, #0
	orrs r1, r3
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #0x2d
	bl tlsr8278_setup_analog_write	@ analog 0x2d bit 6 set
	b .Lcw_mode
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
	bx r3
	.short	0x46c0			@ pad (never executed)
	.size tlsr8278_setup_call_via_r3, . - tlsr8278_setup_call_via_r3

/* 0x814 call_via_r3_b: a second copy of the same. Not called. */
	.global tlsr8278_setup_call_via_r3_b
	.type tlsr8278_setup_call_via_r3_b, %function
tlsr8278_setup_call_via_r3_b:
	bx r3
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
	ldr r3, .Ltr_saved
	ldr r2, [r3, #8]
	cmp r2, #3
	beq 2f
	ldr r3, .Ltr_count
	ldr r3, [r3]
	subs r0, r0, r3
	ldr r3, .Ltr_ratio
	ldr r3, [r3]
	muls r0, r3
	lsrs r0, r0, #4
	ldr r3, .Ltr_tick
	ldr r3, [r3]
	adds r0, r0, r3
1:	bx lr
2:	ldr r2, [r3, #4]
	subs r0, r0, r2
	ldr r2, .Ltr_ratio
	ldr r2, [r2]
	muls r0, r2
	lsrs r0, r0, #4
	ldr r3, [r3]
	adds r0, r0, r3
	b 1b
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
	push {lr}
	movs r0, #0xc8
	movs r1, #0x80
	bl tlsr8278_setup_analog_write
	movs r0, #0x30
	bl tlsr8278_setup_analog_read
	movs r3, #0x80
	rsbs r3, r3, #0	@ r3 = 0xffffff80: bit 7 (the byte store drops the rest)
	orrs r3, r0
	lsls r1, r3, #24
	lsrs r1, r1, #24
	movs r0, #0x30
	bl tlsr8278_setup_analog_write
	movs r0, #0xc7
	movs r1, #0xe
	bl tlsr8278_setup_analog_write
	movs r0, #0xc7
	movs r1, #0xf
	bl tlsr8278_setup_analog_write
1:	movs r0, #0xcf
	bl tlsr8278_setup_analog_read
	lsls r3, r0, #24
	bpl 1b
	movs r0, #0xcb
	bl tlsr8278_setup_analog_read
	adds r1, r0, #0
	movs r0, #0x33
	bl tlsr8278_setup_analog_write
	movs r0, #0x30
	bl tlsr8278_setup_analog_read
	movs r1, #0x7f
	ands r1, r0
	movs r0, #0x30
	bl tlsr8278_setup_analog_write
	movs r0, #0xc7
	movs r1, #0xe
	bl tlsr8278_setup_analog_write
	movs r0, #0x33
	bl tlsr8278_setup_analog_read
	ldr r3, .Lr24_var
	strb r0, [r3]
	pop {pc}
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
	push {r4, lr}
	movs r0, #0x30
	movs r1, #0x60
	bl tlsr8278_setup_analog_write
	movs r0, #0xc6
	movs r1, #0xf6
	bl tlsr8278_setup_analog_write
	movs r0, #0xc6
	movs r1, #0xf7
	bl tlsr8278_setup_analog_write
	movs r4, #0x40
1:	movs r0, #0xcf
	bl tlsr8278_setup_analog_read
	tst r4, r0
	beq 1b
	movs r0, #0xc9
	bl tlsr8278_setup_analog_read
	adds r1, r0, #0
	movs r0, #0x32
	bl tlsr8278_setup_analog_write
	movs r0, #0xca
	bl tlsr8278_setup_analog_read
	adds r1, r0, #0
	movs r0, #0x31
	bl tlsr8278_setup_analog_write
	movs r0, #0xc6
	movs r1, #0xf6
	bl tlsr8278_setup_analog_write
	movs r0, #0x30
	movs r1, #0x20
	bl tlsr8278_setup_analog_write
	pop {r4, pc}
	.size tlsr8278_setup_seq_c6, . - tlsr8278_setup_seq_c6

/*
 * 0x91c flash_read_jedec_id -> r0: the flash's JEDEC ID (command 0x9f, no
 * address, no dummy bytes, 3 bytes read into a zeroed word on the stack):
 * manufacturer in bits 7:0, then memory type and capacity.
 */
	.global tlsr8278_setup_flash_read_jedec_id
	.type tlsr8278_setup_flash_read_jedec_id, %function
tlsr8278_setup_flash_read_jedec_id:
	push {lr}
	sub sp, #0xc
	movs r3, #0
	str r3, [sp, #8]	@ the ID word = 0
	add r3, sp, #8
	str r3, [sp]	@ buf = &ID word (fifth argument)
	movs r3, #3
	str r3, [sp, #4]	@ len = 3 (sixth argument)
	movs r0, #0x9f
	movs r1, #0
	movs r2, #0
	movs r3, #0
	bl tlsr8278_setup_flash_read_cmd
	ldr r0, [sp, #8]
	add sp, #0xc
	pop {pc}
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
	push {lr}
	bl tlsr8278_setup_flash_read_jedec_id
	ldr r2, .Lzb_14
	adds r3, r0, r2
	cmp r3, #0
	beq 2f
	ldr r2, .Lzb_13
	adds r3, r0, r2
	cmp r3, #0
	beq 2f
	ldr r3, .Lzb_12
	adds r2, r0, r3
	rsbs r3, r2, #0
	adcs r2, r3	@ r2 = (ID == 0x12325e)
	ldr r3, .Lzb_11
	adds r0, r0, r3
	rsbs r3, r0, #0
	adcs r3, r0	@ r3 = (ID == 0x11325e)
	adds r0, r2, #0
	orrs r0, r3
1:	pop {pc}
2:	movs r0, #1
	b 1b
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
	push {r4, r5, r6, r7, lr}
	mov r7, r8
	push {r7}
	ldr r6, .Lpk_clk
	movs r3, #0x43
	strb r3, [r6]	@ system clock 16 MHz
	ldr r5, .Lpk_pc_gpio
	movs r3, #0xf7
	strb r3, [r5]	@ 0x800596 = 0xf7
	ldr r4, .Lpk_pwm1
	ldr r3, .Lpk_cycle
	str r3, [r4]	@ PWM1: high time 1, cycle time 2
	ldr r7, .Lpk_pwm_en
	movs r3, #2
	strb r3, [r7]	@ PWM1 on
	ldr r2, .Lpk_pwm_clk
	mov r8, r2
	movs r3, #0xf3
	strb r3, [r2]	@ PWM clock: 244 system clocks
	ldr r0, .Lpk_10ms
	bl tlsr8278_setup_sleep_us
	movs r3, #6
	strb r3, [r6]	@ clock select back to 6
	movs r3, #0xff
	strb r3, [r5]	@ 0x800596 = 0xff
	movs r3, #0
	str r3, [r4]	@ 0x800798 = 0
	strb r3, [r7]	@ PWM off
	mov r2, r8
	strb r3, [r2]	@ PWM clock divider 0
	pop {r2}
	mov r8, r2
	pop {r4, r5, r6, r7, pc}
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
	ldr r3, .Ltw_key
	movs r2, #0x62
	strb r2, [r3]
	ldr r2, .Ltw_word
	ldr r0, [r2]
	movs r2, #0
	strb r2, [r3]
	bx lr
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
	push {r4, r5, lr}
	movs r0, #0x2d
	movs r1, #0x95
	bl tlsr8278_setup_analog_write
	movs r0, #5
	movs r1, #0
	bl tlsr8278_setup_analog_write
	movs r4, #0xa	@ ten tries
	b 2f
1:	subs r4, #1
	cmp r4, #0
	beq 3f
2:	bl tlsr8278_setup_pwm_pc3_10ms
	bl tlsr8278_setup_read_analog40
	adds r5, r0, #0
	ldr r0, .Lxs_wait
	bl tlsr8278_setup_sleep_us
	bl tlsr8278_setup_read_analog40
	cmp r5, r0
	beq 1b	@ equal: again
3:	pop {r4, r5, pc}
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
	push {lr}
	ldr r3, .Lwt_vars_10
	ldrb r3, [r3, #9]	@ tlsr8278_setup_vars + 0x19
	cmp r3, #0
	beq .Lwt_rc
	movs r0, #0x3b
	bl tlsr8278_setup_analog_read
	cmp r0, #0
	bne .Lwt_running
	movs r0, #0x3b
	bl tlsr8278_setup_analog_read
	movs r3, #1
	adds r1, r0, #0
	orrs r1, r3
	lsls r1, r1, #24
	lsrs r1, r1, #24
	movs r0, #0x3b
	bl tlsr8278_setup_analog_write	@ analog 0x3b bit 0 set
	bl tlsr8278_setup_pwm_pc3_until_moving
	bl tlsr8278_setup_read_word_4c
	movs r3, #0xff
	tst r3, r0
	bne .Lwt_vref
.Lwt_slot:
	ldr r2, .Lwt_slot_reg
	ldrh r3, [r2]
	cmp r3, #0
	bne .Lwt_second
.Lwt_first:
	ldr r3, .Lwt_boot_4
	ldr r2, [r3]
	ldr r3, .Lwt_boot_18
	str r2, [r3]	@ boot_vars + 0x18 = boot_vars + 4
.Lwt_flash:
	bl tlsr8278_setup_flash_is_zbit
	ldr r3, .Lwt_zb
	strb r0, [r3]
	pop {pc}
.Lwt_rc:
	bl tlsr8278_setup_seq_c6
.Lwt_word:
	bl tlsr8278_setup_read_word_4c
	movs r3, #0xff
	tst r3, r0
	beq .Lwt_slot	@ the low byte is 0
.Lwt_vref:
	ldr r3, .Lwt_vars
	ldrb r2, [r3, #2]
	ldrb r1, [r3, #3]
	lsls r1, r1, #8
	orrs r1, r2	@ the halfword at vars + 2
	beq .Lwt_slot
	lsls r0, r0, #24
	lsrs r0, r0, #8	@ the low byte << 16
	ldr r2, .Lwt_1047
	adds r0, r0, r2	@ (1047 + the byte) << 16
	lsrs r2, r0, #16
	strb r2, [r3]
	lsrs r0, r0, #24
	strb r0, [r3, #1]	@ vars + 0 = 1047 + the byte
	ldr r2, .Lwt_slot_reg
	ldrh r3, [r2]
	cmp r3, #0
	beq .Lwt_first
.Lwt_second:
	ldr r1, .Lwt_boot_0
	ldr r1, [r1]
	asrs r1, r1, #2
	lsls r1, r1, #3	@ boot_vars + 0, / 4 * 8
	movs r0, #7
	ands r3, r0
	orrs r3, r1
	lsls r3, r3, #16
	lsrs r3, r3, #16
	strh r3, [r2]	@ 0x80063e
	ldr r1, .Lwt_marker
	ldr r2, .Lwt_header
	strh r1, [r2]	@ 0x840014 = 0xa55a
	adds r2, #2
	strh r3, [r2]	@ 0x840016 = the same halfword
	movs r2, #0
	ldr r3, .Lwt_boot_18
	str r2, [r3]	@ boot_vars + 0x18 = 0
	b .Lwt_flash
.Lwt_running:
	ldr r0, .Lwt_6ms
	bl tlsr8278_setup_sleep_us
	b .Lwt_word
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
