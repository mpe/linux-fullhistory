/*  $Id: irq.c,v 1.44 1996/04/25 06:08:46 davem Exp $
 *  arch/sparc/kernel/irq.c:  Interrupt request handling routines. On the
 *                            Sparc the IRQ's are basically 'cast in stone'
 *                            and you are supposed to probe the prom's device
 *                            node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@ipmce.su)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
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

void (*enable_irq)(unsigned int) = (void (*)(unsigned int)) irq_panic;
void (*disable_irq)(unsigned int) = (void (*)(unsigned int)) irq_panic;
void (*clear_clock_irq)( void ) = irq_panic;
void (*clear_profile_irq)( void ) = irq_panic;
void (*load_profile_irq)( unsigned int ) =  (void (*)(unsigned int)) irq_panic;
void (*init_timers)( void (*)(int, void *,struct pt_regs *)) =
    (void (*)( void (*)(int, void *,struct pt_regs *))) irq_panic;

#ifdef __SMP__
void (*set_cpu_int)(int, int);
void (*clear_cpu_int)(int, int);
void (*set_irq_udt)(int);
#endif

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
static struct irqaction static_irqaction[MAX_STATIC_ALLOC];
static int static_irq_count = 0;

static struct irqaction *irq_action[NR_IRQS+1] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < (NR_IRQS+1) ; i++) {
	        action = *(i + irq_action);
		if (!action) 
		        continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
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
	return len;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action;
	struct irqaction * tmp = NULL;
        unsigned long flags;
	unsigned int cpu_irq;
	
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
			if (action->dev_id == dev_id) break;
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
	
        save_flags(flags); cli();
	if (action && tmp)
		tmp->next = action->next;
	else
		*(cpu_irq + irq_action) = action->next;

	kfree_s(action, sizeof(struct irqaction));

	if (!(*(cpu_irq + irq_action)))
		disable_irq(irq);

        restore_flags(flags);
}

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
        printk("Expecting: ");
        for (i = 0; i < 16; i++)
                if (action->handler)
                        prom_printf("[%s:%d:0x%x] ", action->name, (int) i,
				    (unsigned int) action->handler);
        printk("AIEEE\n");
	panic("bogus interrupt received");
}

void handler_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	unsigned int cpu_irq;
	
	cpu_irq = irq & NR_IRQS;
	action = *(cpu_irq + irq_action);
	kstat.interrupts[cpu_irq]++;
#if 0
	printk("I<%d,%d,%d>", smp_processor_id(), irq, smp_proc_in_lock[smp_processor_id()]);
#endif
	while (action) {
		if (!action->handler)
			unexpected_irq(irq, action->dev_id, regs);
		else
			action->handler(irq, action->dev_id, regs);
		action = action->next;
	}
}

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
	unsigned int cpu_irq;
	
	cpu_irq = irq & NR_IRQS;
	action = *(cpu_irq + irq_action);
	kstat.interrupts[cpu_irq]++;
	while (action) {
		action->handler(irq, action->dev_id, regs);
		action = action->next;
	}
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
	kstat.interrupts[irq&NR_IRQS]++;
	printk("Got FAST_IRQ number %04lx\n", (long unsigned int) irq);
	return;
}

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
		return -EBUSY;
	}

	save_flags(flags); cli();

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC)
	    if (static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Fast IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n",
		       irq, devname);
	
	if (action == NULL)
	    action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						 GFP_KERNEL);
	
	if (!action) { 
		restore_flags(flags);
		return -ENOMEM;
	}

	/* Dork with trap table if we get this far. */
	sparc_ttable[SP_TRAP_IRQ1+(cpu_irq-1)].inst_one =
		SPARC_BRANCH((unsigned long) handler,
			     (unsigned long) &sparc_ttable[SP_TRAP_IRQ1+(irq-1)].inst_one);
	sparc_ttable[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two = SPARC_RD_PSR_L0;
	sparc_ttable[SP_TRAP_IRQ1+(cpu_irq-1)].inst_three = SPARC_NOP;
	sparc_ttable[SP_TRAP_IRQ1+(cpu_irq-1)].inst_four = SPARC_NOP;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->dev_id = NULL;

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
	}

	save_flags(flags); cli();

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

void init_IRQ(void)
{
	extern void sun4c_init_IRQ( void );
	extern void sun4m_init_IRQ( void );
#if CONFIG_AP1000
	extern void ap_init_IRQ(void);
	ap_init_IRQ();
	return;
#endif
    
	switch(sparc_cpu_model) {
	case sun4c:
		sun4c_init_IRQ();
		break;

	case sun4m:
		sun4m_init_IRQ();
		break;

	default:
		prom_printf("Cannot initialize IRQ's on this Sun machine...");
		break;
	}
}
