/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/tc32/arch.h>
#include <zephyr/logging/log.h>
#include <zephyr/linker/section_tags.h>
#include <ksched.h>
#include <kswap.h>
#include <tlsr825x/irq.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

#define TC32_IRQ_MAX_DRAIN (TLSR8258_NUM_IRQS * 2u)

/*
 * Debug state for hardware IRQ bring-up. Keep this in normal RAM so it can be
 * inspected over SWS after trapping in z_irq_spurious()/fatal paths.
 */
volatile uint32_t __noinit z_tc32_irq_debug_src;
volatile uint32_t __noinit z_tc32_irq_debug_mask;
volatile uint32_t __noinit z_tc32_irq_debug_pending;
volatile uint32_t __noinit z_tc32_irq_debug_irq;
volatile uint32_t __noinit z_tc32_irq_debug_drained;

/*
 * Owner tracking for chip-level reg_irq_en (0x800643) — populated by the
 * inline arch_irq_lock()/arch_irq_unlock() in <zephyr/arch/tc32/irq.h>.
 * Placed in .data (initialised to zero) so SWS can read consistently even
 * after a reset loop.
 */
/*
 * Bumped once per dispatcher entry so arch_cpu_idle() can tell that an
 * interrupt has actually been taken.
 */
volatile uint32_t z_tc32_irq_count;

/*
 * Set by z_tc32_handle_irqs() when it decides to switch, read by the exit
 * path in reset.S: the thread whose interrupt frame has to be relocated.
 */
struct k_thread *z_tc32_switch_from;

volatile uintptr_t z_tc32_irq_lock_owner[Z_TC32_IRQ_LOCK_OWNER_DEPTH];
volatile uint32_t z_tc32_irq_lock_depth;
volatile uint32_t z_tc32_irq_lock_max_depth;
volatile uintptr_t z_tc32_irq_lock_overflow_ra;

FUNC_NORETURN void z_irq_spurious(const void *unused)
{
	ARG_UNUSED(unused);

	LOG_ERR("Spurious interrupt detected");
	z_tc32_fatal_error(K_ERR_SPURIOUS_IRQ, NULL);
}

static ALWAYS_INLINE void enter_irq(unsigned int irq)
{
	const struct _isr_table_entry *ite = &_sw_isr_table[irq];

	if (IS_ENABLED(CONFIG_TRACING_ISR)) {
		sys_trace_isr_enter();
	}

	((void (*)(const void *))ite->isr)(ite->arg);

	if (IS_ENABLED(CONFIG_TRACING_ISR)) {
		sys_trace_isr_exit();
	}
}

static ALWAYS_INLINE bool irq_is_valid(unsigned int irq)
{
	return tlsr8258_irq_is_valid(irq);
}

static ALWAYS_INLINE bool irq_clear_is_arch_owned(unsigned int irq)
{
	return (tlsr8258_irq_bit(irq) & TLSR8258_IRQ_TIMER_MASK) != 0u;
}

static ALWAYS_INLINE void irq_clear_arch_owned(unsigned int irq)
{
	/* Vendor order for TMR0..TMR2: clear IRQSRC first, then TMR_STATUS. */
	tlsr8258_irq_clear_parent(irq);
	*TLSR8258_REG_TMR_STA = BIT(irq);
}

static ALWAYS_INLINE unsigned int pending_lsb_index(uint32_t pending)
{
	unsigned int irq = 0u;

	while ((pending & BIT(irq)) == 0u) {
		irq++;
	}

	return irq;
}

void *TC32_BOOT_RAM_MIRROR_CODE z_tc32_handle_irqs(void)
{
	uint32_t pending;
	unsigned int drained = 0u;

	/*
	 * First-stage TC32 port policy: process all currently pending sources
	 * with global IRQ still disabled. Hardware nested IRQ entry is not
	 * enabled here; arch_is_in_isr() observes this counter for the whole
	 * pending-drain loop.
	 *
	 * RF-first policy: if the 802.15.4 RF IRQ (ZB_RT, bit 13) is pending
	 * at any point, service it before any other source and then EXIT the
	 * drain loop. Other pending sources (system tick, UART, GPIO) remain
	 * latched and re-trigger on the next IRQ entry; that's free because
	 * the global enable runs straight after this function returns. This
	 * collapses the worst-case RF-to-ACK latency tail: previously a system
	 * tick latched alongside an RF RX would run AFTER the RF handler but
	 * BEFORE the next pending-mask read, delaying the next back-to-back RF
	 * event by the tick handler's runtime (timeout list walk, possible
	 * thread wakeup — measured 20-100us). Now RF events drain back-to-back
	 * with no inline tick service.
	 */
	_kernel.cpus[0].nested++;
	z_tc32_irq_count++;

	while ((pending = (*TLSR8258_REG_IRQ_SRC & tlsr8258_irq_mask_read() &
			  TLSR8258_IRQ_VALID_MASK)) != 0u) {
		unsigned int irq;
		bool rf_pending = (pending & BIT(TLSR8258_IRQ_ZB_RT)) != 0u;

		if (rf_pending) {
			irq = TLSR8258_IRQ_ZB_RT;
		} else {
			irq = pending_lsb_index(pending);
		}

		if (!irq_is_valid(irq)) {
			break;
		}

		if (drained++ >= TC32_IRQ_MAX_DRAIN) {
			_kernel.cpus[0].nested--;
			z_tc32_fatal_error(K_ERR_SPURIOUS_IRQ, NULL);
		}

		if (irq_clear_is_arch_owned(irq)) {
			irq_clear_arch_owned(irq);
		}

		enter_irq(irq);

		if (rf_pending) {
			/* Let any other latched source re-trigger on the next
			 * IRQ entry instead of running inline. Keeps RF tail
			 * latency deterministic across back-to-back RX. */
			break;
		}
	}

	_kernel.cpus[0].nested--;

	if (IS_ENABLED(CONFIG_STACK_SENTINEL)) {
		z_check_stack_sentinel();
	}

	if (!IS_ENABLED(CONFIG_MULTITHREADING) || _kernel.cpus[0].nested != 0U) {
		return NULL;
	}

	/*
	 * Handlers routinely ready a higher priority thread, and
	 * k_thread_abort() on the running thread from an ISR only marks it
	 * dead and leaves the arch to stop running it. Ask the scheduler who
	 * should run; the exit path in reset.S does the stack surgery, since
	 * the interrupt frame sits on the banked IRQ stack and has to move to
	 * the outgoing thread before anyone else can use that stack.
	 */
	struct k_thread *interrupted = _current;
	void *next = z_get_next_switch_handle(interrupted);

	if (next == (void *)interrupted) {
		return NULL;
	}

	z_tc32_switch_from = interrupted;

	return next;
}

#ifdef CONFIG_DYNAMIC_INTERRUPTS
int arch_irq_connect_dynamic(unsigned int irq, unsigned int priority,
		     void (*routine)(const void *parameter),
		     const void *parameter, uint32_t flags)
{
	ARG_UNUSED(priority);
	ARG_UNUSED(flags);

	z_isr_install(irq, routine, parameter);
	return irq;
}
#endif /* CONFIG_DYNAMIC_INTERRUPTS */
