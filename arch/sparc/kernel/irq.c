/*  $Id: irq.c,v 1.91 1998/10/14 07:04:17 jj Exp $
 *  arch/sparc/kernel/irq.c:  Interrupt request handling routines. On the
 *                            Sparc the IRQ's are basically 'cast in stone'
 *                            and you are supposed to probe the prom's device
 *                            node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@metabyte.com)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 *  Copyright (C) 1998 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#include <linux/config.h>
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
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/pcic.h>

/*
 * Dave Redman (djhr@tadpole.co.uk)
 *
 * IRQ numbers.. These are no longer restricted to 15..
 *
 * this is done to enable SBUS cards and onboard IO to be masked
 * correctly. using the interrupt level isn't good enough.
 *
 * For example:
 *   A device interrupting at sbus level6 and the Floppy both come in
 *   at IRQ11, but enabling and disabling them requires writing to
 *   different bits in the SLAVIO/SEC.
 *
 * As a result of these changes sun4m machines could now support
 * directed CPU interrupts using the existing enable/disable irq code
 * with tweaks.
 *
 */

static void irq_panic(void)
{
    extern char *cputypval;
    prom_printf("machine: %s doesn't have irq handlers defined!\n",cputypval);
    prom_halt();
}

void (*init_timers)(void (*)(int, void *,struct pt_regs *)) =
    (void (*)(void (*)(int, void *,struct pt_regs *))) irq_panic;

/*
 * Dave Redman (djhr@tadpole.co.uk)
 *
 * There used to be extern calls and hard coded values here.. very sucky!
 * instead, because some of the devices attach very early, I do something
 * equally sucky but at least we'll never try to free statically allocated
 * space or call kmalloc before kmalloc_init :(.
 * 
 * In fact it's the timer10 that attaches first.. then timer14
 * then kmalloc_init is called.. then the tty interrupts attach.
 * hmmm....
 *
 */
#define MAX_STATIC_ALLOC	4
struct irqaction static_irqaction[MAX_STATIC_ALLOC];
int static_irq_count = 0;

struct irqaction *irq_action[NR_IRQS+1] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;
#ifdef __SMP__
	int j;
