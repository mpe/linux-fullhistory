/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/machvec.h>

#include "proto.h"

#define vulp	volatile unsigned long *
#define vuip	volatile unsigned int *

/* Only uniprocessor needs this IRQ/BH locking depth, on SMP it lives
   in the per-cpu structure for cache reasons.  */
#ifndef __SMP__
int __local_irq_count;
int __local_bh_count;
#endif

#if NR_IRQS > 128
#  error Unable to handle more than 128 irq levels.
#endif

#ifdef CONFIG_ALPHA_GENERIC
#define ACTUAL_NR_IRQS	alpha_mv.nr_irqs
#else
#define ACTUAL_NR_IRQS	NR_IRQS
#endif

/* Reserved interrupts.  These must NEVER be requested by any driver!
   IRQ 2 used by hw cascade */
#define	IS_RESERVED_IRQ(irq)	((irq)==2)


/*
 * Shadow-copy of masked interrupts.
 */

unsigned long _alpha_irq_masks[2] = { ~0UL, ~0UL };

/*
 * The ack_irq routine used by 80% of the systems.
 */

void
common_ack_irq(unsigned long irq)
{
	if (irq < 16) {
		/* Ack the interrupt making it the lowest priority */
		/*  First the slave .. */
		if (irq > 7) {
			outb(0xE0 | (irq - 8), 0xa0);
			irq = 2;
		}
		/* .. then the master */
		outb(0xE0 | irq, 0x20);
	}
}



static void dummy_perf(unsigned long vector, struct pt_regs *regs)
{
        printk(KERN_CRIT "Performance counter interrupt!\n");
}

void (*perf_irq)(unsigned long, struct pt_regs *) = dummy_perf;

/*
 * Dispatch device interrupts.
 */

/* Handle ISA interrupt via the PICs. */

#if defined(CONFIG_ALPHA_GENERIC)
# define IACK_SC	alpha_mv.iack_sc
#elif defined(CONFIG_ALPHA_APECS)
# define IACK_SC	APECS_IACK_SC
#elif defined(CONFIG_ALPHA_LCA)
# define IACK_SC	LCA_IACK_SC
#elif defined(CONFIG_ALPHA_CIA)
# define IACK_SC	CIA_IACK_SC
#elif defined(CONFIG_ALPHA_PYXIS)
# define IACK_SC	PYXIS_IACK_SC
#elif defined(CONFIG_ALPHA_TSUNAMI)
# define IACK_SC	TSUNAMI_IACK_SC
#elif defined(CONFIG_ALPHA_POLARIS)
# define IACK_SC	POLARIS_IACK_SC
#elif defined(CONFIG_ALPHA_IRONGATE)
# define IACK_SC        IRONGATE_IACK_SC
#else
  /* This is bogus but necessary to get it to compile on all platforms. */
# define IACK_SC	1L
#endif

void
isa_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
#if 1
	/*
	 * Generate a PCI interrupt acknowledge cycle.  The PIC will
	 * respond with the interrupt vector of the highest priority
	 * interrupt that is pending.  The PALcode sets up the
	 * interrupts vectors such that irq level L generates vector L.
	 */
	int j = *(vuip) IACK_SC;
	j &= 0xff;
	if (j == 7) {
		if (!(inb(0x20) & 0x80)) {
			/* It's only a passive release... */
			return;
		}
	}
	handle_irq(j, j, regs);
#else
	unsigned long pic;

	/*
	 * It seems to me that the probability of two or more *device*
	 * interrupts occurring at almost exactly the same time is
	 * pretty low.  So why pay the price of checking for
	 * additional interrupts here if the common case can be
	 * handled so much easier?
	 */
	/* 
	 *  The first read of gives you *all* interrupting lines.
	 *  Therefore, read the mask register and and out those lines
	 *  not enabled.  Note that some documentation has 21 and a1 
	 *  write only.  This is not true.
	 */
	pic = inb(0x20) | (inb(0xA0) << 8);	/* read isr */
	pic &= ~alpha_irq_mask;			/* apply mask */
	pic &= 0xFFFB;				/* mask out cascade & hibits */

	while (pic) {
		int j = ffz(~pic);
		pic &= pic - 1;
		handle_irq(j, j, regs);
	}
#endif
}

/* Handle interrupts from the SRM, assuming no additional weirdness.  */

void 
srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;
	handle_irq(irq, ack, regs);
}


/*
 * Initial irq handlers.
 */

static struct irqaction timer_irq = { NULL, 0, 0, NULL, NULL, NULL};
spinlock_t irq_controller_lock = SPIN_LOCK_UNLOCKED;
irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned = { [0 ... NR_IRQS-1] = {0,} };


