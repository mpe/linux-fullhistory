#ifndef _ALPHA_HW_IRQ_H
#define _ALPHA_HW_IRQ_H
/*
 *	linux/arch/alpha/kernel/irq.h
 *
 *	Copyright (C) 1995 Linus Torvalds
 *	Copyright (C) 1998 Richard Henderson
 *
 * This file contains declarations and inline functions for interfacing
 * with the IRQ handling routines in irq.c.
 */

#include <linux/config.h>

#define STANDARD_INIT_IRQ_PROLOG	\
	outb(0, DMA1_RESET_REG);	\
	outb(0, DMA2_RESET_REG);	\
	outb(0, DMA1_CLR_MASK_REG);	\
	outb(0, DMA2_CLR_MASK_REG)

extern void common_ack_irq(unsigned long irq);
extern void isa_device_interrupt(unsigned long vector, struct pt_regs * regs);
extern void srm_device_interrupt(unsigned long vector, struct pt_regs * regs);

extern void handle_irq(int irq, struct pt_regs * regs);

#define RTC_IRQ    8
#if 0 /* on Alpha we want to use only the RTC as timer for SMP issues */
#ifdef CONFIG_RTC
#define TIMER_IRQ  0			 /* timer is the pit */
#else
#define TIMER_IRQ  RTC_IRQ		 /* timer is the rtc */
#endif
#else
#define TIMER_IRQ  RTC_IRQ		 /* timer is the rtc */
#endif

/*
 * PROBE_MASK is the bitset of irqs that we consider for autoprobing.
 */

/* NOTE: we only handle the first 64 IRQs in this code. */

/* The normal mask includes all the IRQs except timer IRQ 0.  */
#define _PROBE_MASK(nr_irqs)	\
	(((nr_irqs > 63) ? ~0UL : ((1UL << (nr_irqs & 63)) - 1)) & ~1UL)

/* Mask out unused timer irq 0 and RTC irq 8. */
#define P2K_PROBE_MASK		(_PROBE_MASK(16) & ~0x101UL)

/* Mask out unused timer irq 0, "irqs" 20-30, and the EISA cascade. */
#define ALCOR_PROBE_MASK	(_PROBE_MASK(48) & ~0xfff000000001UL)

/* Leave timer IRQ 0 in the mask.  */
#define RUFFIAN_PROBE_MASK	(_PROBE_MASK(48) | 1UL)

/* Do not probe/enable beyond the PCI devices. */
#define TSUNAMI_PROBE_MASK	_PROBE_MASK(48)

#if defined(CONFIG_ALPHA_GENERIC)
# define PROBE_MASK	alpha_mv.irq_probe_mask
#elif defined(CONFIG_ALPHA_P2K)
# define PROBE_MASK	P2K_PROBE_MASK
#elif defined(CONFIG_ALPHA_ALCOR) || defined(CONFIG_ALPHA_XLT)
# define PROBE_MASK	ALCOR_PROBE_MASK
#elif defined(CONFIG_ALPHA_RUFFIAN)
# define PROBE_MASK	RUFFIAN_PROBE_MASK
#elif defined(CONFIG_ALPHA_DP264)
# define PROBE_MASK	TSUNAMI_PROBE_MASK
#else
# define PROBE_MASK	_PROBE_MASK(NR_IRQS)
#endif


static inline void alpha_do_profile (unsigned long pc)
{
	if (prof_buffer && current->pid) {
		extern char _stext;

		pc -= (unsigned long) &_stext;
		pc >>= prof_shift;
		/*
		 * Don't ignore out-of-bounds PC values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
		if (pc > prof_len - 1)
			pc = prof_len - 1;
		atomic_inc((atomic_t *)&prof_buffer[pc]);
	}
}

static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}
extern void no_action(int cpl, void *dev_id, struct pt_regs *regs);
extern void init_ISA_irqs(void);
extern void init_RTC_irq(void);

#endif
