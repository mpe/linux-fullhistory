/*
 *  linux/arch/arm/kernel/irq.c
 *
 *  Copyright (C) 1992 Linus Torvalds
 *  Modifications for ARM processor Copyright (C) 1995-1998 Russell King.
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */
#include <linux/config.h> /* for CONFIG_DEBUG_ERRORS */
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>

#ifndef SMP
#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])
#else
#error SMP not supported
#endif

#ifndef cliIF
#define cliIF()
#endif

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
spinlock_t irq_controller_lock;

extern int get_fiq_list(char *);
extern void init_FIQ(void);

struct irqdesc {
	unsigned int	 nomask   : 1;		/* IRQ does not mask in IRQ   */
	unsigned int	 enabled  : 1;		/* IRQ is currently enabled   */
	unsigned int	 triggered: 1;		/* IRQ has occurred	      */
	unsigned int	 probing  : 1;		/* IRQ in use for a probe     */
	unsigned int	 probe_ok : 1;		/* IRQ can be used for probe  */
	unsigned int	 valid    : 1;		/* IRQ claimable	      */
	unsigned int	 unused   :26;
	void (*mask_ack)(unsigned int irq);	/* Mask and acknowledge IRQ   */
	void (*mask)(unsigned int irq);		/* Mask IRQ		      */
	void (*unmask)(unsigned int irq);	/* Unmask IRQ		      */
	struct irqaction *action;
	unsigned int	 unused2[3];
};

static struct irqdesc irq_desc[NR_IRQS];

/*
 * Dummy mask/unmask handler
 */
static void dummy_mask_unmask_irq(unsigned int irq)
{
}

void disable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	cliIF();
	irq_desc[irq].enabled = 0;
	irq_desc[irq].mask(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

void enable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	cliIF();
	irq_desc[irq].enabled = 1;
	irq_desc[irq].probing = 0;
	irq_desc[irq].triggered = 0;
	irq_desc[irq].unmask(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

int get_irq_list(char *buf)
{
	int i;
	struct irqaction * action;
	char *p = buf;

	for (i = 0 ; i < NR_IRQS ; i++) {
	    	action = irq_desc[i].action;
		if (!action)
			continue;
		p += sprintf(p, "%3d: %10u   %s",
			     i, kstat_irqs(i), action->name);
		for (action = action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}

#ifdef CONFIG_ACORN
	p += get_fiq_list(p);
#endif
	return p - buf;
}

/*
 * do_IRQ handles all normal device IRQ's
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqdesc * desc = irq_desc + irq;
	struct irqaction * action;
	int status, cpu;

	spin_lock(&irq_controller_lock);
	desc->mask_ack(irq);
	spin_unlock(&irq_controller_lock);

	cpu = smp_processor_id();
	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq]++;
	desc->triggered = 1;

	/* Return with this interrupt masked if no action */
	status = 0;
	action = desc->action;

	if (action) {
		if (desc->nomask) {
			spin_lock(&irq_controller_lock);
			desc->unmask(irq);
			spin_unlock(&irq_controller_lock);
		}

		if (!(action->flags & SA_INTERRUPT))
			__sti();

		do {
			status |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);

		if (status & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();

		if (!desc->nomask && desc->enabled) {
			spin_lock(&irq_controller_lock);
			desc->unmask(irq);
			spin_unlock(&irq_controller_lock);
		}
	}

	irq_exit(cpu, irq);

	/*
	 * This should be conditional: we should really get
	 * a return code from the irq handler to tell us
	 * whether the handler wants us to do software bottom
	 * half handling or not..
	 *
	 * ** IMPORTANT NOTE: do_bottom_half() ENABLES IRQS!!! **
	 * **  WE MUST DISABLE THEM AGAIN, ELSE IDE DISKS GO   **
	 * **                       AWOL                       **
	 */
	if (1) {
		if (bh_active & bh_mask)
			do_bottom_half();
		__cli();
	}
}

#if defined(CONFIG_ARCH_ACORN)
void do_ecard_IRQ(int irq, struct pt_regs *regs)
{
	struct irqdesc * desc;
	struct irqaction * action;
	int cpu;

	desc = irq_desc + irq;

	cpu = smp_processor_id();
	kstat.irqs[cpu][irq]++;
	desc->triggered = 1;

	action = desc->action;

	if (action) {
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	} else {
		spin_lock(&irq_controller_lock);
		desc->mask(irq);
		spin_unlock(&irq_controller_lock);
	}
}
#endif

int setup_arm_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	if (new->flags & SA_SAMPLE_RANDOM)
	        rand_initialize_irq(irq);

	spin_lock_irqsave(&irq_controller_lock, flags);

	p = &irq_desc[irq].action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&irq_controller_lock, flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		irq_desc[irq].nomask = (new->flags & SA_IRQNOMASK) ? 1 : 0;
		irq_desc[irq].enabled = 1;
		irq_desc[irq].probing = 0;
		irq_desc[irq].unmask(irq);
	}

	spin_unlock_irqrestore(&irq_controller_lock, flags);
	return 0;
}

/*
 * Using "struct sigaction" is slightly silly, but there
 * are historical reasons and it works well, so..
 */
int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		 unsigned long irq_flags, const char * devname, void *dev_id)
{
	unsigned long retval;
	struct irqaction *action;

	if (!irq_desc[irq].valid)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irq_flags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_arm_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (!irq_desc[irq].valid) {
		printk(KERN_ERR "Trying to free IRQ%d\n",irq);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
		return;
	}
	for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

	    	/* Found it - now free it */
		save_flags_cli (flags);
		*p = action->next;
		restore_flags (flags);
		kfree(action);
		return;
	}
	printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
