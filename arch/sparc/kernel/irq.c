/*  arch/sparc/kernel/irq.c:  Interrupt request handling routines. On the
 *                            Sparc the IRQ's are basically 'cast in stone'
 *                            and you are supposed to probe the prom's device
 *                            node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@jamica.lab.ipmce.su)
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * The same sigaction struct is used, and with similar semantics (ie there
 * is a SA_INTERRUPT flag etc). Naturally it's not a 1:1 relation, but there
 * are similarities.
 *
 * sa_handler(int irq_NR) is the default function called (0 if no).
 * sa_mask is horribly ugly (I won't even mention it)
 * sa_flags contains various info: SA_INTERRUPT etc
 * sa_restorer is the unused
 */

#include <linux/config.h>
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
#include <asm/irq.h>
#include <asm/io.h>

/* Pointer to the interrupt enable byte */
/* XXX Ugh, this is so sun4c specific it's driving me nuts. XXX */
unsigned char *interrupt_enable = 0;
struct sun4m_intregs *sun4m_interrupts;

/* XXX Needs to handle Sun4m semantics XXX */
void
disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;

	if(sparc_cpu_model != sun4c) return;

	save_flags(flags);
	cli();

	current_mask = *interrupt_enable;

	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) & (~(SUN4C_INT_E1)));
		break;
	case 4:
		new_mask = ((current_mask) & (~(SUN4C_INT_E4)));
		break;
	case 6:
		new_mask = ((current_mask) & (~(SUN4C_INT_E6)));
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
#if 0 /* Actually this is safe, as the floppy driver needs this */
		printk("AIEEE, Illegal interrupt disable requested irq=%d\n", 
		       (int) irq_nr);
		prom_halt();
#endif
		break;
	};
  
	restore_flags(flags);
	return;
}

/* XXX Needs to handle sun4m semantics XXX */
void
enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char current_mask, new_mask;

	if(sparc_cpu_model != sun4c) return;

	save_flags(flags);
	cli();

	current_mask = *interrupt_enable;

	switch(irq_nr) {
	case 1:
		new_mask = ((current_mask) | SUN4C_INT_E1);
		break;
	case 4:
		new_mask = ((current_mask) | SUN4C_INT_E4);
		break;
	case 6:
		new_mask = ((current_mask) | SUN4C_INT_E6);
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
#if 0  /* Floppy driver does this on sun4c's anyhow */
		printk ("Interrupt does not need to enable IE\n");
		return;
#endif
		restore_flags(flags);
		return;
	};

	*interrupt_enable = new_mask;

	restore_flags(flags);

	return;
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
        save_flags(flags);
        cli();
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
                        printk("[%s:%d:0x%x] ", irq_action[i].name, (int) i,
			       (unsigned int) irq_action[i].handler);
        printk("AIEEE\n");
	prom_halt();
}

void
handler_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq_action + irq;

	if (!action->handler) {
		unexpected_irq(irq, regs);
		return;
	}
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

extern void probe_clock(void);
		
int
request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	    unsigned long irqflags, const char * devname)
{
	struct irqaction *action;
	unsigned long flags;

	if(irq > 14)  /* Only levels 1-14 are valid on the Sparc. */
		return -EINVAL;

	/* i386 keyboard interrupt request, just return */
	if(irq == 1) return 0;

	/* sched_init() requesting the timer IRQ */
	if(irq == 0) {
		irq = 10;
	}

	action = irq + irq_action;

	if(action->handler)
		return -EBUSY;

	if(!handler)
		return -EINVAL;

	save_flags(flags);

	cli();

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;

	enable_irq(irq);

	/* Init the timer/clocks if necessary. */
	if(irq == 10) probe_clock();

	restore_flags(flags);

	return 0;
}

void
sun4c_init_IRQ(void)
{
	struct linux_prom_registers int_regs[2];
	int ie_node;

	ie_node = prom_searchsiblings (prom_getchild(prom_root_node),
				       "interrupt-enable");
	if(ie_node == 0) {
		printk("Cannot find /interrupt-enable node\n");
		prom_halt();
	}
	/* Depending on the "address" property is bad news... */
	prom_getproperty(ie_node, "reg", (char *) int_regs, sizeof(int_regs));
	sparc_alloc_io(int_regs[0].phys_addr, (void *) INTREG_VADDR,
		       int_regs[0].reg_size, "sun4c_interrupts",
		       int_regs[0].which_io, 0x0);

	interrupt_enable = (char *) INTREG_VADDR;

	/* Default value, accept interrupts, but no one is actually active */
	/* We also turn on level14 interrupts so PROM can run the console. */
	*interrupt_enable = (SUN4C_INT_ENABLE | SUN4C_INT_E14);
	sti(); /* As of NOW, L1-A works.  Turn irq's on full-blast. */
	return;
}

void
sun4m_init_IRQ(void)
{
	int ie_node, i;

	struct linux_prom_registers int_regs[PROMREG_MAX];
	int num_regs;

	cli();

	if((ie_node = prom_searchsiblings(prom_getchild(prom_root_node), "obio")) == 0 ||
	   (ie_node = prom_getchild (ie_node)) == 0 ||
	   (ie_node = prom_searchsiblings (ie_node, "interrupt")) == 0)
	{
		printk("Cannot find /obio/interrupt node\n");
		prom_halt();
	}
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

#if 0
	printk("Interrupt register dump...\n");

	for(i=0; i<NCPUS; i++)
		printk("cpu%d: tbt %08x\n", i,
		       sun4m_interrupts->cpu_intregs[i].tbt);

	printk("Master tbt %08x\n", sun4m_interrupts->tbt);
	printk("Master irqs %08x\n", sun4m_interrupts->irqs);
	printk("Master set %08x\n", sun4m_interrupts->set);
	printk("Master clear %08x\n", sun4m_interrupts->clear);
	printk("Undirected ints taken by: %08x\n",
	       sun4m_interrupts->undirected_target);

	prom_halt();
#endif

	sti();

	return;
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
		printk("Cannot initialize IRQ's on this Sun machine...\n");
		halt();
		break;
	};

	return;
}
