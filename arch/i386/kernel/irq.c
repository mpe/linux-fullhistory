/*
 *	linux/arch/i386/kernel/irq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
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

#include <linux/config.h>
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

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>
#include <asm/pgtable.h>

#include "irq.h"

#ifdef __SMP_PROF__
extern volatile unsigned long smp_local_timer_ticks[1+NR_CPUS];
#endif

unsigned int local_irq_count[NR_CPUS];
#ifdef __SMP__
atomic_t __intel_bh_counter;
#else
int __intel_bh_counter;
#endif

#ifdef __SMP_PROF__
static unsigned int int_count[NR_CPUS][NR_IRQS] = {{0},};
#endif

atomic_t nmi_counter;

/*
 * This contains the irq mask for both irq controllers
 */
static unsigned int cached_irq_mask = 0xffff;

#define cached_21	(((char *)(&cached_irq_mask))[0])
#define cached_A1	(((char *)(&cached_irq_mask))[1])

spinlock_t irq_controller_lock;

/*
 * This is always called from an interrupt context
 * with local interrupts disabled. Don't worry about
 * irq-safe locks.
 *
 * Note that we always ack the primary irq controller,
 * even if the interrupt came from the secondary, as
 * the primary will still have routed it. Oh, the joys
 * of PC hardware.
 */
static inline void mask_and_ack_irq(int irq_nr)
{
	spin_lock(&irq_controller_lock);
	cached_irq_mask |= 1 << irq_nr;
	if (irq_nr & 8) {
		inb(0xA1);	/* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x62,0x20);	/* Specific EOI to cascade */
		outb(0x20,0xA0);
	} else {
		inb(0x21);	/* DUMMY */
		outb(cached_21,0x21);
		outb(0x20,0x20);
	}
	spin_unlock(&irq_controller_lock);
}

