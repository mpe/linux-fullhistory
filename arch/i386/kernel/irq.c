/* mostly architecture independent
   some moved to i8259.c
   the beautiful visws architecture code needs to be updated too.
   and, finally, the BUILD_IRQ and SMP_BUILD macros in irq.h need fixed.
   */
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

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/desc.h>
#include <asm/irq.h>
#include <linux/irq.h>


unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

extern atomic_t nmi_counter[NR_CPUS];

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
spinlock_t irq_controller_lock = SPIN_LOCK_UNLOCKED;
/*
 * Controller mappings for all interrupt sources:
 */
irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned =
				{ [0 ... NR_IRQS-1] = { 0, &no_irq_type, }};

/*
 * Special irq handlers.
 */

void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * Generic no controller code
 */

static void enable_none(unsigned int irq) { }
static unsigned int startup_none(unsigned int irq) { return 0; }
static void disable_none(unsigned int irq) { }
static void ack_none(unsigned int irq)
{
/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves, it doesnt deserve
 * a generic callback i think.
 */
#if CONFIG_X86
	printk("unexpected IRQ trap at vector %02x\n", irq);
#ifdef __SMP__
	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 */
	ack_APIC_irq();
#endif
#endif
}

/* startup is the same as "enable", shutdown is same as "disable" */
#define shutdown_none	disable_none
#define end_none	enable_none

struct hw_interrupt_type no_irq_type = {
	"none",
	startup_none,
	shutdown_none,
	enable_none,
	disable_none,
	ack_none,
	end_none
};

volatile unsigned long irq_err_count;

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
		for (j = 0; j < smp_num_cpus; j++)
			p += sprintf(p, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#endif
		p += sprintf(p, " %14s", irq_desc[i].handler->typename);
		p += sprintf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			p += sprintf(p, ", %s", action->name);
		*p++ = '\n';
	}
	p += sprintf(p, "NMI: ");
	for (j = 0; j < smp_num_cpus; j++)
		p += sprintf(p, "%10u ",
			atomic_read(nmi_counter+cpu_logical_map(j)));
	p += sprintf(p, "\n");
#if CONFIG_SMP
	p += sprintf(p, "LOC: ");
	for (j = 0; j < smp_num_cpus; j++)
		p += sprintf(p, "%10u ",
			apic_timer_irqs[cpu_logical_map(j)]);
	p += sprintf(p, "\n");
#endif
	p += sprintf(p, "ERR: %10lu\n", irq_err_count);
	return p - buf;
}


/*
 * Global interrupt locks for SMP. Allow interrupts to come in on any
 * CPU, yet make cli/sti act globally to protect critical regions..
 */
spinlock_t i386_bh_lock = SPIN_LOCK_UNLOCKED;

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
		struct mm_struct *mm = current->mm;
		clear_bit(cpu, &smp_invalidate_needed);
		if (mm)
			atomic_set_mask(1 << cpu, &mm->cpu_vm_mask);
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
	stack = (unsigned long *) &stack;
	for (i = 40; i ; i--) {
		unsigned long x = *++stack;
		if (x > (unsigned long) &get_option && x < (unsigned long) &vsprintf) {
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
	case 1: {
		unsigned int status = irq_desc[irq].status & ~IRQ_DISABLED;
		irq_desc[irq].status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			irq_desc[irq].status = status | IRQ_REPLAY;
			hw_resend_irq(irq_desc[irq].handler,irq);
		}
		irq_desc[irq].handler->enable(irq);
		/* fall-through */
	}
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
asmlinkage unsigned int do_IRQ(struct pt_regs regs)
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
	int irq = regs.orig_eax & 0xff; /* high bits used in ret_from_ code  */
	int cpu = smp_processor_id();
	irq_desc_t *desc;
	struct irqaction * action;
	unsigned int status;

	kstat.irqs[cpu][irq]++;
	desc = irq_desc + irq;
	spin_lock(&irq_controller_lock);
	desc->handler->ack(irq);
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
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		status &= ~IRQ_PENDING; /* we commit to handling */
		status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;
	spin_unlock(&irq_controller_lock);

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	   Since we set PENDING, if another processor is handling
	   a different instance of this same irq, the other processor
	   will take care of it.
	 */
	if (!action)
		return 1;

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
		handle_IRQ_event(irq, &regs, action);
		spin_lock(&irq_controller_lock);
		
		if (!(desc->status & IRQ_PENDING))
			break;
		desc->status &= ~IRQ_PENDING;
		spin_unlock(&irq_controller_lock);
	}
	desc->status &= ~IRQ_INPROGRESS;
	if (!(desc->status & IRQ_DISABLED))
		desc->handler->end(irq);
	spin_unlock(&irq_controller_lock);

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
	return 1;
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

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

	spin_lock_irqsave(&irq_controller_lock,flags);
	p = &irq_desc[irq].action;
	for (;;) {
		struct irqaction * action = *p;
		if (action) {
			struct irqaction **pp = p;
			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found it - now remove it from the list of entries */
			*pp = action->next;
			if (!irq_desc[irq].action) {
				irq_desc[irq].status |= IRQ_DISABLED;
				irq_desc[irq].handler->shutdown(irq);
			}
			spin_unlock_irqrestore(&irq_controller_lock,flags);

			/* Wait to make sure it's not being used on another CPU */
			while (irq_desc[irq].status & IRQ_INPROGRESS)
				barrier();
			kfree(action);
			return;
		}
		printk("Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&irq_controller_lock,flags);
		return;
	}
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
			if(irq_desc[i].handler->startup(i))
				irq_desc[i].status |= IRQ_PENDING;
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

/* this was setup_x86_irq but it seems pretty generic */
int setup_irq(unsigned int irq, struct irqaction * new)
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