static inline void
mask_irq(unsigned long irq)
{
	set_bit(irq, _alpha_irq_masks);
	alpha_mv.update_irq_hw(irq, alpha_irq_mask, 0);
}

static inline void
unmask_irq(unsigned long irq)
{
	clear_bit(irq, _alpha_irq_masks);
	alpha_mv.update_irq_hw(irq, alpha_irq_mask, 1);
}

void
disable_irq_nosync(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	mask_irq(irq_nr);
	restore_flags(flags);
}

void
disable_irq(unsigned int irq_nr)
{
	/* This works non-SMP, and SMP until we write code to distribute
	   interrupts to more that cpu 0.  */
	disable_irq_nosync(irq_nr);
}

void
enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	unmask_irq(irq_nr);
	restore_flags(flags);
}

int
check_irq(unsigned int irq)
{
	return irq_desc[irq].action ? -EBUSY : 0;
}

int
request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	    unsigned long irqflags, const char * devname, void *dev_id)
{
	int shared = 0;
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= ACTUAL_NR_IRQS)
		return -EINVAL;
	if (IS_RESERVED_IRQ(irq))
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	p = &irq_desc[irq].action;
	action = *p;
	if (action) {
		/* Can't share interrupts unless both agree to */
		if (!(action->flags & irqflags & SA_SHIRQ))
			return -EBUSY;

		/* Can't share interrupts unless both are same type */
		if ((action->flags ^ irqflags) & SA_INTERRUPT)
			return -EBUSY;

		/* Add new interrupt at end of irq queue */
		do {
			p = &action->next;
			action = *p;
		} while (action);
		shared = 1;
	}

	action = &timer_irq;
	if (irq != TIMER_IRQ) {
		action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	}
	if (!action)
		return -ENOMEM;

	if (irqflags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	save_and_cli(flags);
	*p = action;

	if (!shared)
		unmask_irq(irq);

	restore_flags(flags);
	return 0;
}
		
void
free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= ACTUAL_NR_IRQS) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (IS_RESERVED_IRQ(irq)) {
		printk("Trying to free reserved IRQ %d\n", irq);
		return;
	}
	for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		if (!irq_desc[irq].action)
			mask_irq(irq);
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

int get_irq_list(char *buf)
{
	int i;
	struct irqaction * action;
	char *p = buf;

#ifdef __SMP__
	p += sprintf(p, "           ");
	for (i = 0; i < smp_num_cpus; i++)
		p += sprintf(p, "CPU%d       ", i);
	*p++ = '\n';
#endif

	for (i = 0; i < NR_IRQS; i++) {
		action = irq_desc[i].action;
		if (!action) 
			continue;
		p += sprintf(p, "%3d: ",i);
#ifndef __SMP__
		p += sprintf(p, "%10u ", kstat_irqs(i));
#else
		{
		  int j;
		  for (j = 0; j < smp_num_cpus; j++)
			  p += sprintf(p, "%10u ",
				       kstat.irqs[cpu_logical_map(j)][i]);
		}
#endif
		p += sprintf(p, "  %c%s",
			     (action->flags & SA_INTERRUPT)?'+':' ',
			     action->name);

		for (action=action->next; action; action = action->next) {
			p += sprintf(p, ", %c%s",
				     (action->flags & SA_INTERRUPT)?'+':' ',
				     action->name);
		}
		*p++ = '\n';
	}
	return p - buf;
}

#ifdef __SMP__
/* Who has global_irq_lock. */
int global_irq_holder = NO_PROC_ID;

/* This protects IRQ's. */
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;

/* Global IRQ locking depth. */
atomic_t global_irq_count = ATOMIC_INIT(0);

/* This protects BH software state (masks, things like that). */
atomic_t global_bh_lock = ATOMIC_INIT(0);
atomic_t global_bh_count = ATOMIC_INIT(0);

static void *previous_irqholder = NULL;

#define MAXCOUNT 100000000

static void show(char * str, void *where);

static inline void
wait_on_irq(int cpu, void *where)
{
	int count = MAXCOUNT;

	for (;;) {

		/*
		 * Wait until all interrupts are gone. Wait
		 * for bottom half handlers unless we're
		 * already executing in one..
		 */
		if (!atomic_read(&global_irq_count)) {
			if (local_bh_count(cpu)
			    || !atomic_read(&global_bh_count))
				break;
		}

		/* Duh, we have to loop. Release the lock to avoid deadlocks */
		spin_unlock(&global_irq_lock);

		for (;;) {
			if (!--count) {
				show("wait_on_irq", where);
				count = MAXCOUNT;
			}
			__sti();
			udelay(1); /* make sure to run pending irqs */
			__cli();

			if (atomic_read(&global_irq_count))
				continue;
			if (spin_is_locked(&global_irq_lock))
				continue;
			if (!local_bh_count(cpu)
			    && atomic_read(&global_bh_count))
				continue;
			if (spin_trylock(&global_irq_lock))
				break;
		}
	}
}

