/*
 *	linux/arch/i386/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQs are in fact implemented a bit like signal handlers for the kernel.
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
#include <asm/delay.h>
#include <asm/desc.h>

#include "irq.h"

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

atomic_t nmi_counter;

/*
 * Linux has a controller-independent x86 interrupt architecture.
 * every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the apropriate
 * controller. Thus drivers need not be aware of the
 * interrupt-controller.
 *
 * Various interrupt controllers we handle: 8259 PIC, SMP IO-APIC,
 * PIIX4's internal 8259 PIC and SGI's Visual Workstation Cobalt (IO-)APIC.
 * (IO-APICs assumed to be messaging to Pentium local-APICs)
 *
 * the code is designed to be easily extended with new/different
 * interrupt controllers, without having to do assembly magic.
 */

/*
 * Micro-access to controllers is serialized over the whole
 * system. We never hold this lock when we call the actual
 * IRQ handler.
 */
spinlock_t irq_controller_lock;

/*
 * Dummy controller type for unused interrupts
 */
static void do_none(unsigned int irq, struct pt_regs * regs)
{
	/*
	 * we are careful. While for ISA irqs it's common to happen
	 * outside of any driver (think autodetection), this is not
	 * at all nice for PCI interrupts. So we are stricter and
	 * print a warning when such spurious interrupts happen.
	 * Spurious interrupts can confuse other drivers if the PCI
	 * IRQ line is shared.
	 *
	 * Such spurious interrupts are either driver bugs, or
	 * sometimes hw (chipset) bugs.
	 */
	printk("unexpected IRQ vector %d on CPU#%d!\n",irq, smp_processor_id());

#ifdef __SMP__
	/*
	 * [currently unexpected vectors happen only on SMP and APIC.
	 *  if we want to have non-APIC and non-8259A controllers
	 *  in the future with unexpected vectors, this ack should
	 *  probably be made controller-specific.]
	 */
	ack_APIC_irq();
#endif
}
static void enable_none(unsigned int irq) { }
static void disable_none(unsigned int irq) { }

/* startup is the same as "enable", shutdown is same as "disable" */
#define startup_none	enable_none
#define shutdown_none	disable_none

struct hw_interrupt_type no_irq_type = {
	"none",
	startup_none,
	shutdown_none,
	do_none,
	enable_none,
	disable_none
};

/*
 * This is the 'legacy' 8259A Programmable Interrupt Controller,
 * present in the majority of PC/AT boxes.
 */

static void do_8259A_IRQ(unsigned int irq, struct pt_regs * regs);
static void enable_8259A_irq(unsigned int irq);
void disable_8259A_irq(unsigned int irq);

/* startup is the same as "enable", shutdown is same as "disable" */
#define startup_8259A_irq	enable_8259A_irq
#define shutdown_8259A_irq	disable_8259A_irq

static struct hw_interrupt_type i8259A_irq_type = {
	"XT-PIC",
	startup_8259A_irq,
	shutdown_8259A_irq,
	do_8259A_IRQ,
	enable_8259A_irq,
	disable_8259A_irq
};

/*
 * Controller mappings for all interrupt sources:
 */
irq_desc_t irq_desc[NR_IRQS] = { [0 ... NR_IRQS-1] = { 0, &no_irq_type, }};


/*
 * 8259A PIC functions to handle ISA devices:
 */

/*
 * This contains the irq mask for both 8259A irq controllers,
 */
static unsigned int cached_irq_mask = 0xffff;

#define __byte(x,y) 	(((unsigned char *)&(y))[x])
#define cached_21	(__byte(0,cached_irq_mask))
#define cached_A1	(__byte(1,cached_irq_mask))

/*
 * Not all IRQs can be routed through the IO-APIC, eg. on certain (older)
 * boards the timer interrupt is not connected to any IO-APIC pin, it's
 * fed to the CPU IRQ line directly.
 *
 * Any '1' bit in this mask means the IRQ is routed through the IO-APIC.
 * this 'mixed mode' IRQ handling costs nothing because it's only used
 * at IRQ setup time.
 */
unsigned long io_apic_irqs = 0;

/*
 * These have to be protected by the irq controller spinlock
 * before being called.
 */