static inline void set_irq_mask(int irq_nr)
{
	if (irq_nr & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

/*
 * These have to be protected by the spinlock
 * before being called.
 */
static inline void mask_irq(unsigned int irq_nr)
{
	cached_irq_mask |= 1 << irq_nr;
	set_irq_mask(irq_nr);
}

static inline void unmask_irq(unsigned int irq_nr)
{
	cached_irq_mask &= ~(1 << irq_nr);
	set_irq_mask(irq_nr);
}

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	mask_irq(irq_nr);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
	synchronize_irq();
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	unmask_irq(irq_nr);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that do all
 * the operations that are needed to keep the AT interrupt-controller
 * happy. They are also written to be fast - and to disable interrupts
 * as little as humanly possible.
 */

#if NR_IRQS != 16
#error make irq stub building NR_IRQS dependent and remove me.
#endif

BUILD_COMMON_IRQ()
BUILD_IRQ(FIRST,0,0x01)
BUILD_IRQ(FIRST,1,0x02)
BUILD_IRQ(FIRST,2,0x04)
BUILD_IRQ(FIRST,3,0x08)
BUILD_IRQ(FIRST,4,0x10)
BUILD_IRQ(FIRST,5,0x20)
BUILD_IRQ(FIRST,6,0x40)
BUILD_IRQ(FIRST,7,0x80)
BUILD_IRQ(SECOND,8,0x01)
BUILD_IRQ(SECOND,9,0x02)
BUILD_IRQ(SECOND,10,0x04)
BUILD_IRQ(SECOND,11,0x08)
BUILD_IRQ(SECOND,12,0x10)
BUILD_IRQ(SECOND,13,0x20)
BUILD_IRQ(SECOND,14,0x40)
BUILD_IRQ(SECOND,15,0x80)

#ifdef __SMP__
BUILD_SMP_INTERRUPT(reschedule_interrupt)
BUILD_SMP_INTERRUPT(invalidate_interrupt)
BUILD_SMP_INTERRUPT(stop_cpu_interrupt)
BUILD_SMP_TIMER_INTERRUPT(apic_timer_interrupt)
#endif

static void (*interrupt[17])(void) = {
	IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
	IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
	IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
	IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt	
};

/*
 * Initial irq handlers.
 */

static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * Note that on a 486, we don't want to do a SIGFPE on an irq13
 * as the irq is unreliable, and exception 16 works correctly
 * (ie as explained in the intel literature). On a 386, you
 * can't use exception 16 due to bad IBM design, so we have to
 * rely on the less exact irq13.
 *
 * Careful.. Not only is IRQ13 unreliable, but it is also
 * leads to races. IBM designers who came up with it should
 * be shot.
 */
 
static void math_error_irq(int cpl, void *dev_id, struct pt_regs *regs)
{
	outb(0,0xF0);
	if (ignore_irq13 || !boot_cpu_data.hard_math)
		return;
	math_error();
}

static struct irqaction irq13 = { math_error_irq, 0, 0, "fpu", NULL, NULL };

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

static struct irqaction *irq_action[16] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

int get_irq_list(char *buf)
{
	int i;
	struct irqaction * action;
	char *p = buf;

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		p += sprintf(p, "%3d: %10u   %s",
			i, kstat.interrupts[i], action->name);
		for (action=action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}
	p += sprintf(p, "NMI: %10u\n", atomic_read(&nmi_counter));
#ifdef __SMP_PROF__
	p += sprintf(p, "IPI: %10lu\n", ipi_count);
#endif		
	return p - buf;
}

#ifdef __SMP_PROF__

extern unsigned int prof_multiplier[NR_CPUS];
extern unsigned int prof_counter[NR_CPUS];

int get_smp_prof_list(char *buf) {
	int i,j, len = 0;
	struct irqaction * action;
	unsigned long sum_spins = 0;
	unsigned long sum_spins_syscall = 0;
	unsigned long sum_spins_sys_idle = 0;
	unsigned long sum_smp_idle_count = 0;
	unsigned long sum_local_timer_ticks = 0;

	for (i=0;i<smp_num_cpus;i++) {
		int cpunum = cpu_logical_map[i];
		sum_spins+=smp_spins[cpunum];
		sum_spins_syscall+=smp_spins_syscall[cpunum];
		sum_spins_sys_idle+=smp_spins_sys_idle[cpunum];
		sum_smp_idle_count+=smp_idle_count[cpunum];
		sum_local_timer_ticks+=smp_local_timer_ticks[cpunum];
	}

	len += sprintf(buf+len,"CPUS: %10i \n", smp_num_cpus);
	len += sprintf(buf+len,"            SUM ");
	for (i=0;i<smp_num_cpus;i++)
		len += sprintf(buf+len,"        P%1d ",cpu_logical_map[i]);
	len += sprintf(buf+len,"\n");
	for (i = 0 ; i < NR_IRQS ; i++) {
		action = *(i + irq_action);
		if (!action || !action->handler)
			continue;
		len += sprintf(buf+len, "%3d: %10d ",
			i, kstat.interrupts[i]);
		for (j=0;j<smp_num_cpus;j++)
			len+=sprintf(buf+len, "%10d ",
				int_count[cpu_logical_map[j]][i]);
		len += sprintf(buf+len, "  %s", action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ", %s", action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	len+=sprintf(buf+len, "LCK: %10lu",
		sum_spins);

	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10lu",smp_spins[cpu_logical_map[i]]);

	len +=sprintf(buf+len,"   spins from int\n");

	len+=sprintf(buf+len, "LCK: %10lu",
		sum_spins_syscall);

	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10lu",smp_spins_syscall[cpu_logical_map[i]]);

	len +=sprintf(buf+len,"   spins from syscall\n");

	len+=sprintf(buf+len, "LCK: %10lu",
		sum_spins_sys_idle);

	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10lu",smp_spins_sys_idle[cpu_logical_map[i]]);

	len +=sprintf(buf+len,"   spins from sysidle\n");
	len+=sprintf(buf+len,"IDLE %10lu",sum_smp_idle_count);

	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10lu",smp_idle_count[cpu_logical_map[i]]);

	len +=sprintf(buf+len,"   idle ticks\n");

	len+=sprintf(buf+len,"TICK %10lu",sum_local_timer_ticks);
	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10lu",smp_local_timer_ticks[cpu_logical_map[i]]);

	len +=sprintf(buf+len,"   local APIC timer ticks\n");

	len+=sprintf(buf+len,"MULT:          ");
	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10u",prof_multiplier[cpu_logical_map[i]]);
	len +=sprintf(buf+len,"   profiling multiplier\n");

	len+=sprintf(buf+len,"COUNT:         ");
	for (i=0;i<smp_num_cpus;i++)
		len+=sprintf(buf+len," %10u",prof_counter[cpu_logical_map[i]]);

	len +=sprintf(buf+len,"   profiling counter\n");

	len+=sprintf(buf+len, "IPI: %10lu   received\n",
		ipi_count);

	return len;
}
#endif 


/*
 * Global interrupt locks for SMP. Allow interrupts to come in on any
 * CPU, yet make cli/sti act globally to protect critical regions..
 */
#ifdef __SMP__
unsigned char global_irq_holder = NO_PROC_ID;
unsigned volatile int global_irq_lock;
atomic_t global_irq_count;

#define irq_active(cpu) \
	(global_irq_count != local_irq_count[cpu])

/*
 * "global_cli()" is a special case, in that it can hold the
 * interrupts disabled for a longish time, and also because
 * we may be doing TLB invalidates when holding the global
 * IRQ lock for historical reasons. Thus we may need to check
 * SMP invalidate events specially by hand here (but not in
 * any normal spinlocks)
 */
static inline void check_smp_invalidate(int cpu)
{
	if (test_bit(cpu, &smp_invalidate_needed)) {
		clear_bit(cpu, &smp_invalidate_needed);
		local_flush_tlb();
	}
}

static unsigned long previous_irqholder;

static inline void wait_on_irq(int cpu, unsigned long where)
{
	int local_count = local_irq_count[cpu];

	/* Are we the only one in an interrupt context? */
	while (local_count != atomic_read(&global_irq_count)) {
		/*
		 * No such luck. Now we need to release the lock,
		 * _and_ release our interrupt context, because
		 * otherwise we'd have dead-locks and live-locks
		 * and other fun things.
		 */
		atomic_sub(local_count, &global_irq_count);
		global_irq_lock = 0;

		/*
		 * Wait for everybody else to go away and release
		 * their things before trying to get the lock again.
		 */
		for (;;) {
			check_smp_invalidate(cpu);
			if (atomic_read(&global_irq_count))
				continue;
			if (global_irq_lock)
				continue;
			if (!test_and_set_bit(0,&global_irq_lock))
				break;
		}
		atomic_add(local_count, &global_irq_count);
	}
}

/*
 * This is called when we want to synchronize with
 * interrupts. We may for example tell a device to
 * stop sending interrupts: but to make sure there
 * are no interrupts that are executing on another
 * CPU we need to call this function.
 *
 * On UP this is a no-op.
 */
void synchronize_irq(void)
{
	int cpu = smp_processor_id();
	int local_count = local_irq_count[cpu];

	/* Do we need to wait? */
	if (local_count != atomic_read(&global_irq_count)) {
		/* The stupid way to do this */
		cli();
		sti();
	}
}

static inline void get_irqlock(int cpu, unsigned long where)
{
	if (test_and_set_bit(0,&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		do {
			do {
				check_smp_invalidate(cpu);
			} while (test_bit(0,&global_irq_lock));
		} while (test_and_set_bit(0,&global_irq_lock));		
	}
	/*
	 * Ok, we got the lock bit.
	 * But that's actually just the easy part.. Now
	 * we need to make sure that nobody else is running
	 * in an interrupt context. 
	 */
	wait_on_irq(cpu, where);

	/*
	 * Finally.
	 */
	global_irq_holder = cpu;
	previous_irqholder = where;
}

void __global_cli(void)
{
	int cpu = smp_processor_id();
	unsigned long where;

	__asm__("movl 16(%%esp),%0":"=r" (where));
	__cli();
	get_irqlock(cpu, where);
}

void __global_sti(void)
{
	release_irqlock(smp_processor_id());
	__sti();
}

unsigned long __global_save_flags(void)
{
	return global_irq_holder == (unsigned char) smp_processor_id();
}

void __global_restore_flags(unsigned long flags)
{
	switch (flags) {
	case 0:
		release_irqlock(smp_processor_id());
		__sti();
		break;
	case 1:
		__global_cli();
		break;
	default:
		printk("global_restore_flags: %08lx (%08lx)\n",
			flags, (&flags)[-1]);
	}
}

#endif

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
asmlinkage void do_IRQ(struct pt_regs regs)
{
	int irq = regs.orig_eax & 0xff;
	struct irqaction * action;
	int status, cpu;

	/* 
	 * mask and ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 */
	mask_and_ack_irq(irq);

	cpu = smp_processor_id();
	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;

	/* Return with this interrupt masked if no action */
	status = 0;
	action = *(irq + irq_action);
	if (action) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();

		do {
			status |= action->flags;
			action->handler(irq, action->dev_id, &regs);
			action = action->next;
		} while (action);
		if (status & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
		spin_lock(&irq_controller_lock);
		unmask_irq(irq);
		spin_unlock(&irq_controller_lock);
	}

	irq_exit(cpu, irq);
	/*
	 * This should be conditional: we should really get
	 * a return code from the irq handler to tell us
	 * whether the handler wants us to do software bottom
	 * half handling or not..
	 */
	if (1) {
		if (bh_active & bh_mask)
			do_bottom_half();
	}
}

int setup_x86_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	p = irq_action + irq;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ))
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	save_flags(flags);
	cli();
	*p = new;

	if (!shared) {
		spin_lock(&irq_controller_lock);
		unmask_irq(irq);
		spin_unlock(&irq_controller_lock);
	}
	restore_flags(flags);
	return 0;
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq > 15)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_x86_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq > 15) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_flags(flags);
		cli();
		*p = action->next;
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/* first, enable any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!irq_action[i]) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		/* about 100ms delay */;

	/* now filter out any obviously spurious interrupts */
	return irqs & ~cached_irq_mask;
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i;

#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04lx irqmask=0x%04x\n", irqs, cached_irq_mask);
#endif
	irqs &= cached_irq_mask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;
	return i;
}