static inline void
get_irqlock(int cpu, void* where)
{
	if (!spin_trylock(&global_irq_lock)) {
		/* Do we already hold the lock?  */
		if (cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it.  Wait.  */
		spin_lock(&global_irq_lock);
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
#if DEBUG_SPINLOCK
	global_irq_lock.task = current;
	global_irq_lock.previous = where;
#endif
	global_irq_holder = cpu;
	previous_irqholder = where;
}

void
__global_cli(void)
{
	int cpu = smp_processor_id();
	void *where = __builtin_return_address(0);

	/*
	 * Maximize ipl.  If ipl was previously 0 and if this thread
	 * is not in an irq, then take global_irq_lock.
	 */
	if (swpipl(7) == 0 && !local_irq_count(cpu))
		get_irqlock(cpu, where);
}

void
__global_sti(void)
{
        int cpu = smp_processor_id();

        if (!local_irq_count(cpu))
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
unsigned long
__global_save_flags(void)
{
        int retval;
        int local_enabled;
        unsigned long flags;
	int cpu = smp_processor_id();

        __save_flags(flags);
        local_enabled = (!(flags & 7));
        /* default to local */
        retval = 2 + local_enabled;

        /* Check for global flags if we're not in an interrupt.  */
        if (!local_irq_count(cpu)) {
                if (local_enabled)
                        retval = 1;
                if (global_irq_holder == cpu)
                        retval = 0;
	}
	return retval;
}

void
__global_restore_flags(unsigned long flags)
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
                printk("global_restore_flags: %08lx (%p)\n",
                        flags, __builtin_return_address(0));
        }
}

static void
show(char * str, void *where)
{
#if 0
	int i;
        unsigned long *stack;
#endif
        int cpu = smp_processor_id();

        printk("\n%s, CPU %d: %p\n", str, cpu, where);
        printk("irq:  %d [%d %d]\n",
	       atomic_read(&global_irq_count),
               cpu_data[0].irq_count,
	       cpu_data[1].irq_count);

        printk("bh:   %d [%d %d]\n",
	       atomic_read(&global_bh_count),
	       cpu_data[0].bh_count,
	       cpu_data[1].bh_count);
#if 0
        stack = (unsigned long *) &str;
        for (i = 40; i ; i--) {
		unsigned long x = *++stack;
                if (x > (unsigned long) &init_task_union &&
		    x < (unsigned long) &vsprintf) {
			printk("<[%08lx]> ", x);
                }
        }
#endif
}
        
static inline void
wait_on_bh(void)
{
	int count = MAXCOUNT;
        do {
		if (!--count) {
			show("wait_on_bh", 0);
                        count = ~0;
                }
                /* nothing .. wait for the other bh's to go away */
		barrier();
        } while (atomic_read(&global_bh_count) != 0);
}

/*
 * This is called when we want to synchronize with
 * bottom half handlers. We need to wait until
 * no other CPU is executing any bottom half handler.
 *
 * Don't wait if we're already running in an interrupt
 * context or are inside a bh handler.
 */
void
synchronize_bh(void)
{
	if (atomic_read(&global_bh_count) && !in_interrupt())
		wait_on_bh();
}

/*
 * From its use, I infer that synchronize_irq() stalls a thread until
 * the effects of a command to an external device are known to have
 * taken hold.  Typically, the command is to stop sending interrupts.
 * The strategy here is wait until there is at most one processor
 * (this one) in an irq.  The memory barrier serializes the write to
 * the device and the subsequent accesses of global_irq_count.
 * --jmartin
 */
#define DEBUG_SYNCHRONIZE_IRQ 0

void
synchronize_irq(void)
{
#if 0
	/* Joe's version.  */
	int cpu = smp_processor_id();
	int local_count;
	int global_count;
	int countdown = 1<<24;
	void *where = __builtin_return_address(0);

	mb();
	do {
		local_count = local_irq_count(cpu);
		global_count = atomic_read(&global_irq_count);
		if (DEBUG_SYNCHRONIZE_IRQ && (--countdown == 0)) {
			printk("%d:%d/%d\n", cpu, local_count, global_count);
			show("synchronize_irq", where);
			break;
		}
	} while (global_count != local_count);
#else
	/* Jay's version.  */
	if (atomic_read(&global_irq_count)) {
		cli();
		sti();
	}
#endif
}
#endif /* __SMP__ */

static void
unexpected_irq(int irq, struct pt_regs * regs)
{
#if 0
#if 1
	printk("device_interrupt: unexpected interrupt %d\n", irq);
#else
	struct irqaction *action;
	int i;

	printk("IO device interrupt, irq = %d\n", irq);
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
	printk("Expecting: ");
	for (i = 0; i < ACTUAL_NR_IRQS; i++)
		if ((action = irq_desc[i].action))
			while (action->handler) {
				printk("[%s:%d] ", action->name, i);
				action = action->next;
			}
	printk("\n");
#endif
#endif

#if defined(CONFIG_ALPHA_JENSEN)
	/* ??? Is all this just debugging, or are the inb's and outb's
	   necessary to make things work?  */
	printk("64=%02x, 60=%02x, 3fa=%02x 2fa=%02x\n",
	       inb(0x64), inb(0x60), inb(0x3fa), inb(0x2fa));
	outb(0x0c, 0x3fc);
	outb(0x0c, 0x2fc);
	outb(0,0x61);
	outb(0,0x461);
#endif
}

void
handle_irq(int irq, int ack, struct pt_regs * regs)
{
	struct irqaction * action;
	int cpu = smp_processor_id();

	if ((unsigned) irq > ACTUAL_NR_IRQS) {
		printk("device_interrupt: illegal interrupt %d\n", irq);
		return;
	}

#if 0
	/* A useful bit of code to find out if an interrupt is going wild.  */
	{
	  static unsigned int last_msg, last_cc;
	  static int last_irq, count;
	  unsigned int cc;

	  __asm __volatile("rpcc %0" : "=r"(cc));
	  ++count;
	  if (cc - last_msg > 150000000 || irq != last_irq) {
		printk("handle_irq: irq %d count %d cc %u @ %p\n",
		       irq, count, cc-last_cc, regs->pc);
		count = 0;
		last_msg = cc;
		last_irq = irq;
	  }
	  last_cc = cc;
	}
#endif

	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq] += 1;
	action = irq_desc[irq].action;

	/*
	 * For normal interrupts, we mask it out, and then ACK it.
	 * This way another (more timing-critical) interrupt can
	 * come through while we're doing this one.
	 *
	 * Note! An irq without a handler gets masked and acked, but
	 * never unmasked. The autoirq stuff depends on this (it looks
	 * at the masks before and after doing the probing).
	 */
	if (ack >= 0) {
		mask_irq(ack);
		alpha_mv.ack_irq(ack);
	}
	if (action) {
		if (action->flags & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
		if (ack >= 0)
			unmask_irq(ack);
	} else {
		unexpected_irq(irq, regs);
	}
	irq_exit(cpu, irq);
}


/*
 * Start listening for interrupts..
 */

unsigned long
probe_irq_on(void)
{
	struct irqaction * action;
	unsigned long irqs = 0;
	unsigned long delay;
	unsigned int i;

	/* Handle only the first 64 IRQs here.  This is enough for
	   [E]ISA, which is the only thing that needs probing anyway.  */
	for (i = (ACTUAL_NR_IRQS - 1) & 63; i > 0; i--) {
		if (!(PROBE_MASK & (1UL << i))) {
			continue;
		}
		action = irq_desc[i].action;
		if (!action) {
			enable_irq(i);
			irqs |= (1UL << i);
		}
	}

	/*
	 * Wait about 100ms for spurious interrupts to mask themselves
	 * out again...
	 */
	for (delay = jiffies + HZ/10; time_before(jiffies, delay); )
		barrier();

	/* Now filter out any obviously spurious interrupts.  */
	return irqs & ~alpha_irq_mask;
}

/*
 * Get the result of the IRQ probe.. A negative result means that
 * we have several candidates (but we return the lowest-numbered
 * one).
 */

int
probe_irq_off(unsigned long irqs)
{
	int i;
	
	/* Handle only the first 64 IRQs here.  This is enough for
	   [E]ISA, which is the only thing that needs probing anyway.  */
        irqs &= alpha_irq_mask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (1UL << i))
		i = -i;
	return i;
}


