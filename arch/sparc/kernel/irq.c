/*  $Id: irq.c,v 1.34 1996/02/20 07:45:04 davem Exp $
 *  arch/sparc/kernel/irq.c:  Interrupt request handling routines. On the
 *                            Sparc the IRQ's are basically 'cast in stone'
 *                            and you are supposed to probe the prom's device
 *                            node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@jamica.lab.ipmce.su)
 */

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
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>

/* Pointer to the interrupt enable byte */
unsigned char *interrupt_enable = 0;
struct sun4m_intregs *sun4m_interrupts;

void sun4c_disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;

	if(sparc_cpu_model != sun4c)
		return;
	save_flags(flags); cli();
	current_mask = *interrupt_enable;
	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) & (~(SUN4C_INT_E1)));
		break;
	case 8:
		new_mask = ((current_mask) & (~(SUN4C_INT_E8)));
		break;
	case 10:
		new_mask = ((current_mask) & (~(SUN4C_INT_E10)));
		break;
	case 14:
		new_mask = ((current_mask) & (~(SUN4C_INT_E14)));
		break;
	default:
		restore_flags(flags);
		return;
	}
	*interrupt_enable = new_mask;
	restore_flags(flags);
}

void sun4m_disable_irq(unsigned int irq_nr)
{
#if 0
	printk("IRQ routines not yet written for the sun4m\n");
	panic("disable_irq: Unsupported arch.");
#endif
}

void disable_irq(unsigned int irq_nr)
{
	switch(sparc_cpu_model) {
	case sun4c:
		sun4c_disable_irq(irq_nr);
		break;
	case sun4m:
		sun4m_disable_irq(irq_nr);
		break;
	default:
		panic("disable_irq: Unsupported arch.");
	}
}

void sun4c_enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;

	if(sparc_cpu_model != sun4c)
		return;
	save_flags(flags); cli();
	current_mask = *interrupt_enable;
	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) | SUN4C_INT_E1);
		break;
	case 8:
		new_mask = ((current_mask) | SUN4C_INT_E8);
		break;
	case 10:
		new_mask = ((current_mask) | SUN4C_INT_E10);
		break;
	case 14:
		new_mask = ((current_mask) | SUN4C_INT_E14);
		break;
	default:
		restore_flags(flags);
		return;
	}
	*interrupt_enable = new_mask;
	restore_flags(flags);
}

void sun4m_enable_irq(unsigned int irq_nr)
{
#if 0
	printk("IRQ routines not written for the sun4m yet.\n");
	panic("IRQ unsupported arch.");
#endif
}

void enable_irq(unsigned int irq_nr)
{
	switch(sparc_cpu_model) {
	case sun4c:
		sun4c_enable_irq(irq_nr);
		break;
	case sun4m:
		sun4m_enable_irq(irq_nr);
		break;
	default:
		panic("IRQ unsupported arch.");
	}
}

/*
 * Initial irq handlers.
 */
extern void timer_interrupt(int, void *, struct pt_regs *);
extern void rs_interrupt(int, void *, struct pt_regs *);

static struct irqaction timer_irq = {
	timer_interrupt,
	SA_INTERRUPT,
	0, "timer",
	NULL, NULL
};

static struct irqaction serial_irq = {
	rs_interrupt,
	SA_INTERRUPT,
	0, "zilog serial",
	NULL, NULL
};

static struct irqaction *irq_action[16] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, &timer_irq, NULL, &serial_irq, NULL , NULL, NULL
};