__initfunc(void init_IRQ(void))
{
	int i;

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */

	for (i = 0; i < NR_IRQS ; i++)
		set_intr_gate(0x20+i,interrupt[i]);

#ifdef __SMP__	
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level
	 */

	/*
	 * The reschedule interrupt slowly changes it's functionality,
	 * while so far it was a kind of broadcasted timer interrupt,
	 * in the future it should become a CPU-to-CPU rescheduling IPI,
	 * driven by schedule() ?
	 *
	 * [ It has to be here .. it doesn't work if you put
	 *   it down the bottom - assembler explodes 8) ]
	 */
	/* IRQ '16' (trap 0x30) - IPI for rescheduling */
	set_intr_gate(0x20+i, reschedule_interrupt);


	/* IRQ '17' (trap 0x31) - IPI for invalidation */
	set_intr_gate(0x21+i, invalidate_interrupt);

	/* IRQ '18' (trap 0x40) - IPI for CPU halt */
	set_intr_gate(0x30+i, stop_cpu_interrupt);

	/* IRQ '19' (trap 0x41) - self generated IPI for local APIC timer */
	set_intr_gate(0x31+i, apic_timer_interrupt);
#endif	
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	setup_x86_irq(2, &irq2);
	setup_x86_irq(13, &irq13);
} 
