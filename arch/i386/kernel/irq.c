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
#include <linux/tasks.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>
#include <asm/pgtable.h>
#include <asm/delay.h>

#include "irq.h"

/*
 * I had a lockup scenario where a tight loop doing
 * spin_unlock()/spin_lock() on CPU#1 was racing with
 * spin_lock() on CPU#0. CPU#0 should have noticed spin_unlock(), but
 * apparently the spin_unlock() information did not make it
 * through to CPU#0 ... nasty, is this by design, do we haveto limit
 * 'memory update oscillation frequency' artificially like here?
 *
 * Such 'high frequency update' races can be avoided by careful design, but
 * some of our major constructs like spinlocks use similar techniques,
 * it would be nice to clarify this issue. Set this define to 0 if you
 * want to check wether your system freezes. I suspect the delay done
 * by SYNC_OTHER_CORES() is in correlation with 'snooping latency', but
 * i thought that such things are guaranteed by design, since we use
 * the 'LOCK' prefix.
 */
#define SUSPECTED_CPU_OR_CHIPSET_BUG_WORKAROUND 1

#if SUSPECTED_CPU_OR_CHIPSET_BUG_WORKAROUND
# define SYNC_OTHER_CORES(x) udelay(x+1)
#else
/*
 * We have to allow irqs to arrive between __sti and __cli
 */
# define SYNC_OTHER_CORES(x) __asm__ __volatile__ ("nop")
#endif

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

atomic_t nmi_counter;

/*
 * About the IO-APIC, the architecture is 'merged' into our
 * current irq architecture, seemlessly. (i hope). It is only
 * visible through 8 more hardware interrupt lines, but otherwise
 * drivers are unaffected. The main code is believed to be
 * NR_IRQS-safe (nothing anymore thinks we have 16
 * irq lines only), but there might be some places left ...
 */

/*
 * This contains the irq mask for both 8259A irq controllers,
 * and on SMP the extended IO-APIC IRQs 16-23. The IO-APIC
 * uses this mask too, in probe_irq*().
 *
 * (0x0000ffff for NR_IRQS==16, 0x00ffffff for NR_IRQS=24)
 */
static unsigned int cached_irq_mask = (1<<NR_IRQS)-1;

#define cached_21	((cached_irq_mask | io_apic_irqs) & 0xff)
#define cached_A1	(((cached_irq_mask | io_apic_irqs) >> 8) & 0xff)

spinlock_t irq_controller_lock;

static int irq_events [NR_IRQS] = { -1, };
static int disabled_irq [NR_IRQS] = { 0, };
#ifdef __SMP__
static int irq_owner [NR_IRQS] = { NO_PROC_ID, };
#endif

/*
 * Not all IRQs can be routed through the IO-APIC, eg. on certain (older)
 * boards the timer interrupt and sometimes the keyboard interrupt is
 * not connected to any IO-APIC pin, it's fed to the CPU ExtInt IRQ line
 * directly.
 *
 * Any '1' bit in this mask means the IRQ is routed through the IO-APIC.
 * this 'mixed mode' IRQ handling costs us one more branch in do_IRQ,
 * but we have _much_ higher compatibility and robustness this way.
 */

#ifndef __SMP__
  static const unsigned int io_apic_irqs = 0;
#else
  /*
   * Default to all normal IRQ's _not_ using the IO APIC.
   *
   * To get IO-APIC interrupts you should either:
   *  - turn some of them into IO-APIC interrupts at runtime
   *    with some magic system call interface.
   *  - explicitly use irq 16-19 depending on which PCI irq
   *    line your PCI controller uses.
   */
  unsigned int io_apic_irqs = 0xff0000;
#endif