void disable_8259A_irq(unsigned int irq)
{
	unsigned int mask = 1 << irq;
	cached_irq_mask |= mask;
	if (irq & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

static void enable_8259A_irq(unsigned int irq)
{
	unsigned int mask = ~(1 << irq);
	cached_irq_mask &= mask;
	if (irq & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

int i8259A_irq_pending(unsigned int irq)
{
	unsigned int mask = 1<<irq;

	if (irq < 8)
                return (inb(0x20) & mask);
        return (inb(0xA0) & (mask >> 8));
}

void make_8259A_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	io_apic_irqs &= ~(1<<irq);
	irq_desc[irq].handler = &i8259A_irq_type;
	enable_irq(irq);
}

/*
 * Careful! The 8259A is a fragile beast, it pretty
 * much _has_ to be done exactly like this (mask it
 * first, _then_ send the EOI, and the order of EOI
 * to the two 8259s is important!
 */
static inline void mask_and_ack_8259A(unsigned int irq)
{
	cached_irq_mask |= 1 << irq;
	if (irq & 8) {
		inb(0xA1);	/* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x62,0x20);	/* Specific EOI to cascade */
		outb(0x20,0xA0);
	} else {
		inb(0x21);	/* DUMMY */
		outb(cached_21,0x21);
		outb(0x20,0x20);
	}
}

static void do_8259A_IRQ(unsigned int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	irq_desc_t *desc = irq_desc + irq;

	spin_lock(&irq_controller_lock);
	{
		unsigned int status;
		mask_and_ack_8259A(irq);
		status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
		action = NULL;
		if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
			action = desc->action;
			status |= IRQ_INPROGRESS;
		}
		desc->status = status;
	}
	spin_unlock(&irq_controller_lock);

	/* Exit early if we had no action or it was disabled */
	if (!action)
		return;

	handle_IRQ_event(irq, regs, action);

	spin_lock(&irq_controller_lock);
	{
		unsigned int status = desc->status & ~IRQ_INPROGRESS;
		desc->status = status;
		if (!(status & IRQ_DISABLED))
			enable_8259A_irq(irq);
	}
	spin_unlock(&irq_controller_lock);
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

#define BI(x,y) \
	BUILD_IRQ(##x##y)

#define BUILD_16_IRQS(x) \
	BI(x,0) BI(x,1) BI(x,2) BI(x,3) \
	BI(x,4) BI(x,5) BI(x,6) BI(x,7) \
	BI(x,8) BI(x,9) BI(x,a) BI(x,b) \
	BI(x,c) BI(x,d) BI(x,e) BI(x,f)

/*
 * ISA PIC or low IO-APIC triggered (INTA-cycle or APIC) interrupts:
 * (these are usually mapped to vectors 0x20-0x30)
 */
BUILD_16_IRQS(0x0)

#ifdef CONFIG_X86_IO_APIC
/*
 * The IO-APIC gives us many more interrupt sources. Most of these 
 * are unused but an SMP system is supposed to have enough memory ...
 * sometimes (mostly wrt. hw bugs) we get corrupted vectors all
 * across the spectrum, so we really want to be prepared to get all
 * of these. Plus, more powerful systems might have more than 64
 * IO-APIC registers.
 *
 * (these are usually mapped into the 0x30-0xff vector range)
 */
                   BUILD_16_IRQS(0x1) BUILD_16_IRQS(0x2) BUILD_16_IRQS(0x3)
BUILD_16_IRQS(0x4) BUILD_16_IRQS(0x5) BUILD_16_IRQS(0x6) BUILD_16_IRQS(0x7)
BUILD_16_IRQS(0x8) BUILD_16_IRQS(0x9) BUILD_16_IRQS(0xa) BUILD_16_IRQS(0xb)
BUILD_16_IRQS(0xc) BUILD_16_IRQS(0xd)
#endif

#undef BUILD_16_IRQS
#undef BI


#ifdef __SMP__
/*
 * The following vectors are part of the Linux architecture, there
 * is no hardware IRQ pin equivalent for them, they are triggered
 * through the ICC by us (IPIs)
 */
BUILD_SMP_INTERRUPT(reschedule_interrupt)
BUILD_SMP_INTERRUPT(invalidate_interrupt)
BUILD_SMP_INTERRUPT(stop_cpu_interrupt)
BUILD_SMP_INTERRUPT(call_function_interrupt)
BUILD_SMP_INTERRUPT(spurious_interrupt)

/*
 * every pentium local APIC has two 'local interrupts', with a
 * soft-definable vector attached to both interrupts, one of
 * which is a timer interrupt, the other one is error counter
 * overflow. Linux uses the local APIC timer interrupt to get
 * a much simpler SMP time architecture:
 */
BUILD_SMP_TIMER_INTERRUPT(apic_timer_interrupt)

#endif

#define IRQ(x,y) \
	IRQ##x##y##_interrupt

#define IRQLIST_16(x) \
	IRQ(x,0), IRQ(x,1), IRQ(x,2), IRQ(x,3), \
	IRQ(x,4), IRQ(x,5), IRQ(x,6), IRQ(x,7), \
	IRQ(x,8), IRQ(x,9), IRQ(x,a), IRQ(x,b), \
	IRQ(x,c), IRQ(x,d), IRQ(x,e), IRQ(x,f)

static void (*interrupt[NR_IRQS])(void) = {
	IRQLIST_16(0x0),

#ifdef CONFIG_X86_IO_APIC
	                 IRQLIST_16(0x1), IRQLIST_16(0x2), IRQLIST_16(0x3),
	IRQLIST_16(0x4), IRQLIST_16(0x5), IRQLIST_16(0x6), IRQLIST_16(0x7),
	IRQLIST_16(0x8), IRQLIST_16(0x9), IRQLIST_16(0xa), IRQLIST_16(0xb),
	IRQLIST_16(0xc), IRQLIST_16(0xd)
#endif
};

#undef IRQ
#undef IRQLIST_16


/*
 * Special irq handlers.
 */

void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

#ifndef CONFIG_VISWS
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
#endif

/*
 * Generic, controller-independent functions:
 */

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
		action = irq_desc[i].action;
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
		p += sprintf(p, " %14s", irq_desc[i].handler->typename);
		p += sprintf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}
	p += sprintf(p, "NMI: %10u\n", atomic_read(&nmi_counter));
#ifdef __SMP__
	p += sprintf(p, "ERR: %10lu\n", ipi_count);
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
	extern char *get_options(char *str, int *ints);

	printk("\n%s, CPU %d:\n", str, cpu);
	printk("irq:  %d [%d %d]\n",
		atomic_read(&global_irq_count), local_irq_count[0], local_irq_count[1]);
	printk("bh:   %d [%d %d]\n",
		atomic_read(&global_bh_count), local_bh_count[0], local_bh_count[1]);
	stack = (unsigned long *) &stack;
	for (i = 40; i ; i--) {
		unsigned long x = *++stack;
		if (x > (unsigned long) &get_options && x < (unsigned long) &vsprintf) {
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

/*
 * I had a lockup scenario where a tight loop doing
 * spin_unlock()/spin_lock() on CPU#1 was racing with
 * spin_lock() on CPU#0. CPU#0 should have noticed spin_unlock(), but
 * apparently the spin_unlock() information did not make it
 * through to CPU#0 ... nasty, is this by design, do we have to limit
 * 'memory update oscillation frequency' artificially like here?
 *
 * Such 'high frequency update' races can be avoided by careful design, but
 * some of our major constructs like spinlocks use similar techniques,
 * it would be nice to clarify this issue. Set this define to 0 if you
 * want to check whether your system freezes.  I suspect the delay done
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
	if (atomic_read(&global_bh_count) && !in_interrupt())
		wait_on_bh();
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

#define EFLAGS_IF_SHIFT 9

/*
 * A global "cli()" while in an interrupt context
 * turns into just a local cli(). Interrupts
 * should use spinlocks for the (very unlikely)
 * case that they ever want to protect against
 * each other.
 *
 * If we already have local interrupts disabled,
 * this will not turn a local disable into a
 * global one (problems with spinlocks: this makes
 * save_flags+cli+sti usable inside a spinlock).
 */
void __global_cli(void)
{
	unsigned int flags;

	__save_flags(flags);
	if (flags & (1 << EFLAGS_IF_SHIFT)) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count[cpu])
			get_irqlock(cpu);
	}
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (!local_irq_count[cpu])
		release_irqlock(cpu);
	__sti();
}

/*
 * SMP flags value to restore to:
 * 0 - global cli
 * 1 - global sti
 * 2 - local cli
 * 3 - local sti
 */
unsigned long __global_save_flags(void)
{
	int retval;
	int local_enabled;
	unsigned long flags;

	__save_flags(flags);
	local_enabled = (flags >> EFLAGS_IF_SHIFT) & 1;
	/* default to local */
	retval = 2 + local_enabled;

	/* check for global flags if we're not in an interrupt */
	if (!local_irq_count[smp_processor_id()]) {
		if (local_enabled)
			retval = 1;
		if (global_irq_holder == (unsigned char) smp_processor_id())
			retval = 0;
	}
	return retval;
}

void __global_restore_flags(unsigned long flags)
{
	switch (flags) {
	case 0:
		__global_cli();
		break;
	case 1:
		__global_sti();
		break;
	case 2:
		__cli();
		break;
	case 3:
		__sti();
		break;
	default:
		printk("global_restore_flags: %08lx (%08lx)\n",
			flags, (&flags)[-1]);
	}
}

#endif

/*
 * This should really return information about whether
 * we should do bottom half handling etc. Right now we
 * end up _always_ checking the bottom half, which is a
 * waste of time and is not what some drivers would
 * prefer.
 */
int handle_IRQ_event(unsigned int irq, struct pt_regs * regs, struct irqaction * action)
{
	int status;
	int cpu = smp_processor_id();

	irq_enter(cpu, irq);

	status = 1;	/* Force the "do bottom halves" bit */

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

	irq_exit(cpu, irq);

	return status;
}

/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock. 
 */
void disable_irq_nosync(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	if (!irq_desc[irq].depth++) {
		irq_desc[irq].status |= IRQ_DISABLED;
		irq_desc[irq].handler->disable(irq);
	}
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * Synchronous version of the above, making sure the IRQ is
 * no longer running on any other IRQ..
 */
void disable_irq(unsigned int irq)
{
	disable_irq_nosync(irq);

	if (!local_irq_count[smp_processor_id()]) {
		do {
			barrier();
		} while (irq_desc[irq].status & IRQ_INPROGRESS);
	}
}

void enable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	switch (irq_desc[irq].depth) {
	case 1:
		irq_desc[irq].status &= ~IRQ_DISABLED;
		irq_desc[irq].handler->enable(irq);
		/* fall throught */
	default:
		irq_desc[irq].depth--;
		break;
	case 0:
		printk("enable_irq() unbalanced from %p\n",
		       __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
asmlinkage void do_IRQ(struct pt_regs regs)
{	
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
	int irq = regs.orig_eax & 0xff; /* subtle, see irq.h */
	int cpu = smp_processor_id();

	kstat.irqs[cpu][irq]++;
	irq_desc[irq].handler->handle(irq, &regs);

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

int setup_x86_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&irq_controller_lock,flags);
	p = &irq_desc[irq].action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&irq_controller_lock,flags);
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
		irq_desc[irq].depth = 0;
		irq_desc[irq].status &= ~IRQ_DISABLED;
		irq_desc[irq].handler->startup(irq);
	}
	spin_unlock_irqrestore(&irq_controller_lock,flags);
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

	if (irq >= NR_IRQS)
		return;

	spin_lock_irqsave(&irq_controller_lock,flags);
	for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		*p = action->next;
		kfree(action);
		if (!irq_desc[irq].action) {
			irq_desc[irq].status |= IRQ_DISABLED;
			irq_desc[irq].handler->shutdown(irq);
		}
		goto out;
	}
	printk("Trying to free free IRQ%d\n",irq);
out:
	spin_unlock_irqrestore(&irq_controller_lock,flags);
}

/*
 * IRQ autodetection code..
 *
 * This depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck
 * with "IRQ_WAITING" cleared and the interrupt
 * disabled.
 */
unsigned long probe_irq_on(void)
{
	unsigned int i;
	unsigned long delay;

	/*
	 * first, enable any unassigned irqs
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = NR_IRQS-1; i > 0; i--) {
		if (!irq_desc[i].action) {
			irq_desc[i].status |= IRQ_AUTODETECT | IRQ_WAITING;
			irq_desc[i].handler->startup(i);
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	/*
	 * Wait for spurious interrupts to trigger
	 */
	for (delay = jiffies + HZ/10; time_after(delay, jiffies); )
		/* about 100ms delay */ synchronize_irq();

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i=0; i<NR_IRQS; i++) {
		unsigned int status = irq_desc[i].status;

		if (!(status & IRQ_AUTODETECT))
			continue;
		
		/* It triggered already - consider it spurious. */
		if (!(status & IRQ_WAITING)) {
			irq_desc[i].status = status & ~IRQ_AUTODETECT;
			irq_desc[i].handler->shutdown(i);
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	return 0x12345678;
}

int probe_irq_off(unsigned long unused)
{
	int i, irq_found, nr_irqs;

	if (unused != 0x12345678)
		printk("Bad IRQ probe from %lx\n", (&unused)[-1]);

	nr_irqs = 0;
	irq_found = 0;
	spin_lock_irq(&irq_controller_lock);
	for (i=0; i<NR_IRQS; i++) {
		unsigned int status = irq_desc[i].status;

		if (!(status & IRQ_AUTODETECT))
			continue;

		if (!(status & IRQ_WAITING)) {
			if (!nr_irqs)
				irq_found = i;
			nr_irqs++;
		}
		irq_desc[i].status = status & ~IRQ_AUTODETECT;
		irq_desc[i].handler->shutdown(i);
	}
	spin_unlock_irq(&irq_controller_lock);

	if (nr_irqs > 1)
		irq_found = -irq_found;
	return irq_found;
}

void init_ISA_irqs (void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 0;

		if (i < 16) {
			/*
			 * 16 old-style INTA-cycle interrupts:
			 */
			irq_desc[i].handler = &i8259A_irq_type;
		} else {
			/*
			 * 'high' PCI IRQs filled in on demand
			 */
			irq_desc[i].handler = &no_irq_type;
		}
	}
}

__initfunc(void init_IRQ(void))
{
	int i;

#ifndef CONFIG_X86_VISWS_APIC
	init_ISA_irqs();
#else
	init_VISWS_APIC_irqs();
#endif
	/*
	 * Cover the whole vector space, no vector can escape
	 * us. (some of these will be overridden and become
	 * 'special' SMP interrupts)
	 */
	for (i = 0; i < NR_IRQS; i++) {
		int vector = FIRST_EXTERNAL_VECTOR + i;
		if (vector != SYSCALL_VECTOR) 
			set_intr_gate(vector, interrupt[i]);
	}

#ifdef __SMP__	

	/*
	  IRQ0 must be given a fixed assignment and initialized
	  before init_IRQ_SMP.
	*/
	set_intr_gate(IRQ0_TRAP_VECTOR, interrupt[0]);

	/*
	 * The reschedule interrupt is a CPU-to-CPU reschedule-helper
	 * IPI, driven by wakeup.
	 */
	set_intr_gate(RESCHEDULE_VECTOR, reschedule_interrupt);

	/* IPI for invalidation */
	set_intr_gate(INVALIDATE_TLB_VECTOR, invalidate_interrupt);

	/* IPI for CPU halt */
	set_intr_gate(STOP_CPU_VECTOR, stop_cpu_interrupt);

	/* self generated IPI for local APIC timer */
	set_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

	/* IPI for generic function call */
	set_intr_gate(CALL_FUNCTION_VECTOR, call_function_interrupt);

	/* IPI vector for APIC spurious interrupts */
	set_intr_gate(SPURIOUS_APIC_VECTOR, spurious_interrupt);
#endif

	/*
	 * Set the clock to 100 Hz, we already have a valid
	 * vector now:
	 */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */

#ifndef CONFIG_VISWS
	setup_x86_irq(2, &irq2);
	setup_x86_irq(13, &irq13);
#endif
}

#ifdef CONFIG_X86_IO_APIC
__initfunc(void init_IRQ_SMP(void))
{
	int i;
	for (i = 0; i < NR_IRQS ; i++)
		if (IO_APIC_VECTOR(i) > 0)
			set_intr_gate(IO_APIC_VECTOR(i), interrupt[i]);
}
#endif

