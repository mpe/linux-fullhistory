/*  $Id: sun4d_irq.c,v 1.3 1997/12/22 16:09:15 jj Exp $
 *  arch/sparc/kernel/sun4d_irq.c: 
 *			SS1000/SC2000 interrupt handling.
 *
 *  Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Heavily based on arch/sparc/kernel/irq.c.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/spinlock.h>
#include <asm/sbus.h>
#include <asm/sbi.h>

struct sun4d_timer_regs *sun4d_timers;
#define TIMER_IRQ	10

#define MAX_STATIC_ALLOC	4
extern struct irqaction static_irqaction[MAX_STATIC_ALLOC];
extern int static_irq_count;

extern struct irqaction *irq_action[];

struct sbus_action {
	struct irqaction *action;
	unsigned char	lock;
	unsigned char	active;
	unsigned char	disabled;
} *sbus_actions;

static int pil_to_sbus[] = {
	0, 0, 1, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 0,
};

static int nsbi;

int sun4d_get_irq_list(char *buf)
{
	int i, j = 0, k = 0, len = 0, sbusl;
	struct irqaction * action;

	for (i = 0 ; i < NR_IRQS ; i++) {
		sbusl = pil_to_sbus[i];
		if (!sbusl) {
	 		action = *(i + irq_action);
			if (!action) 
		        	continue;
		} else {
			for (j = 0; j < nsbi; j++) {
				for (k = 0; k < 4; k++)
					if ((action = sbus_actions [(j << 5) + (sbusl << 2) + k].action))
						goto found_it;
			}
			continue;
		}
found_it:	len += sprintf(buf+len, "%2d: %8d %c %s",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		action = action->next;
		for (;;) {
			for (; action; action = action->next) {
				len += sprintf(buf+len, ",%s %s",
					(action->flags & SA_INTERRUPT) ? " +" : "",
					action->name);
			}
			if (!sbusl) break;
			k++;
			if (k < 4)
				action = sbus_actions [(j << 5) + (sbusl << 2) + k].action;
			else {
				j++;
				if (j == nsbi) break;
				k = 0;
				action = sbus_actions [(j << 5) + (sbusl << 2)].action;
			}
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

void sun4d_free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action, **actionp;
	struct irqaction *tmp = NULL;
        unsigned long flags;
	
	if (irq < 15)
		actionp = irq + irq_action;
	else
		actionp = &(sbus_actions[irq - (1 << 5)].action);
	action = *actionp;
	if (!action) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	if (dev_id) {
		for (; action; action = action->next) {
			if (action->dev_id == dev_id)
				break;
			tmp = action;
		}
		if (!action) {
			printk("Trying to free free shared IRQ%d\n",irq);
			return;
		}
	} else if (action->flags & SA_SHIRQ) {
		printk("Trying to free shared IRQ%d with NULL device ID\n", irq);
		return;
	}
	if (action->flags & SA_STATIC_ALLOC)
	{
	    /* This interrupt is marked as specially allocated
	     * so it is a bad idea to free it.
	     */
	    printk("Attempt to free statically allocated IRQ%d (%s)\n",
		   irq, action->name);
	    return;
	}
	
        save_and_cli(flags);
	if (action && tmp)
		tmp->next = action->next;
	else
		*actionp = action->next;

	kfree_s(action, sizeof(struct irqaction));

	if (!(*actionp))
		disable_irq(irq);

	restore_flags(flags);
}

extern void unexpected_irq(int, void *, struct pt_regs *);