#endif

	if (sparc_cpu_model == sun4d) {
		extern int sun4d_get_irq_list(char *);
		
		return sun4d_get_irq_list(buf);
	}
	for (i = 0 ; i < (NR_IRQS+1) ; i++) {
	        action = *(i + irq_action);
		if (!action) 
		        continue;
		len += sprintf(buf+len, "%3d: ", i);
#ifndef __SMP__
		len += sprintf(buf+len, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < smp_num_cpus; j++)
			len += sprintf(buf+len, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#endif
		len += sprintf(buf+len, " %c %s",
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action;
	struct irqaction * tmp = NULL;
        unsigned long flags;
	unsigned int cpu_irq;
	
	if (sparc_cpu_model == sun4d) {
		extern void sun4d_free_irq(unsigned int, void *);
		
		return sun4d_free_irq(irq, dev_id);
	}
	cpu_irq = irq & NR_IRQS;
	action = *(cpu_irq + irq_action);
        if (cpu_irq > 14) {  /* 14 irq levels on the sparc */
                printk("Trying to free bogus IRQ %d\n", irq);
                return;
        }
	if (!action->handler) {
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
		*(cpu_irq + irq_action) = action->next;

	kfree_s(action, sizeof(struct irqaction));

	if (!(*(cpu_irq + irq_action)))
		disable_irq(irq);

        restore_flags(flags);
}

/* Per-processor IRQ and bh locking depth, both SMP and non-SMP code use this. */
unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

#ifdef __SMP__
/* SMP interrupt locking on Sparc. */

/* Who has global_irq_lock. */
unsigned char global_irq_holder = NO_PROC_ID;

/* This protects IRQ's. */
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;

/* Global IRQ locking depth. */
atomic_t global_irq_count = ATOMIC_INIT(0);

atomic_t global_bh_count = ATOMIC_INIT(0);
atomic_t global_bh_lock = ATOMIC_INIT(0);

/* This protects BH software state (masks, things like that). */
spinlock_t sparc_bh_lock = SPIN_LOCK_UNLOCKED;

#ifdef DEBUG_IRQLOCK

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("wait_on_bh CPU#%d stuck at %08lx\n", cpu, where); stuck = INIT_STUCK; }

static inline void wait_on_bh(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;
	do {
		STUCK;
		/* nothing .. wait for the other bh's to go away */
	} while (atomic_read(&global_bh_count) != 0);
}

static unsigned long previous_irqholder;

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("wait_on_irq CPU#%d stuck at %08lx, waiting for %08lx (local=%d, global=%d)\n", cpu, where, previous_irqholder, local_count, atomic_read(&global_irq_count)); stuck = INIT_STUCK; }

/*
 * We have to allow irqs to arrive between __sti and __cli
 */
#define SYNC_OTHER_CORES(x) __asm__ __volatile__ ("nop")

static inline void wait_on_irq(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;
	int local_count = local_irq_count[cpu];

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
		spin_unlock(&global_irq_lock);

		for (;;) {
			STUCK;

			__sti();
			SYNC_OTHER_CORES(cpu);
			__cli();

			if (atomic_read(&global_irq_count))
				continue;
			if (*((unsigned char *)&global_irq_lock))
				continue;
			if (!local_bh_count[cpu] && atomic_read(&global_bh_count))
				continue;
			if (spin_trylock(&global_irq_lock))
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
	unsigned long where;

	__asm__("mov %%i7, %0" : "=r" (where));

	if (atomic_read(&global_bh_count) && !in_interrupt()) {
		int cpu = smp_processor_id();
		wait_on_bh(cpu, where);
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

#undef INIT_STUCK
#define INIT_STUCK 10000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("get_irqlock stuck at %08lx, waiting for %08lx\n", where, previous_irqholder); stuck = INIT_STUCK;}

static inline void get_irqlock(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;

	if (!spin_trylock(&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		do {
			do {
				STUCK;
				barrier();
			} while (*((volatile unsigned char *)&global_irq_lock));
		} while (!spin_trylock(&global_irq_lock));
	}
	/* 
	 * We also to make sure that nobody else is running
	 * in an interrupt context. 
	 */
	wait_on_irq(cpu, where);

	/*
	 * Ok, finally..
	 */
	global_irq_holder = cpu;
	previous_irqholder = where;
}

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
	unsigned long where;

	__asm__("mov %%i7, %0" : "=r" (where));

	__save_flags(flags);

	if ((flags & PSR_PIL) != PSR_PIL) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count[cpu])
			get_irqlock(cpu, where);
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
	int local_enabled = 0;
	unsigned long flags;

	__save_flags(flags);

	if ((flags & PSR_PIL) != PSR_PIL)
		local_enabled = 1;

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

#undef INIT_STUCK
#define INIT_STUCK 200000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("irq_enter stuck (irq=%d, cpu=%d, global=%d)\n",irq,cpu,global_irq_holder); stuck = INIT_STUCK;}

#define VERBOSE_IRQLOCK_DEBUGGING

void irq_enter(int cpu, int irq, void *_opaque)
{
#ifdef VERBOSE_IRQLOCK_DEBUGGING
	extern void smp_show_backtrace_all_cpus(void);
#endif
	int stuck = INIT_STUCK;

	hardirq_enter(cpu);
	barrier();
	while (*((volatile unsigned char *)&global_irq_lock)) {
		if ((unsigned char) cpu == global_irq_holder) {
			struct pt_regs *regs = _opaque;
			int sbh_cnt = atomic_read(&global_bh_count);
			int globl_locked = *((unsigned char *)&global_irq_lock);
			int globl_icount = atomic_read(&global_irq_count);
			int local_count = local_irq_count[cpu];
			unsigned long pc = regs->pc;

			/* It is very important that we load the state variables
			 * before we do the first call to printk() as printk()
			 * could end up changing them...
			 */

			printk("CPU[%d]: BAD! Local IRQ's enabled, global disabled "
			       "interrupt at PC[%08lx]\n", cpu, pc);
			printk("CPU[%d]: bhcnt[%d] glocked[%d] gicnt[%d] licnt[%d]\n",
			       cpu, sbh_cnt, globl_locked, globl_icount, local_count);
#ifdef VERBOSE_IRQLOCK_DEBUGGING
			printk("Performing backtrace on all cpus, write this down!\n");
			smp_show_backtrace_all_cpus();
#endif
			break;
		}
		STUCK;
		barrier();
	}
}

void irq_exit(int cpu, int irq)
{
	hardirq_exit(cpu);
	release_irqlock(cpu);
}

#endif /* DEBUG_IRQLOCK */
#endif /* __SMP__ */

void unexpected_irq(int irq, void *dev_id, struct pt_regs * regs)
{
        int i;
	struct irqaction * action;
	unsigned int cpu_irq;
	
	cpu_irq = irq & NR_IRQS;
	action = *(cpu_irq + irq_action);

        printk("IO device interrupt, irq = %d\n", irq);
        printk("PC = %08lx NPC = %08lx FP=%08lx\n", regs->pc, 
		    regs->npc, regs->u_regs[14]);
	if (action) {
		printk("Expecting: ");
        	for (i = 0; i < 16; i++)
                	if (action->handler)
                        	prom_printf("[%s:%d:0x%x] ", action->name,
				    (int) i, (unsigned int) action->handler);
	}
        printk("AIEEE\n");
	panic("bogus interrupt received");
}

void handler_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int cpu = smp_processor_id();
#ifdef __SMP__
	extern void smp4m_irq_rotate(int cpu);
#endif

	disable_pil_irq(irq);
#if 0 /* FIXME: rotating IRQs halts the machine during SCSI probe. -ecd */
#ifdef __SMP__
	/* Only rotate on lower priority IRQ's (scsi, ethernet, etc.). */
	if(irq < 10)
		smp4m_irq_rotate(cpu);
#endif
#endif
	irq_enter(cpu, irq, regs);
	action = *(irq + irq_action);
	kstat.irqs[cpu][irq]++;
	do {
		if (!action || !action->handler)
			unexpected_irq(irq, 0, regs);
		action->handler(irq, action->dev_id, regs);
		action = action->next;
	} while (action);
	irq_exit(cpu, irq);
	enable_pil_irq(irq);
}

#ifdef CONFIG_BLK_DEV_FD
extern void floppy_interrupt(int irq, void *dev_id, struct pt_regs *regs);

void sparc_floppy_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	disable_pil_irq(irq);
	irq_enter(cpu, irq, regs);
	kstat.irqs[cpu][irq]++;
	floppy_interrupt(irq, dev_id, regs);
	irq_exit(cpu, irq);
	enable_pil_irq(irq);
}
#endif

/* Fast IRQ's on the Sparc can only have one routine attached to them,
 * thus no sharing possible.
 */
int request_fast_irq(unsigned int irq,
		     void (*handler)(int, void *, struct pt_regs *),
		     unsigned long irqflags, const char *devname)
{
	struct irqaction *action;
	unsigned long flags;
	unsigned int cpu_irq;
#ifdef __SMP__
	struct tt_entry *trap_table;
	extern struct tt_entry trapbase_cpu1, trapbase_cpu2, trapbase_cpu3;
#endif
	
	cpu_irq = irq & NR_IRQS;
	if(cpu_irq > 14)
		return -EINVAL;
	if(!handler)
		return -EINVAL;
	action = *(cpu_irq + irq_action);
	if(action) {
		if(action->flags & SA_SHIRQ)
			panic("Trying to register fast irq when already shared.\n");
		if(irqflags & SA_SHIRQ)
			panic("Trying to register fast irq as shared.\n");

		/* Anyway, someone already owns it so cannot be made fast. */
		printk("request_fast_irq: Trying to register yet already owned.\n");
		return -EBUSY;
	}

	save_and_cli(flags);

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC) {
	    if (static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Fast IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n",
		       irq, devname);
	}
	
	if (action == NULL)
	    action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						 GFP_KERNEL);
	
	if (!action) { 
		restore_flags(flags);
		return -ENOMEM;
	}

	/* Dork with trap table if we get this far. */
#define INSTANTIATE(table) \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_one = SPARC_RD_PSR_L0; \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two = \
		SPARC_BRANCH((unsigned long) handler, \
			     (unsigned long) &table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two);\
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_three = SPARC_RD_WIM_L3; \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_four = SPARC_NOP;

	INSTANTIATE(sparc_ttable)
#ifdef __SMP__
	trap_table = &trapbase_cpu1; INSTANTIATE(trap_table)
	trap_table = &trapbase_cpu2; INSTANTIATE(trap_table)
	trap_table = &trapbase_cpu3; INSTANTIATE(trap_table)
#endif
#undef INSTANTIATE
	/*
	 * XXX Correct thing whould be to flush only I- and D-cache lines
	 * which contain the handler in question. But as of time of the
	 * writing we have no CPU-neutral interface to fine-grained flushes.
	 */
	flush_cache_all();

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->dev_id = NULL;
	action->next = NULL;

	*(cpu_irq + irq_action) = action;

	enable_irq(irq);
	restore_flags(flags);
	return 0;
}

