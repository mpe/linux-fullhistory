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

#define CR0_NE 32

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

unsigned int local_irq_count[NR_CPUS];
#ifdef __SMP__
atomic_t __intel_bh_counter;
#else
int __intel_bh_counter;
#endif

#ifdef __SMP_PROF__
static unsigned int int_count[NR_CPUS][NR_IRQS] = {{0},};
#endif

static inline void mask_irq(unsigned int irq_nr)
{
	unsigned char mask;

	mask = 1 << (irq_nr & 7);
	if (irq_nr < 8) {
		cache_21 |= mask;
		outb(cache_21,0x21);
	} else {
		cache_A1 |= mask;
		outb(cache_A1,0xA1);
	}
}

static inline void unmask_irq(unsigned int irq_nr)
{
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	if (irq_nr < 8) {
		cache_21 &= mask;
		outb(cache_21,0x21);
	} else {
		cache_A1 &= mask;
		outb(cache_A1,0xA1);
	}
}

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	mask_irq(irq_nr);
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	unmask_irq(irq_nr);
	restore_flags(flags);
}

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that do all
 * the operations that are needed to keep the AT interrupt-controller
 * happy. They are also written to be fast - and to disable interrupts
 * as little as humanly possible.
 *
 * NOTE! These macros expand to three different handlers for each line: one
 * complete handler that does all the fancy stuff (including signal handling),
 * and one fast handler that is meant for simple IRQ's that want to be
 * atomic. The specific handler is chosen depending on the SA_INTERRUPT
 * flag when installing a handler. Finally, one "bad interrupt" handler, that
 * is used when no handler is present.
 *
 * The timer interrupt is handled specially to insure that the jiffies
 * variable is updated at all times.  Specifically, the timer interrupt is
 * just like the complete handlers except that it is invoked with interrupts
 * disabled and should never re-enable them.  If other interrupts were
 * allowed to be processed while the timer interrupt is active, then the
 * other interrupts would have to avoid using the jiffies variable for delay
 * and interval timing operations to avoid hanging the system.
 */

#if NR_IRQS != 16
#error make irq stub building NR_IRQS dependent and remove me.
#endif

BUILD_TIMER_IRQ(FIRST,0,0x01)
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

/*
 * Pointers to the low-level handlers: first the general ones, then the
 * fast ones, then the bad ones.
 */
static void (*interrupt[17])(void) = {
	IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
	IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
	IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
	IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt	
};

static void (*fast_interrupt[16])(void) = {
	fast_IRQ0_interrupt, fast_IRQ1_interrupt,
	fast_IRQ2_interrupt, fast_IRQ3_interrupt,
	fast_IRQ4_interrupt, fast_IRQ5_interrupt,
	fast_IRQ6_interrupt, fast_IRQ7_interrupt,
	fast_IRQ8_interrupt, fast_IRQ9_interrupt,
	fast_IRQ10_interrupt, fast_IRQ11_interrupt,
	fast_IRQ12_interrupt, fast_IRQ13_interrupt,
	fast_IRQ14_interrupt, fast_IRQ15_interrupt
};

static void (*bad_interrupt[16])(void) = {
	bad_IRQ0_interrupt, bad_IRQ1_interrupt,
	bad_IRQ2_interrupt, bad_IRQ3_interrupt,
	bad_IRQ4_interrupt, bad_IRQ5_interrupt,
	bad_IRQ6_interrupt, bad_IRQ7_interrupt,
	bad_IRQ8_interrupt, bad_IRQ9_interrupt,
	bad_IRQ10_interrupt, bad_IRQ11_interrupt,
	bad_IRQ12_interrupt, bad_IRQ13_interrupt,
	bad_IRQ14_interrupt, bad_IRQ15_interrupt
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
	if (ignore_irq13 || !hard_math)
		return;
	math_error();
}

static struct irqaction irq13 = { math_error_irq, 0, 0, "math error", NULL, NULL };

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
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %10u %c %s",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, "\n");
	}
/*
 *	Linus - should you add NMI counts here ?????
 */
#ifdef __SMP_PROF__
	len+=sprintf(buf+len, "IPI: %8lu received\n",
		ipi_count);
#endif		
	return len;
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
		len += sprintf(buf+len, "%c %s",
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
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

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("wait_on_irq CPU#%d stuck at %08lx, waiting for %08lx (local=%d, global=%d)\n", cpu, where, previous_irqholder, local_count, atomic_read(&global_irq_count)); stuck = INIT_STUCK; }

static inline void wait_on_irq(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;
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
			STUCK;
			check_smp_invalidate(cpu);
			if (atomic_read(&global_irq_count))
				continue;
			if (global_irq_lock)
				continue;
			if (!set_bit(0,&global_irq_lock))
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

#undef INIT_STUCK
#define INIT_STUCK 10000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("get_irqlock stuck at %08lx, waiting for %08lx\n", where, previous_irqholder); stuck = INIT_STUCK;}

static inline void get_irqlock(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;

	if (set_bit(0,&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		do {
			do {
				STUCK;
				check_smp_invalidate(cpu);
			} while (test_bit(0,&global_irq_lock));
		} while (set_bit(0,&global_irq_lock));		
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
		__global_sti();
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
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int do_random, cpu = smp_processor_id();

	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;

	/* slow interrupts run with interrupts enabled */
	__sti();
	action = *(irq + irq_action);
	do_random = 0;
	while (action) {
		do_random |= action->flags;
		action->handler(irq, action->dev_id, regs);
		action = action->next;
	}
	if (do_random & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	irq_exit(cpu, irq);
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
	struct irqaction * action;
	int do_random, cpu = smp_processor_id();

	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;
	action = *(irq + irq_action);
	do_random = 0;
	while (action) {
		do_random |= action->flags;
		action->handler(irq, action->dev_id, NULL);
		action = action->next;
	}
	if (do_random & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	irq_exit(cpu, irq);
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

		/* Can't share interrupts unless both are same type */
		if ((old->flags ^ new->flags) & SA_INTERRUPT)
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
		if (new->flags & SA_INTERRUPT)
			set_intr_gate(0x20+irq,fast_interrupt[irq]);
		else
			set_intr_gate(0x20+irq,interrupt[irq]);
		unmask_irq(irq);
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
		if (!irq[irq_action]) {
			mask_irq(irq);
			set_intr_gate(0x20+irq,bad_interrupt[irq]);
		}
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
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
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	return irqs & ~irqmask;
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04lx irqmask=0x%04x\n", irqs, irqmask);
#endif
	irqs &= irqmask;
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
	static unsigned char smptrap=0;
	if(smptrap)
		return;
	smptrap=1;

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */

	for (i = 0; i < NR_IRQS ; i++)
		set_intr_gate(0x20+i,bad_interrupt[i]);

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