int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < 16 ; i++) {
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
	struct irqaction * action = *(irq + irq_action);
	struct irqaction * tmp = NULL;
        unsigned long flags;

        if (irq > 14) {  /* 14 irq levels on the sparc */
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
        save_flags(flags); cli();
	if (action && tmp)
		tmp->next = action->next;
	else
		*(irq + irq_action) = action->next;

	kfree_s(action, sizeof(struct irqaction));

	if (!(*(irq + irq_action)))
		disable_irq(irq);

        restore_flags(flags);
}

void unexpected_irq(int irq, void *dev_id, struct pt_regs * regs)
{
        int i;
	struct irqaction * action = *(irq + irq_action);

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
	struct irqaction * action = *(irq + irq_action);

	kstat.interrupts[irq]++;
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
	struct irqaction * action = *(irq + irq_action);

	kstat.interrupts[irq]++;
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
	kstat.interrupts[irq]++;
	printk("Got FAST_IRQ number %04lx\n", (long unsigned int) irq);
	return;
}

/* Fast IRQ's on the Sparc can only have one routine attached to them,
 * thus no sharing possible.
 */
int request_fast_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		     unsigned long irqflags, const char *devname)
{
	struct irqaction *action;
	unsigned long flags;

	if(irq > 14)
		return -EINVAL;
	if(!handler)
		return -EINVAL;
	action = *(irq + irq_action);
	if(action) {
		if(action->flags & SA_SHIRQ)
			panic("Trying to register fast irq when already shared.\n");
		if(irqflags & SA_SHIRQ)
			panic("Trying to register fast irq as shared.\n");

		/* Anyway, someone already owns it so cannot be made fast. */
		return -EBUSY;
	}

	save_flags(flags); cli();

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);

	if (!action) { 
		restore_flags(flags);
		return -ENOMEM;
	}

	/* Dork with trap table if we get this far. */
	sparc_ttable[SP_TRAP_IRQ1+(irq-1)].inst_one =
		SPARC_BRANCH((unsigned long) handler,
			     (unsigned long) &sparc_ttable[SP_TRAP_IRQ1+(irq-1)].inst_one);
	sparc_ttable[SP_TRAP_IRQ1+(irq-1)].inst_two = SPARC_RD_PSR_L0;
	sparc_ttable[SP_TRAP_IRQ1+(irq-1)].inst_three = SPARC_NOP;
	sparc_ttable[SP_TRAP_IRQ1+(irq-1)].inst_four = SPARC_NOP;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->dev_id = NULL;

	*(irq + irq_action) = action;

	restore_flags(flags);
	return 0;
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction * action, *tmp = NULL;
	unsigned long flags;

	if(irq > 14)
		return -EINVAL;

	if (!handler)
	    return -EINVAL;
	action = *(irq + irq_action);
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
	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);

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
		*(irq + irq_action) = action;

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

void sun4c_init_IRQ(void)
{
	struct linux_prom_registers int_regs[2];
	int ie_node;

	ie_node = prom_searchsiblings (prom_getchild(prom_root_node),
				       "interrupt-enable");
	if(ie_node == 0)
		panic("Cannot find /interrupt-enable node");
	/* Depending on the "address" property is bad news... */
	prom_getproperty(ie_node, "reg", (char *) int_regs, sizeof(int_regs));
	interrupt_enable = (char *) sparc_alloc_io(int_regs[0].phys_addr, 0,
						   int_regs[0].reg_size,
						   "sun4c_interrupts",
						   int_regs[0].which_io, 0x0);
	*interrupt_enable = (SUN4C_INT_ENABLE);
	sti();
}

void sun4m_init_IRQ(void)
{
	int ie_node;

	struct linux_prom_registers int_regs[PROMREG_MAX];
	int num_regs;

	cli();
	if((ie_node = prom_searchsiblings(prom_getchild(prom_root_node), "obio")) == 0 ||
	   (ie_node = prom_getchild (ie_node)) == 0 ||
	   (ie_node = prom_searchsiblings (ie_node, "interrupt")) == 0)
		panic("Cannot find /obio/interrupt node\n");
	num_regs = prom_getproperty(ie_node, "reg", (char *) int_regs,
				    sizeof(int_regs));
	num_regs = (num_regs/sizeof(struct linux_prom_registers));

	/* Apply the obio ranges to these registers. */
	prom_apply_obio_ranges(int_regs, num_regs);

	/* Map the interrupt registers for all possible cpus. */
	sun4m_interrupts = sparc_alloc_io(int_regs[0].phys_addr, 0,
					  PAGE_SIZE*NCPUS, "interrupts_percpu",
					  int_regs[0].which_io, 0x0);

	/* Map the system interrupt control registers. */
	sparc_alloc_io(int_regs[num_regs-1].phys_addr, 0,
		       int_regs[num_regs-1].reg_size, "interrupts_system",
		       int_regs[num_regs-1].which_io, 0x0);
	sti();
}

void init_IRQ(void)
{
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