static inline void mask_8259A(int irq)
{
	cached_irq_mask |= 1 << irq;
	if (irq & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

static inline void unmask_8259A(int irq)
{
	cached_irq_mask &= ~(1 << irq);
	if (irq & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

void set_8259A_irq_mask(int irq)
{
	/*
	 * (it might happen that we see IRQ>15 on a UP box, with SMP
	 * emulation)
	 */
	if (irq < 16) {
		if (irq & 8) {
			outb(cached_A1,0xA1);
		} else {
			outb(cached_21,0x21);
		}
	}
}

/*
 * These have to be protected by the spinlock
 * before being called.
 */
void mask_irq(unsigned int irq)
{
	if (IO_APIC_IRQ(irq))
		disable_IO_APIC_irq(irq);
	else {
		cached_irq_mask |= 1 << irq;
		set_8259A_irq_mask(irq);
	}
}

void unmask_irq(unsigned int irq)
{
	if (IO_APIC_IRQ(irq))
		enable_IO_APIC_irq(irq);
	else {
		cached_irq_mask &= ~(1 << irq);
		set_8259A_irq_mask(irq);
	}
}

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that save
 * register context and call do_IRQ(). do_IRQ() then does all the
 * operations that are needed to keep the AT (or SMP IOAPIC)
 * interrupt-controller happy.
 */


BUILD_COMMON_IRQ()
/*
 * ISA PIC or IO-APIC triggered (INTA-cycle or APIC) interrupts:
 */
BUILD_IRQ(0) BUILD_IRQ(1) BUILD_IRQ(2) BUILD_IRQ(3)
BUILD_IRQ(4) BUILD_IRQ(5) BUILD_IRQ(6) BUILD_IRQ(7)
BUILD_IRQ(8) BUILD_IRQ(9) BUILD_IRQ(10) BUILD_IRQ(11)
BUILD_IRQ(12) BUILD_IRQ(13) BUILD_IRQ(14) BUILD_IRQ(15)

#ifdef __SMP__

/*
 * The IO-APIC (persent only in SMP boards) has 8 more hardware
 * interrupt pins, for all of them we define an IRQ vector:
 *
 * raw PCI interrupts 0-3, basically these are the ones used
 * heavily:
 */
BUILD_IRQ(16) BUILD_IRQ(17) BUILD_IRQ(18) BUILD_IRQ(19)

/*
 * [FIXME: anyone with 2 separate PCI buses and 2 IO-APICs,
 *         please speak up and request experimental patches.
 *         --mingo ]
 */

/*
 * MIRQ (motherboard IRQ) interrupts 0-1:
 */
BUILD_IRQ(20) BUILD_IRQ(21)

/*
 * 'nondefined general purpose interrupt'.
 */
BUILD_IRQ(22)
/*
 * optionally rerouted SMI interrupt:
 */
BUILD_IRQ(23)

/*
 * The following vectors are part of the Linux architecture, there
 * is no hardware IRQ pin equivalent for them, they are triggered
 * through the ICC by us (IPIs), via smp_message_pass():
 */
BUILD_SMP_INTERRUPT(reschedule_interrupt)
BUILD_SMP_INTERRUPT(invalidate_interrupt)
BUILD_SMP_INTERRUPT(stop_cpu_interrupt)

/*
 * every pentium local APIC has two 'local interrupts', with a
 * soft-definable vector attached to both interrupts, one of
 * which is a timer interrupt, the other one is error counter
 * overflow. Linux uses the local APIC timer interrupt to get
 * a much simpler SMP time architecture:
 */
BUILD_SMP_TIMER_INTERRUPT(apic_timer_interrupt)

#endif

static void (*interrupt[NR_IRQS])(void) = {
	IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
	IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
	IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
	IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt
#ifdef __SMP__
	,IRQ16_interrupt, IRQ17_interrupt, IRQ18_interrupt, IRQ19_interrupt,
	IRQ20_interrupt, IRQ21_interrupt, IRQ22_interrupt, IRQ23_interrupt
#endif
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

static struct irqaction *irq_action[NR_IRQS] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
#ifdef __SMP__
	,NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
#endif
};

int get_irq_list(char *buf)
{
	int i, j;
	struct irqaction * action;
	char *p = buf;

	p += sprintf(p, "           ");
	for (j=0; j<smp_num_cpus; j++)
		p += sprintf(p, "CPU%d       ",j);
	*p++ = '\n';

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		p += sprintf(p, "%3d: ",i);
#ifndef __SMP__
		p += sprintf(p, "%10u ", kstat_irqs(i));
#else
		for (j=0; j<smp_num_cpus; j++)
			p += sprintf(p, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#endif

		if (IO_APIC_IRQ(i))
			p += sprintf(p, " IO-APIC ");
		else
			p += sprintf(p, "  XT PIC ");
		p += sprintf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}
	p += sprintf(p, "NMI: %10u\n", atomic_read(&nmi_counter));
#ifdef __SMP__
	p += sprintf(p, "IPI: %10lu\n", ipi_count);
#endif		
	return p - buf;
}

/*
 * Global interrupt locks for SMP. Allow interrupts to come in on any
 * CPU, yet make cli/sti act globally to protect critical regions..
 */
#ifdef __SMP__
unsigned char global_irq_holder = NO_PROC_ID;
unsigned volatile int global_irq_lock;
atomic_t global_irq_count;

atomic_t global_bh_count;
atomic_t global_bh_lock;

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

static void show(char * str)
{
	int i;
	unsigned long *stack;
	int cpu = smp_processor_id();

	printk("\n%s, CPU %d:\n", str, cpu);
	printk("irq:  %d [%d %d]\n",
		atomic_read(&global_irq_count), local_irq_count[0], local_irq_count[1]);
	printk("bh:   %d [%d %d]\n",
		atomic_read(&global_bh_count), local_bh_count[0], local_bh_count[1]);
	stack = (unsigned long *) &str;
	for (i = 40; i ; i--) {
		unsigned long x = *++stack;
		if (x > (unsigned long) &init_task_union && x < (unsigned long) &vsprintf) {
			printk("<[%08lx]> ", x);
		}
	}
}
	

#define MAXCOUNT 100000000

static inline void wait_on_bh(void)
{
	int count = MAXCOUNT;
	do {
		if (!--count) {
			show("wait_on_bh");
			count = ~0;
		}
		/* nothing .. wait for the other bh's to go away */
	} while (atomic_read(&global_bh_count) != 0);
}

static inline void wait_on_irq(int cpu)
{
	int count = MAXCOUNT;

	for (;;) {

		/*
		 * Wait until all interrupts are gone. Wait
		 * for bottom half handlers unless we're
		 * already executing in one..
		 */
		if (!atomic_read(&global_irq_count)) {
			if (local_bh_count[cpu] || !atomic_read(&global_bh_count))
				break;
		}

		/* Duh, we have to loop. Release the lock to avoid deadlocks */
		clear_bit(0,&global_irq_lock);

		for (;;) {
			if (!--count) {
				show("wait_on_irq");
				count = ~0;
			}
			__sti();
			SYNC_OTHER_CORES(cpu);
			__cli();
			check_smp_invalidate(cpu);
			if (atomic_read(&global_irq_count))
				continue;
			if (global_irq_lock)
				continue;
			if (!local_bh_count[cpu] && atomic_read(&global_bh_count))
				continue;
			if (!test_and_set_bit(0,&global_irq_lock))
				break;
		}
	}
}

/*
 * This is called when we want to synchronize with
 * bottom half handlers. We need to wait until
 * no other CPU is executing any bottom half handler.
 *
 * Don't wait if we're already running in an interrupt
 * context or are inside a bh handler.
 */
void synchronize_bh(void)
{
	if (atomic_read(&global_bh_count)) {
		int cpu = smp_processor_id();
		if (!local_irq_count[cpu] && !local_bh_count[cpu]) {
			wait_on_bh();
		}
	}
}

/*
 * This is called when we want to synchronize with
 * interrupts. We may for example tell a device to
 * stop sending interrupts: but to make sure there
 * are no interrupts that are executing on another
 * CPU we need to call this function.
 */
void synchronize_irq(void)
{
	if (atomic_read(&global_irq_count)) {
		/* Stupid approach */
		cli();
		sti();
	}
}

static inline void get_irqlock(int cpu)
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
	 * We also to make sure that nobody else is running
	 * in an interrupt context. 
	 */
	wait_on_irq(cpu);

	/*
	 * Ok, finally..
	 */
	global_irq_holder = cpu;
}

/*
 * A global "cli()" while in an interrupt context
 * turns into just a local cli(). Interrupts
 * should use spinlocks for the (very unlikely)
 * case that they ever want to protect against
 * each other.
 */
void __global_cli(void)
{
	int cpu = smp_processor_id();

	__cli();
	if (!local_irq_count[cpu])
		get_irqlock(cpu);
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (!local_irq_count[cpu])
		release_irqlock(cpu);
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

static int handle_IRQ_event(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int status;

	status = 0;
	action = *(irq + irq_action);

	if (action) {
		status |= 1;

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
	}

	return status;
}


/*
 * disable/enable_irq() wait for all irq contexts to finish
 * executing. Also it's recursive.
 */
void disable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	disabled_irq[irq]++;
	mask_irq(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);

	synchronize_irq();
}

void enable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	disabled_irq[irq]--;
	unmask_irq(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * Careful! The 8259A is a fragile beast, it pretty
 * much _has_ to be done exactly like this (mask it
 * first, _then_ send the EOI, and the order of EOI
 * to the two 8259s is important!
 */
static inline void mask_and_ack_8259A(int irq_nr)
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

static void do_8259A_IRQ(int irq, int cpu, struct pt_regs * regs)
{
	mask_and_ack_8259A(irq);

	irq_enter(cpu, irq);

	if (handle_IRQ_event(irq, regs)) {
		spin_lock(&irq_controller_lock);
		unmask_8259A(irq);
		spin_unlock(&irq_controller_lock);
	}

	irq_exit(cpu, irq);
}

#ifdef __SMP__
/*
 * FIXME! This is completely broken.
 */
static void do_ioapic_IRQ(int irq, int cpu, struct pt_regs * regs)
{
	int should_handle_irq;

	spin_lock(&irq_controller_lock);
	should_handle_irq = 0;
	if (!irq_events[irq]++ && !disabled_irq[irq]) {
		should_handle_irq = 1;
		irq_owner[irq] = cpu;
		hardirq_enter(cpu);
	}

	ack_APIC_irq();

	spin_unlock(&irq_controller_lock);
	
	if (should_handle_irq) {
again:
		if (!handle_IRQ_event(irq, regs))
			disabled_irq[irq] = 1;

	}

	spin_lock(&irq_controller_lock);
	release_irqlock(cpu);

	if ((--irq_events[irq]) && (!disabled_irq[irq]) && should_handle_irq) {
		spin_unlock(&irq_controller_lock);
		goto again;
	}

	irq_owner[irq] = NO_PROC_ID;
	hardirq_exit(cpu);
	spin_unlock(&irq_controller_lock);

	enable_IO_APIC_irq(irq);
}
#endif

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 *
 * the biggest change on SMP is the fact that we no more mask
 * interrupts in hardware, please believe me, this is unavoidable,
 * the hardware is largely message-oriented, i tried to force our
 * state-driven irq handling scheme onto the IO-APIC, but no avail.
 *
 * so we soft-disable interrupts via 'event counters', the first 'incl'
 * will do the IRQ handling. This also has the nice side effect of increased
 * overlapping ... i saw no driver problem so far.
 */
asmlinkage void do_IRQ(struct pt_regs regs)
{	
	void (*do_lowlevel_IRQ)(int, int, struct pt_regs *);

	/* 
	 * We ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	int irq = regs.orig_eax & 0xff;
	int cpu = smp_processor_id();

	kstat.irqs[cpu][irq]++;

	do_lowlevel_IRQ = do_8259A_IRQ;
#ifdef __SMP__
	if (IO_APIC_IRQ(irq))
		do_lowlevel_IRQ = do_ioapic_IRQ;
#endif
	
	do_lowlevel_IRQ(irq, cpu, &regs);

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
		if (IO_APIC_IRQ(irq)) {
			/*
			 * First disable it in the 8259A:
			 */
			cached_irq_mask |= 1 << irq;
			if (irq < 16)
				set_8259A_irq_mask(irq);
			setup_IO_APIC_irq(irq);
		}
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

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_KERNEL);
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

	if (irq >= NR_IRQS) {
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

/*
 * probing is always single threaded [FIXME: is this true?]
 */
static unsigned int probe_irqs[NR_CPUS][NR_IRQS];

unsigned long probe_irq_on (void)
{
	unsigned int i, j, irqs = 0;
	unsigned long delay;

	/*
	 * save current irq counts
	 */
	memcpy(probe_irqs,kstat.irqs,NR_CPUS*NR_IRQS*sizeof(int));

	/*
	 * first, enable any unassigned irqs
	 */
	for (i = NR_IRQS-1; i > 0; i--) {
		if (!irq_action[i]) {
			spin_lock(&irq_controller_lock);
			unmask_irq(i);
			irqs |= (1 << i);
			spin_unlock(&irq_controller_lock);
		}
	}

	/*
	 * wait for spurious interrupts to increase counters
	 */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		/* about 100ms delay */ synchronize_irq();

	/*
	 * now filter out any obviously spurious interrupts
	 */
	for (i=0; i<NR_IRQS; i++)
		for (j=0; j<NR_CPUS; j++)
			if (kstat.irqs[j][i] != probe_irqs[j][i])
				irqs &= ~(i<<1);

	return irqs;
}

int probe_irq_off (unsigned long irqs)
{
	int i,j, irq_found = -1;

	for (i=0; i<NR_IRQS; i++) {
		int sum = 0;
		for (j=0; j<NR_CPUS; j++) {
			sum += kstat.irqs[j][i];
			sum -= probe_irqs[j][i];
		}
		if (sum && (irqs & (i<<1))) {
			if (irq_found != -1) {
				irq_found = -irq_found;
				goto out;
			} else
				irq_found = i;
		}
	}
	if (irq_found == -1)
		irq_found = 0;
out:
	return irq_found;
}

void init_IO_APIC_traps(void)
{
	int i;
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level
	 *
	 * also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kindof importantish ;)
	 */
	for (i = 0; i < NR_IRQS ; i++)
		if (IO_APIC_GATE_OFFSET+(i<<3) <= 0xfe)  /* HACK */ {
			if (IO_APIC_IRQ(i)) {
				/*
				 * First disable it in the 8259A:
				 */
				cached_irq_mask |= 1 << i;
				if (i < 16)
					set_8259A_irq_mask(i);
				setup_IO_APIC_irq(i);
			}
		}
}

__initfunc(void init_IRQ(void))
{
	int i;

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */

	printk("INIT IRQ\n");
	for (i=0; i<NR_IRQS; i++) {
		irq_events[i] = 0;
#ifdef __SMP__
		irq_owner[i] = NO_PROC_ID;
#endif
		disabled_irq[i] = 0;
	}
	/*
	 * 16 old-style INTA-cycle interrupt gates:
	 */
	for (i = 0; i < 16; i++)
		set_intr_gate(0x20+i,interrupt[i]);

#ifdef __SMP__	

	for (i = 0; i < NR_IRQS ; i++)
		if (IO_APIC_GATE_OFFSET+(i<<3) <= 0xfe)  /* hack -- mingo */
			set_intr_gate(IO_APIC_GATE_OFFSET+(i<<3),interrupt[i]);

	/*
	 * The reschedule interrupt slowly changes it's functionality,
	 * while so far it was a kind of broadcasted timer interrupt,
	 * in the future it should become a CPU-to-CPU rescheduling IPI,
	 * driven by schedule() ?
	 *
	 * [ It has to be here .. it doesn't work if you put
	 *   it down the bottom - assembler explodes 8) ]
	 */

	/* IPI for rescheduling */
	set_intr_gate(0x30, reschedule_interrupt);

	/* IPI for invalidation */
	set_intr_gate(0x31, invalidate_interrupt);

	/* IPI for CPU halt */
	set_intr_gate(0x40, stop_cpu_interrupt);

	/* self generated IPI for local APIC timer */
	set_intr_gate(0x41, apic_timer_interrupt);

#endif	
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	setup_x86_irq(2, &irq2);
	setup_x86_irq(13, &irq13);
} 

