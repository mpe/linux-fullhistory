/*
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/kallsyms.h>
#include <linux/profile.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/iSeries/LparData.h>
#include <asm/machdep.h>
#include <asm/paca.h>

#ifdef CONFIG_SMP
extern void iSeries_smp_message_recv( struct pt_regs * );
#endif

extern irq_desc_t irq_desc[NR_IRQS];
EXPORT_SYMBOL(irq_desc);

int distribute_irqs = 1;
int __irq_offset_value;
int ppc_spurious_interrupts;
unsigned long lpevent_count;
u64 ppc64_interrupt_controller;

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	irq_desc_t *desc;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for (j=0; j<NR_CPUS; j++) {
			if (cpu_online(j))
				seq_printf(p, "CPU%d       ",j);
		}
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		desc = get_irq_desc(i);
		spin_lock_irqsave(&desc->lock, flags);
		action = desc->action;
		if (!action || !action->handler)
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifdef CONFIG_SMP
		for (j = 0; j < NR_CPUS; j++) {
			if (cpu_online(j))
				seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
		}
#else
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif /* CONFIG_SMP */
		if (desc->handler)
			seq_printf(p, " %s ", desc->handler->typename );
		else
			seq_printf(p, "  None      ");
		seq_printf(p, "%s", (desc->status & IRQ_LEVEL) ? "Level " : "Edge  ");
		seq_printf(p, "    %s",action->name);
		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&desc->lock, flags);
	} else if (i == NR_IRQS)
		seq_printf(p, "BAD: %10u\n", ppc_spurious_interrupts);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
void fixup_irqs(cpumask_t map)
{
	unsigned int irq;
	static int warned;

	for_each_irq(irq) {
		cpumask_t mask;

		if (irq_desc[irq].status & IRQ_PER_CPU)
			continue;

		cpus_and(mask, irq_affinity[irq], map);
		if (any_online_cpu(mask) == NR_CPUS) {
			printk("Breaking affinity for irq %i\n", irq);
			mask = map;
		}
		if (irq_desc[irq].handler->set_affinity)
			irq_desc[irq].handler->set_affinity(irq, mask);
		else if (irq_desc[irq].action && !(warned++))
			printk("Cannot set affinity for irq %i\n", irq);
	}

	local_irq_enable();
	mdelay(1);
	local_irq_disable();
}
#endif

extern int noirqdebug;

/*
 * Eventually, this should take an array of interrupts and an array size
 * so it can dispatch multiple interrupts.
 */