int request_irq(unsigned int irq,
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction * action, *tmp = NULL;
	unsigned long flags;
	unsigned int cpu_irq;
	
	if (sparc_cpu_model == sun4d) {
		extern int sun4d_request_irq(unsigned int, 
					     void (*)(int, void *, struct pt_regs *),
					     unsigned long, const char *, void *);
		return sun4d_request_irq(irq, handler, irqflags, devname, dev_id);
	}
	cpu_irq = irq & NR_IRQS;
	if(cpu_irq > 14)
		return -EINVAL;

	if (!handler)
	    return -EINVAL;
	    
	action = *(cpu_irq + irq_action);
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
	if (irqflags & SA_STATIC_ALLOC) {
	    if (static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n",irq, devname);
	}
	
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
		*(cpu_irq + irq_action) = action;

	enable_irq(irq);
	restore_flags(flags);
	return 0;
}

/* We really don't need these at all on the Sparc.  We only have
 * stubs here because they are exported to modules.
 */
unsigned long probe_irq_on(void)
{
	return 0;
}

int probe_irq_off(unsigned long mask)
{
	return 0;
}

/* djhr
 * This could probably be made indirect too and assigned in the CPU
 * bits of the code. That would be much nicer I think and would also
 * fit in with the idea of being able to tune your kernel for your machine
 * by removing unrequired machine and device support.
 *
 */

__initfunc(void init_IRQ(void))
{
	extern void sun4c_init_IRQ( void );
	extern void sun4m_init_IRQ( void );
	extern void sun4d_init_IRQ( void );
    
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4:
		sun4c_init_IRQ();
		break;

	case sun4m:
#ifdef CONFIG_PCI
		pcic_probe();
		if (pci_present()) {
			sun4m_pci_init_IRQ();
			break;
		}
#endif
		sun4m_init_IRQ();
		break;
		
	case sun4d:
		sun4d_init_IRQ();
		break;

	case ap1000:
#if CONFIG_AP1000
		ap_init_IRQ();;
		break;
#endif

	default:
		prom_printf("Cannot initialize IRQ's on this Sun machine...");
		break;
	}
	btfixup();
}