void sun4d_handler_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int cpu = smp_processor_id();
	/* SBUS IRQ level (1 - 7) */
	int sbusl = pil_to_sbus[irq];
	
	/* FIXME: Is this necessary?? */
	cc_get_ipen();
	
	cc_set_iclr(1 << irq);
	
	irq_enter(cpu, irq, regs);
	kstat.interrupts[irq]++;
	if (!sbusl) {
		action = *(irq + irq_action);
		if (!action)
			unexpected_irq(irq, 0, regs);
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	} else {
		int bus_mask = bw_get_intr_mask(sbusl) & 0x3ffff;
		int lock;
		int sbino;
		struct sbus_action *actionp;
		unsigned mask, slot;
		int sbil = (sbusl << 2);
		
		bw_clear_intr_mask(sbusl, bus_mask);
		
		/* Loop for each pending SBI */
		for (sbino = 0; bus_mask; sbino++, bus_mask >>= 1)
			if (bus_mask & 1) {
				mask = acquire_sbi(SBI2DEVID(sbino), 0xf << sbil);
				mask &= (0xf << sbil);
				actionp = sbus_actions + (sbino << 5) + (sbil);
				/* Loop for each pending SBI slot */
				for (slot = (1 << sbil); mask; slot <<= 1, actionp++)
					if (mask & slot) {
						mask &= ~slot;
						action = actionp->action;
						__asm__ __volatile__ ("ldstub	[%1 + 4], %0"
						: "=r" (lock) : "r" (actionp));
						
						if (!lock) {
							if (!action)
								unexpected_irq(irq, 0, regs);
							do {
								action->handler(irq, action->dev_id, regs);
								action = action->next;
							} while (action);
							actionp->lock = 0;
						} else
							actionp->active = 1;
						release_sbi(SBI2DEVID(sbino), slot);
					}
			}
	}
	irq_exit(cpu, irq);
}

int sun4d_request_irq(unsigned int irq,
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *action, *tmp = NULL, **actionp;
	unsigned long flags;
	int sbusl;
	unsigned int *ret = NULL;
	struct linux_sbus_device *sdev = NULL;
	
	if(irq > 14)
		return -EINVAL;

	if (!handler)
	    return -EINVAL;
	    
	if (irqflags & SA_DCOOKIE) {
		struct devid_cookie *d = (struct devid_cookie *)dev_id;
		
		dev_id = d->real_dev_id;
		sdev = (struct linux_sbus_device *)d->bus_cookie;
		ret = &d->ret_ino;
	}
	
	sbusl = pil_to_sbus[irq];
	if (sbusl && !sdev) {
		printk ("Attempt to register SBUS IRQ %d without DCOOKIE\n", irq);
		return -EINVAL;
	}
	if (sbusl) {
		actionp = &(sbus_actions[(sdev->my_bus->board << 5) + 
				(sbusl << 2) + sdev->slot].action);
		*ret = ((sdev->my_bus->board + 1) << 5) +
				(sbusl << 2) + sdev->slot;
	} else
		actionp = irq + irq_action;
	action = *actionp;
	
	if (action) {
		if ((action->flags & SA_SHIRQ) && (irqflags & SA_SHIRQ)) {
			for (tmp = action; tmp->next; tmp = tmp->next);
		} else {
			return -EBUSY;
		}
		if ((action->flags & SA_INTERRUPT) ^ (irqflags & SA_INTERRUPT)) {
			printk("Attempt to mix fast and slow interrupts on IRQ%d denied\n", irq);
			return -EBUSY;
		}   
		action = NULL;		/* Or else! */
	}

	save_and_cli(flags);

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC)
	    if (static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n",irq, devname);
	
	if (action == NULL)
	    action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						 GFP_KERNEL);
	
	if (!action) { 
		restore_flags(flags);
		return -ENOMEM;
	}

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	if (tmp)
		tmp->next = action;
	else
		*actionp = action;
		
	if (ret) irq = *ret;

	if (irq > NR_IRQS) {
		struct sbus_action *s = sbus_actions + irq - (1 << 5);
	
		if (s->disabled) {
			s->disabled = 0;
			s->active = 0;
			s->lock = 0;
		}
	}
	
	restore_flags(flags);
	return 0;
}

static void sun4d_disable_irq(unsigned int irq)
{
	struct sbus_action *s;
	
	if (irq < NR_IRQS) {
		/* FIXME */
		printk ("Unable to disable IRQ %d\n", irq);
		return;
	}
	s = sbus_actions + irq - (1 << 5);
	
	if (s->disabled) return;
	s->disabled = 1;
	__asm__ __volatile__ ("
1:		ldstub	[%0 + 4], %%g1
		orcc	%%g1, 0, %%g0
		bne	1b"
		: : "r" (s) : "g1", "cc");
}