/*
 * The main interrupt entry point.
 */

asmlinkage void 
do_entInt(unsigned long type, unsigned long vector, unsigned long la_ptr,
	  unsigned long a3, unsigned long a4, unsigned long a5,
	  struct pt_regs regs)
{
	switch (type) {
	case 0:
#ifdef __SMP__
		handle_ipi(&regs);
		return;
#else
		printk("Interprocessor interrupt? You must be kidding\n");
#endif
		break;
	case 1:
		handle_irq(RTC_IRQ, -1, &regs);
		return;
	case 2:
		alpha_mv.machine_check(vector, la_ptr, &regs);
		return;
	case 3:
		alpha_mv.device_interrupt(vector, &regs);
		return;
	case 4:
		perf_irq(vector, &regs);
		return;
	default:
		printk("Hardware intr %ld %lx? Huh?\n", type, vector);
	}
	printk("PC = %016lx PS=%04lx\n", regs.pc, regs.ps);
}

void __init
init_IRQ(void)
{
	wrent(entInt, 0);
	alpha_mv.init_irq();
}


/*
 */
#define MCHK_K_TPERR           0x0080
#define MCHK_K_TCPERR          0x0082
#define MCHK_K_HERR            0x0084
#define MCHK_K_ECC_C           0x0086
#define MCHK_K_ECC_NC          0x0088
#define MCHK_K_OS_BUGCHECK     0x008A
#define MCHK_K_PAL_BUGCHECK    0x0090