void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq)
{
	int status;
	struct irqaction *action;
	int cpu = smp_processor_id();
	irq_desc_t *desc = get_irq_desc(irq);
	irqreturn_t action_ret;
#ifdef CONFIG_IRQSTACKS
	struct thread_info *curtp, *irqtp;
#endif

	kstat_cpu(cpu).irqs[irq]++;

	if (desc->status & IRQ_PER_CPU) {
		/* no locking required for CPU-local interrupts: */
		ack_irq(irq);
		action_ret = handle_IRQ_event(irq, regs, desc->action);
		desc->handler->end(irq);
		return;
	}

	spin_lock(&desc->lock);
	ack_irq(irq);	
	/*
	   REPLAY is when Linux resends an IRQ that was dropped earlier
	   WAITING is used by probe to mark irqs that are being tested
	   */
	status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
	status |= IRQ_PENDING; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (likely(!(status & (IRQ_DISABLED | IRQ_INPROGRESS)))) {
		action = desc->action;
		if (!action || !action->handler) {
			ppc_spurious_interrupts++;
			printk(KERN_DEBUG "Unhandled interrupt %x, disabled\n", irq);
			/* We can't call disable_irq here, it would deadlock */
			if (!desc->depth)
				desc->depth = 1;
			desc->status |= IRQ_DISABLED;
			/* This is not a real spurrious interrupt, we
			 * have to eoi it, so we jump to out
			 */
			mask_irq(irq);
			goto out;
		}
		status &= ~IRQ_PENDING; /* we commit to handling */
		status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	   Since we set PENDING, if another processor is handling
	   a different instance of this same irq, the other processor
	   will take care of it.
	 */
	if (unlikely(!action))
		goto out;

	/*
	 * Edge triggered interrupts need to remember
	 * pending events.
	 * This applies to any hw interrupts that allow a second
	 * instance of the same irq to arrive while we are in do_IRQ
	 * or in the handler. But the code here only handles the _second_
	 * instance of the irq, not the third or fourth. So it is mostly
	 * useful for irq hardware that does not mask cleanly in an
	 * SMP environment.
	 */
	for (;;) {
		spin_unlock(&desc->lock);

#ifdef CONFIG_IRQSTACKS
		/* Switch to the irq stack to handle this */
		curtp = current_thread_info();
		irqtp = hardirq_ctx[smp_processor_id()];
		if (curtp != irqtp) {
			irqtp->task = curtp->task;
			irqtp->flags = 0;
			action_ret = call_handle_IRQ_event(irq, regs, action, irqtp);
			irqtp->task = NULL;
			if (irqtp->flags)
				set_bits(irqtp->flags, &curtp->flags);
		} else
#endif
			action_ret = handle_IRQ_event(irq, regs, action);

		spin_lock(&desc->lock);
		if (!noirqdebug)
			note_interrupt(irq, desc, action_ret);
		if (likely(!(desc->status & IRQ_PENDING)))
			break;
		desc->status &= ~IRQ_PENDING;
	}
out:
	desc->status &= ~IRQ_INPROGRESS;
	/*
	 * The ->end() handler has to deal with interrupts which got
	 * disabled while the handler was running.
	 */
	if (desc->handler) {
		if (desc->handler->end)
			desc->handler->end(irq);
		else if (desc->handler->enable)
			desc->handler->enable(irq);
	}
	spin_unlock(&desc->lock);
}

#ifdef CONFIG_PPC_ISERIES
void do_IRQ(struct pt_regs *regs)
{
	struct paca_struct *lpaca;
	struct ItLpQueue *lpq;

	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 2KB free? */
	{
		long sp;

		sp = __get_SP() & (THREAD_SIZE-1);

		if (unlikely(sp < (sizeof(struct thread_info) + 2048))) {
			printk("do_IRQ: stack overflow: %ld\n",
				sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

	lpaca = get_paca();
#ifdef CONFIG_SMP
	if (lpaca->lppaca.int_dword.fields.ipi_cnt) {
		lpaca->lppaca.int_dword.fields.ipi_cnt = 0;
		iSeries_smp_message_recv(regs);
	}
#endif /* CONFIG_SMP */
	lpq = lpaca->lpqueue_ptr;
	if (lpq && ItLpQueue_isLpIntPending(lpq))
		lpevent_count += ItLpQueue_process(lpq, regs);

	irq_exit();

	if (lpaca->lppaca.int_dword.fields.decr_int) {
		lpaca->lppaca.int_dword.fields.decr_int = 0;
		/* Signal a fake decrementer interrupt */
		timer_interrupt(regs);
	}
}

#else	/* CONFIG_PPC_ISERIES */

void do_IRQ(struct pt_regs *regs)
{
	int irq;

	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 2KB free? */
	{
		long sp;

		sp = __get_SP() & (THREAD_SIZE-1);

		if (unlikely(sp < (sizeof(struct thread_info) + 2048))) {
			printk("do_IRQ: stack overflow: %ld\n",
				sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

	irq = ppc_md.get_irq(regs);

	if (irq >= 0)
		ppc_irq_dispatch_handler(regs, irq);
	else
		/* That's not SMP safe ... but who cares ? */
		ppc_spurious_interrupts++;

	irq_exit();
}
#endif	/* CONFIG_PPC_ISERIES */

void __init init_IRQ(void)
{
	static int once = 0;

	if (once)
		return;

	once++;

	ppc_md.init_IRQ();
	irq_ctx_init();
}

#ifndef CONFIG_PPC_ISERIES
/*
 * Virtual IRQ mapping code, used on systems with XICS interrupt controllers.
 */

#define UNDEFINED_IRQ 0xffffffff
unsigned int virt_irq_to_real_map[NR_IRQS];

/*
 * Don't use virtual irqs 0, 1, 2 for devices.
 * The pcnet32 driver considers interrupt numbers < 2 to be invalid,
 * and 2 is the XICS IPI interrupt.
 * We limit virtual irqs to 17 less than NR_IRQS so that when we
 * offset them by 16 (to reserve the first 16 for ISA interrupts)
 * we don't end up with an interrupt number >= NR_IRQS.
 */
#define MIN_VIRT_IRQ	3
#define MAX_VIRT_IRQ	(NR_IRQS - NUM_ISA_INTERRUPTS - 1)
#define NR_VIRT_IRQS	(MAX_VIRT_IRQ - MIN_VIRT_IRQ + 1)

void
virt_irq_init(void)
{
	int i;
	for (i = 0; i < NR_IRQS; i++)
		virt_irq_to_real_map[i] = UNDEFINED_IRQ;
}

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
int virt_irq_create_mapping(unsigned int real_irq)
{
	unsigned int virq, first_virq;
	static int warned;

	if (ppc64_interrupt_controller == IC_OPEN_PIC)
		return real_irq;	/* no mapping for openpic (for now) */

	/* don't map interrupts < MIN_VIRT_IRQ */
	if (real_irq < MIN_VIRT_IRQ) {
		virt_irq_to_real_map[real_irq] = real_irq;
		return real_irq;
	}

	/* map to a number between MIN_VIRT_IRQ and MAX_VIRT_IRQ */
	virq = real_irq;
	if (virq > MAX_VIRT_IRQ)
		virq = (virq % NR_VIRT_IRQS) + MIN_VIRT_IRQ;

	/* search for this number or a free slot */
	first_virq = virq;
	while (virt_irq_to_real_map[virq] != UNDEFINED_IRQ) {
		if (virt_irq_to_real_map[virq] == real_irq)
			return virq;
		if (++virq > MAX_VIRT_IRQ)
			virq = MIN_VIRT_IRQ;
		if (virq == first_virq)
			goto nospace;	/* oops, no free slots */
	}

	virt_irq_to_real_map[virq] = real_irq;
	return virq;

 nospace:
	if (!warned) {
		printk(KERN_CRIT "Interrupt table is full\n");
		printk(KERN_CRIT "Increase NR_IRQS (currently %d) "
		       "in your kernel sources and rebuild.\n", NR_IRQS);
		warned = 1;
	}
	return NO_IRQ;
}

/*
 * In most cases will get a hit on the very first slot checked in the
 * virt_irq_to_real_map.  Only when there are a large number of
 * IRQs will this be expensive.
 */
unsigned int real_irq_to_virt_slowpath(unsigned int real_irq)
{
	unsigned int virq;
	unsigned int first_virq;

	virq = real_irq;

	if (virq > MAX_VIRT_IRQ)
		virq = (virq % NR_VIRT_IRQS) + MIN_VIRT_IRQ;

	first_virq = virq;

	do {
		if (virt_irq_to_real_map[virq] == real_irq)
			return virq;

		virq++;

		if (virq >= MAX_VIRT_IRQ)
			virq = 0;

	} while (first_virq != virq);

	return NO_IRQ;

}

#endif /* CONFIG_PPC_ISERIES */

#ifdef CONFIG_IRQSTACKS
struct thread_info *softirq_ctx[NR_CPUS];
struct thread_info *hardirq_ctx[NR_CPUS];

void irq_ctx_init(void)
{
	struct thread_info *tp;
	int i;

	for_each_cpu(i) {
		memset((void *)softirq_ctx[i], 0, THREAD_SIZE);
		tp = softirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = SOFTIRQ_OFFSET;

		memset((void *)hardirq_ctx[i], 0, THREAD_SIZE);
		tp = hardirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = HARDIRQ_OFFSET;
	}
}

void do_softirq(void)
{
	unsigned long flags;
	struct thread_info *curtp, *irqtp;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	if (local_softirq_pending()) {
		curtp = current_thread_info();
		irqtp = softirq_ctx[smp_processor_id()];
		irqtp->task = curtp->task;
		call_do_softirq(irqtp);
		irqtp->task = NULL;
	}

	local_irq_restore(flags);
}
EXPORT_SYMBOL(do_softirq);

#endif /* CONFIG_IRQSTACKS */

static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