static void sun4d_enable_irq(unsigned int irq)
{
	struct sbus_action *s;
	struct irqaction *action;
	
	if (irq < NR_IRQS)
		/* FIXME */
		return;
	s = sbus_actions + irq - (1 << 5);
	
	if (!s->disabled) return;
	action = s->action;
	s->disabled = 0;
	while (s->active) {
		s->active = 0;
		while (action) {
			/* FIXME: Hope no sbus intr handler uses regs */
			action->handler(irq, action->dev_id, NULL);
			action = action->next;
		}
	}
	s->lock = 0;
}

#ifdef __SMP__

/* +-------+-------------+-----------+------------------------------------+
 * | bcast |  devid      |   sid     |              levels mask           |
 * +-------+-------------+-----------+------------------------------------+
 *  31      30         23 22       15 14                                 0
 */
#define IGEN_MESSAGE(bcast, devid, sid, levels) \
	(((bcast) << 31) | ((devid) << 23) | ((sid) << 15) | (levels))

static void sun4d_send_ipi(int cpu, int level)
{
	cc_set_igen(IGEN_MESSAGE(0, cpu << 3, 6 + ((level >> 1) & 7), 1 << (level - 1)));
}

static void sun4d_clear_ipi(int cpu, int level)
{
}

static void sun4d_set_udt(int cpu)
{
}
#endif
 
static void sun4d_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4d_timers->l10_timer_limit;
}

static void sun4d_clear_profile_irq(int cpu)
{
	bw_get_prof_limit(cpu);
}

static void sun4d_load_profile_irq(int cpu, unsigned int limit)
{
	bw_set_prof_limit(cpu, limit);
}

__initfunc(static void sun4d_init_timers(void (*counter_fn)(int, void *, struct pt_regs *)))
{
	int irq;
	extern struct prom_cpuinfo linux_cpus[NCPUS];
	int cpu;

	/* Map the User Timer registers. */
	sun4d_timers = sparc_alloc_io(BW_LOCAL_BASE+BW_TIMER_LIMIT, 0,
				      PAGE_SIZE, "user timer", 0xf, 0x0);
    
	sun4d_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4d_timers->l10_cur_count;
	master_l10_limit = &sun4d_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
	
	/* Enable user timer free run for CPU 0 in BW */
	/* bw_set_ctrl(0, bw_get_ctrl(0) | BW_CTRL_USER_TIMER); */
    
	for(cpu = 0; cpu < NCPUS; cpu++)
		sun4d_load_profile_irq(linux_cpus[cpu].mid, 0);
}

__initfunc(unsigned long sun4d_init_sbi_irq(unsigned long memory_start))
{
	struct linux_sbus *sbus;
	struct sbus_action *s;
	int i;
	unsigned mask;

	nsbi = 0;
	for_each_sbus(sbus)
		nsbi++;
	memory_start = ((memory_start + 7) & ~7);
	sbus_actions = (struct sbus_action *)memory_start;
	memory_start += (nsbi * 8 * 4 * sizeof(struct sbus_action));
	memset (sbus_actions, 0, (nsbi * 8 * 4 * sizeof(struct sbus_action)));
	for (i = 0, s = sbus_actions; i < nsbi * 8 * 4; i++, s++) {
		s->lock = 0xff;
		s->disabled = 1;
	}
	for_each_sbus(sbus) {
		/* Get rid of pending irqs from PROM */
		mask = acquire_sbi(sbus->devid, 0xffffffff);
		if (mask) {
			printk ("Clearing pending IRQs %08x on SBI %d\n", mask, sbus->board);
			release_sbi(sbus->devid, mask);
		}
	}
	return memory_start;
}

__initfunc(void sun4d_init_IRQ(void))
{
	__cli();

	enable_irq = sun4d_enable_irq;
	disable_irq = sun4d_disable_irq;
	clear_clock_irq = sun4d_clear_clock_irq;
	clear_profile_irq = sun4d_clear_profile_irq;
	load_profile_irq = sun4d_load_profile_irq;
	init_timers = sun4d_init_timers;
#ifdef __SMP__
	set_cpu_int = (void (*) (int, int))sun4d_send_ipi;
	clear_cpu_int = (void (*) (int, int))sun4d_clear_ipi;
	set_irq_udt = (void (*) (int))sun4d_set_udt;
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}
