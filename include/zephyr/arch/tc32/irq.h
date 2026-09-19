/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_

#include <zephyr/sys/util.h>

#ifndef _ASMLANGUAGE

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/irq.h>
#include <zephyr/sw_isr_table.h>
#include <tlsr825x/irq.h>

#define TC32_NUM_IRQS TLSR8258_NUM_IRQS

/*
 * IRQ-lock owner tracking.  Each arch_irq_lock() that actually transitions
 * the chip-level reg_irq_en from 1 to 0 records its return address in the
 * ring buffer; arch_irq_unlock() that transitions back from 0 to 1 clears
 * the corresponding slot.  If the device ever wedges with irq_en=0 forever,
 * the slots with non-zero values name the function PCs whose locks did not
 * see a matching unlock.  Read via SWS at z_tc32_irq_lock_owner[...] and
 * resolve to symbols with `addr2line` against zephyr.elf.
 *
 *   depth — current net lock depth (counts only locks that flipped 1->0)
 *   owner — ring buffer of recent unbalanced locker return addresses
 *   max   — high-water mark on depth (any value > expected nesting is
 *           a smoking gun on its own)
 *
 * The ring is intentionally tiny so the hot-path overhead is two MMIO
 * writes + four scalar stores per lock/unlock pair.  All fields live in
 * .data so SWS can read them while CPU is halted.
 */
#define Z_TC32_IRQ_LOCK_OWNER_DEPTH 8u
extern volatile uint32_t z_tc32_irq_count;
extern volatile uintptr_t z_tc32_irq_lock_owner[Z_TC32_IRQ_LOCK_OWNER_DEPTH];
extern volatile uint32_t z_tc32_irq_lock_depth;
extern volatile uint32_t z_tc32_irq_lock_max_depth;
extern volatile uintptr_t z_tc32_irq_lock_overflow_ra;

static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	uint8_t key = *TLSR8258_REG_IRQ_EN;

	*TLSR8258_REG_IRQ_EN = 0;
	if ((key & 1u) != 0u) {
		uintptr_t ra = (uintptr_t)__builtin_return_address(0);
		uint32_t slot = z_tc32_irq_lock_depth;

		if (slot < Z_TC32_IRQ_LOCK_OWNER_DEPTH) {
			z_tc32_irq_lock_owner[slot] = ra;
		} else {
			z_tc32_irq_lock_overflow_ra = ra;
		}
		z_tc32_irq_lock_depth = slot + 1u;
		if (z_tc32_irq_lock_depth > z_tc32_irq_lock_max_depth) {
			z_tc32_irq_lock_max_depth = z_tc32_irq_lock_depth;
		}
	}
	return key;
}

static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
	if ((key & 1u) != 0u && z_tc32_irq_lock_depth > 0u) {
		uint32_t slot = z_tc32_irq_lock_depth - 1u;

		z_tc32_irq_lock_depth = slot;
		if (slot < Z_TC32_IRQ_LOCK_OWNER_DEPTH) {
			z_tc32_irq_lock_owner[slot] = 0u;
		}
	}
	*TLSR8258_REG_IRQ_EN = (uint8_t)key;
}

static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	return (key & 1U) != 0U;
}

static ALWAYS_INLINE bool arch_cpu_irqs_are_enabled(void)
{
	return (*TLSR8258_REG_IRQ_EN & 1u) != 0u;
}

#define arch_irq_enable(irq) z_tc32_irq_enable(irq)
#define arch_irq_disable(irq) z_tc32_irq_disable(irq)
#define arch_irq_is_enabled(irq) z_tc32_irq_is_enabled(irq)

static ALWAYS_INLINE void z_tc32_irq_enable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();
	uint32_t bit = tlsr8258_irq_bit(irq);

	if ((bit & TLSR8258_IRQ_VALID_MASK) != 0u) {
		tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() | bit);
	}
	arch_irq_unlock(key);
}

static ALWAYS_INLINE void z_tc32_irq_disable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();
	uint32_t bit = tlsr8258_irq_bit(irq);

	if (bit != 0u) {
		tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() & ~bit);
	}
	arch_irq_unlock(key);
}

static ALWAYS_INLINE int z_tc32_irq_is_enabled(unsigned int irq)
{
	uint32_t bit = tlsr8258_irq_bit(irq);

	return (bit & TLSR8258_IRQ_VALID_MASK & tlsr8258_irq_mask_read()) != 0u;
}

#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
	{ Z_ISR_DECLARE(irq_p, 0, isr_p, isr_param_p); }

#define ARCH_IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p) \
	ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, NULL, flags_p)

#define ARCH_ISR_DIRECT_HEADER() do { } while (false)
#define ARCH_ISR_DIRECT_FOOTER(swap) do { ARG_UNUSED(swap); } while (false)
#define ARCH_ISR_DIRECT_DECLARE(name) \
	static inline int name##_body(void); \
	int name(void) \
	{ \
		return name##_body(); \
	} \
	static inline int name##_body(void)

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_ */