#ifdef CONFIG_DEBUG_ERRORS
	__backtrace();
#endif
}

/* Start the interrupt probing.  Unlike other architectures,
 * we don't return a mask of interrupts from probe_irq_on,
 * but return the number of interrupts enabled for the probe.
 * The interrupts which have been enabled for probing is
 * instead recorded in the irq_desc structure.
 */
unsigned long probe_irq_on(void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/*
	 * first snaffle up any unassigned but
	 * probe-able interrupts
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < NR_IRQS; i++) {
		if (!irq_desc[i].valid ||
		    !irq_desc[i].probe_ok ||
		    irq_desc[i].action)
			continue;

		irq_desc[i].probing = 1;
		irq_desc[i].enabled = 1;
		irq_desc[i].triggered = 0;
		irq_desc[i].unmask(i);
		irqs += 1;
	}
	spin_unlock_irq(&irq_controller_lock);

	/*
	 * wait for spurious interrupts to mask themselves out again
	 */
	for (delay = jiffies + HZ/10; time_before(jiffies, delay); )
		/* min 100ms delay */;

	/*
	 * now filter out any obviously spurious interrupts
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_desc[i].probing && irq_desc[i].triggered) {
			irq_desc[i].probing = 0;
			irqs -= 1;
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	/* now filter out any obviously spurious interrupts */
	return irqs;
}

/*
 * Possible return values:
 *  >= 0 - interrupt number
 *    -1 - no interrupt/many interrupts
 */
int probe_irq_off(unsigned long irqs)
{
	unsigned int i;
	int irq_found = -1;

	/*
	 * look at the interrupts, and find exactly one
	 * that we were probing has been triggered
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_desc[i].probing &&
		    irq_desc[i].triggered) {
			if (irq_found != -1) {
				irq_found = NO_IRQ;
				goto out;
			}
			irq_found = i;
		}
	}

	if (irq_found == -1)
		irq_found = NO_IRQ;
out:
	spin_unlock_irq(&irq_controller_lock);
	return irq_found;
}

/*
 * Get architecture specific interrupt handlers
 * and interrupt initialisation.
 */
#include <asm/arch/irq.h>

__initfunc(void init_IRQ(void))
{
	extern void init_dma(void);
	int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].mask_ack = dummy_mask_unmask_irq;
		irq_desc[irq].mask     = dummy_mask_unmask_irq;
		irq_desc[irq].unmask   = dummy_mask_unmask_irq;
	}

	irq_init_irq();
#ifdef CONFIG_ARCH_ACORN
	init_FIQ();
#endif
	init_dma();
}