#ifndef __SMP__
struct mcheck_info __mcheck_info;
#endif

void
process_mcheck_info(unsigned long vector, unsigned long la_ptr,
		    struct pt_regs *regs, const char *machine,
		    int expected)
{
	struct el_common *mchk_header;
	const char *reason;

	/*
	 * See if the machine check is due to a badaddr() and if so,
	 * ignore it.
	 */

#if DEBUG_MCHECK > 0
	 printk(KERN_CRIT "%s machine check %s\n", machine,
	        expected ? "expected." : "NOT expected!!!");
#endif

	if (expected) {
		int cpu = smp_processor_id();
		mcheck_expected(cpu) = 0;
		mcheck_taken(cpu) = 1;
		return;
	}

	mchk_header = (struct el_common *)la_ptr;

	printk(KERN_CRIT "%s machine check: vector=0x%lx pc=0x%lx code=0x%lx\n",
	       machine, vector, regs->pc, mchk_header->code);

	switch ((unsigned int) mchk_header->code) {
	/* Machine check reasons.  Defined according to PALcode sources.  */
	case 0x80: reason = "tag parity error"; break;
	case 0x82: reason = "tag control parity error"; break;
	case 0x84: reason = "generic hard error"; break;
	case 0x86: reason = "correctable ECC error"; break;
	case 0x88: reason = "uncorrectable ECC error"; break;
	case 0x8A: reason = "OS-specific PAL bugcheck"; break;
	case 0x90: reason = "callsys in kernel mode"; break;
	case 0x96: reason = "i-cache read retryable error"; break;
	case 0x98: reason = "processor detected hard error"; break;
	
	/* System specific (these are for Alcor, at least): */
	case 0x202: reason = "system detected hard error"; break;
	case 0x203: reason = "system detected uncorrectable ECC error"; break;
	case 0x204: reason = "SIO SERR occurred on PCI bus"; break;
	case 0x205: reason = "parity error detected by CIA"; break;
	case 0x206: reason = "SIO IOCHK occurred on ISA bus"; break;
	case 0x207: reason = "non-existent memory error"; break;
	case 0x208: reason = "MCHK_K_DCSR"; break;
	case 0x209: reason = "PCI SERR detected"; break;
	case 0x20b: reason = "PCI data parity error detected"; break;
	case 0x20d: reason = "PCI address parity error detected"; break;
	case 0x20f: reason = "PCI master abort error"; break;
	case 0x211: reason = "PCI target abort error"; break;
	case 0x213: reason = "scatter/gather PTE invalid error"; break;
	case 0x215: reason = "flash ROM write error"; break;
	case 0x217: reason = "IOA timeout detected"; break;
	case 0x219: reason = "IOCHK#, EISA add-in board parity or other catastrophic error"; break;
	case 0x21b: reason = "EISA fail-safe timer timeout"; break;
	case 0x21d: reason = "EISA bus time-out"; break;
	case 0x21f: reason = "EISA software generated NMI"; break;
	case 0x221: reason = "unexpected ev5 IRQ[3] interrupt"; break;
	default: reason = "unknown"; break;
	}

	printk(KERN_CRIT "machine check type: %s%s\n",
	       reason, mchk_header->retry ? " (retryable)" : "");

	dik_show_regs(regs, NULL);

#if DEBUG_MCHECK > 1
	{
		/* Dump the logout area to give all info.  */
		unsigned long *ptr = (unsigned long *)la_ptr;
		long i;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(KERN_CRIT "   +%8lx %016lx %016lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
	}
#endif
}
