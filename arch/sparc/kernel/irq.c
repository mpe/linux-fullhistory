/*  $Id: irq.c,v 1.29 1995/11/25 00:58:08 davem Exp $
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

void
sun4c_disable_irq(unsigned int irq_nr)
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
	};
  
	*interrupt_enable = new_mask;
	restore_flags(flags);
}

void
sun4m_disable_irq(unsigned int irq_nr)
{
	printk("IRQ routines not yet written for the sun4m\n");
	panic("disable_irq: Unsupported arch.");
}

void
disable_irq(unsigned int irq_nr)
{
	switch(sparc_cpu_model) {
	case sun4c:
		sun4c_disable_irq(irq_nr);
		break;
	case sun4m:
		sun4m_disable_irq(irq_nr);
	default:
		panic("disable_irq: Unsupported arch.");
	}
}

void
sun4c_enable_irq(unsigned int irq_nr)
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
	};

	*interrupt_enable = new_mask;
	restore_flags(flags);
}

void
sun4m_enable_irq(unsigned int irq_nr)
{
	printk("IRQ routines not written for the sun4m yet.\n");
	panic("IRQ unsupported arch.");
}

void
enable_irq(unsigned int irq_nr)
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
struct irqaction {
	void (*handler)(int, struct pt_regs *);
	unsigned long flags;
	unsigned long mask;
	const char *name;
};

static struct irqaction irq_action[16] = {
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
  { NULL, 0, 0, NULL }, { NULL, 0, 0, NULL }
};


int
get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action = irq_action;

	for (i = 0 ; i < 16 ; i++, action++) {
		if (!action->handler)
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s\n",
			       i, kstat.interrupts[i],
			       (action->flags & SA_INTERRUPT) ? '+' : ' ',
			       action->name);
	}
	return len;
}

void
free_irq(unsigned int irq)
{
        struct irqaction * action = irq + irq_action;
        unsigned long flags;

        if (irq > 14) {  /* 14 irq levels on the sparc */
                printk("Trying to free bogus IRQ %d\n", irq);
                return;
        }
        if (!action->handler) {
                printk("Trying to free free IRQ%d\n", irq);
                return;
        }
        save_flags(flags); cli();
        disable_irq(irq);
        action->handler = NULL;
        action->flags = 0;
        action->mask = 0;
        action->name = NULL;
        restore_flags(flags);
}

void
unexpected_irq(int irq, struct pt_regs * regs)
{
        int i;

        printk("IO device interrupt, irq = %d\n", irq);
        printk("PC = %08lx NPC = %08lx FP=%08lx\n", regs->pc, 
		    regs->npc, regs->u_regs[14]);
        printk("Expecting: ");
        for (i = 0; i < 16; i++)
                if (irq_action[i].handler)
                        prom_printf("[%s:%d:0x%x] ", irq_action[i].name, (int) i,
			       (unsigned int) irq_action[i].handler);
        printk("AIEEE\n");
	panic("bogus interrupt received");
}

void
handler_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq_action + irq;

	kstat.interrupts[irq]++;
	if (!action->handler)
		unexpected_irq(irq, regs);
	else
		action->handler(irq, regs);
}

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void
do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction *action = irq + irq_action;

	kstat.interrupts[irq]++;
	action->handler(irq, regs);
	return;
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void
do_fast_IRQ(int irq)
{
	kstat.interrupts[irq]++;
	printk("Got FAST_IRQ number %04lx\n", (long unsigned int) irq);
	return;
}

int
request_fast_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
		 unsigned long irqflags, const char *devname)
{
	struct irqaction *action;
	unsigned long flags;

	if(irq > 14)
		return -EINVAL;
	action = irq + irq_action;
	if(action->handler)
		return -EBUSY;
	if(!handler)
		return -EINVAL;

	save_flags(flags); cli();

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

	restore_flags(flags);
	return 0;
}

extern void probe_clock(void);
		
int
request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	    unsigned long irqflags, const char * devname)
{
	struct irqaction *action;
	unsigned long flags;

	if(irq > 14)
		return -EINVAL;

	action = irq + irq_action;
	if(action->handler)
		return -EBUSY;
	if(!handler)
		return -EINVAL;

	save_flags(flags); cli();
	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	enable_irq(irq);
	if(irq == 10)
		probe_clock();
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

void
sun4c_init_IRQ(void)
{
	struct linux_prom_registers int_regs[2];
	int ie_node;

	ie_node = prom_searchsiblings (prom_getchild(prom_root_node),
				       "interrupt-enable");
	if(ie_node == 0)
		panic("Cannot find /interrupt-enable node");
	/* Depending on the "address" property is bad news... */
	prom_getproperty(ie_node, "reg", (char *) int_regs, sizeof(int_regs));
	sparc_alloc_io(int_regs[0].phys_addr, (void *) INTREG_VADDR,
		       int_regs[0].reg_size, "sun4c_interrupts",
		       int_regs[0].which_io, 0x0);

	interrupt_enable = (char *) INTREG_VADDR;

	/* Default value, accept interrupts, but no one is actually active */
	*interrupt_enable = (SUN4C_INT_ENABLE);
	sti(); /* Turn irq's on full-blast. */
}

void
sun4m_init_IRQ(void)
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
	sparc_alloc_io(int_regs[0].phys_addr, (void *) INTREG_VADDR,
		       PAGE_SIZE*NCPUS, "interrupts_percpu",
		       int_regs[0].which_io, 0x0);

	/* Map the system interrupt control registers. */
	sparc_alloc_io(int_regs[num_regs-1].phys_addr,
		       (void *) INTREG_VADDR+(NCPUS*PAGE_SIZE),
		       int_regs[num_regs-1].reg_size, "interrupts_system",
		       int_regs[num_regs-1].which_io, 0x0);

	sun4m_interrupts = (struct sun4m_intregs *) INTREG_VADDR;
	sti();
}

void
init_IRQ(void)
{
	switch(sparc_cpu_model) {
	case sun4c:
		sun4c_init_IRQ();
		break;
	case sun4m:
		sun4m_init_IRQ();
		break;
	default:
		panic("Cannot initialize IRQ's on this Sun machine...");
		break;
	};
}
