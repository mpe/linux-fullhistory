/*
 *	linux/arch/ppc/kernel/irq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *      Adapted from arch/i386 by Gary Thomas
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

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#define CR0_NE 32

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

void disable_irq(unsigned int irq_nr)
{
	unsigned char mask;
	int s = _disable_interrupts();

	mask = 1 << (irq_nr & 7);
	if (irq_nr < 8) {
		cache_21 |= mask;
		outb(cache_21,0x21);
		_enable_interrupts(s);
		return;
	}
	cache_A1 |= mask;
	outb(cache_A1,0xA1);
	_enable_interrupts(s);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned char mask;
	int s = _disable_interrupts();

	mask = ~(1 << (irq_nr & 7));
	if (irq_nr < 8) {
		cache_21 &= mask;
		outb(cache_21,0x21);
		_enable_interrupts(s);
		return;
	}
	cache_A1 &= mask;
	outb(cache_A1,0xA1);
	_enable_interrupts(s);
}

/*
 * Irq handlers.
 */
static struct irqaction timer_irq = { NULL, 0, 0, NULL, NULL, NULL};
static struct irqaction cascade_irq = { NULL, 0, 0, NULL, NULL, NULL};
static struct irqaction math_irq = { NULL, 0, 0, NULL, NULL, NULL};

static struct irqaction *irq_action[16] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL
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

asmlinkage void handle_IRQ(struct pt_regs *regs)
{
	int irq, s;
	struct irqaction * action;
	intr_count++;
	/* Figure out IRQ#, etc. */
	outb(0x0C, 0x20);  /* Poll interrupt controller */
	irq = inb(0x20);
	irq &= 0x07;  /* Caution! */
	if (irq == 2)
	{ /* Cascaded interrupt -> IRQ8..IRQ15 */
		outb(0x0C, 0xA0);
		irq = inb(0xA0) & 0x07;
		irq += 8;
	}
	/* Mask interrupt & Issue EOI to interrupt controller */
	if (irq > 7)
	{
		outb(cache_A1 | (1<<(irq-7)), 0xA1);
		outb(0x20, 0xA0);
		/* Need to ack cascade controller as well */
		outb(0x20, 0x20);
	} else
	{
		outb(cache_21 | (1<<irq), 0x21);
		outb(0x20, 0x20);
	}
	action = *(irq + irq_action);
	kstat.interrupts[irq]++;
	while (action) {
	    if (action->handler)
	    {
		action->handler(irq, action->dev_id, regs);
	    } else
	    {
		_printk("Bogus interrupt #%d\n", irq);
	    }
	    action = action->next;
	}
	if (_disable_interrupts() && !action->notified)
	{
		action->notified = 1;
		printk("*** WARNING! %s handler [IRQ %d] turned interrupts on!\n", action->name, irq);
	}
	/* Re-enable interrupt */
	if (irq > 7)
	{
		outb(cache_A1, 0xA1);
	} else
	{
		outb(cache_21, 0x21);
	}
	intr_count--;
}

/*
 * This routine gets called when the SCSI times out on an operation.
 * I don't know why this happens, but every so often it does and it
 * seems to be a problem with the interrupt controller [state].  It
 * happens a lot when there is also network activity (both devices
 * are on the PCI bus with interrupts on the cascaded controller).
 * Re-initializing the interrupt controller [which might lose some
 * pending edge detected interrupts] seems to fix it.
 */
void check_irq(void )
{
	int s = _disable_interrupts();
	unsigned char _a0, _a1, _20, _21;
	_a1 = inb(0xA1);
	_21 = inb(0x21);
	outb(0x0C, 0x20);  _20 = inb(0x20);	
	outb(0x0C, 0xA0);  _a0 = inb(0xA0);
#if 0	
	printk("IRQ 0x20 = %x, 0x21 = %x/%x, 0xA0 = %x, 0xA1 = %x/%x\n",
		_20, _21, cache_21, _a0, _a1, cache_A1);
#endif		
	/* Reset interrupt controller - see if this fixes it! */
	/* Initialize interrupt controllers */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x40, 0x21); /* Vector base */
	outb(0x04, 0x21); /* Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */
	outb(0xFF, 0x21); /* Mask all */
	outb(0x00, 0x4D0); /* All edge triggered */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x48, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */
	outb(0xFF, 0xA1); /* Mask all */
	outb(0xCF, 0x4D1); /* Trigger mode */
	outb(cache_A1, 0xA1);
	outb(cache_21, 0x21);
	enable_irq(2);  /* Enable cascade interrupt */
	_enable_interrupts(s);
}

#define SA_PROBE SA_ONESHOT

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	struct irqaction * action, *tmp = NULL;
	unsigned long flags;

#if 0
_printk("Request IRQ #%d, Handler: %x\n", irq, handler);
cnpause();
#endif
	if (irq > 15)
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
	save_flags(flags);
	cli();
	if (irq == 2)
	    action = &cascade_irq;
	else if (irq == 13)
	  action = &math_irq;
	else if (irq == TIMER_IRQ)
	  action = &timer_irq;
	else
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

	if (tmp) {
	    tmp->next = action;
	} else {
	    *(irq + irq_action) = action;
#if 0	
	    if (!(action->flags & SA_PROBE)) { /* SA_ONESHOT is used by probing */
		if (action->flags & SA_INTERRUPT)
		  set_intr_gate(0x20+irq,fast_interrupt[irq]);
		else
		  set_intr_gate(0x20+irq,interrupt[irq]);
	    }
#endif	
	    if (irq < 8) {
		cache_21 &= ~(1<<irq);
		outb(cache_21,0x21);
	    } else {
		cache_21 &= ~(1<<2);
		cache_A1 &= ~(1<<(irq-8));
		outb(cache_21,0x21);
		outb(cache_A1,0xA1);
	    }
	}
	restore_flags(flags);
	return 0;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action = *(irq + irq_action);
	struct irqaction * tmp = NULL;
	unsigned long flags;

	if (irq > 15) {
		printk("Trying to free IRQ%d\n",irq);
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
	save_flags(flags);
	cli();
	if (action && tmp) {
	    tmp->next = action->next;
	} else {
	    *(irq + irq_action) = action->next;
	}

	if ((irq == 2) || (irq == 13) | (irq == TIMER_IRQ))
	  memset(action, 0, sizeof(struct irqaction));
	else 
	  kfree_s(action, sizeof(struct irqaction));
	
	if (!(*(irq + irq_action))) {
	    if (irq < 8) {
		cache_21 |= 1 << irq;
		outb(cache_21,0x21);
	    } else {
		cache_A1 |= 1 << (irq-8);
		outb(cache_A1,0xA1);
	    }
#if 0	
	    set_intr_gate(0x20+irq,bad_interrupt[irq]);
#endif	
	}

	restore_flags(flags);
}

static void no_action(int cpl, void *dev_id, struct pt_regs * regs) { }

unsigned /*int*/ long probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, snaffle up any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!request_irq(i, no_action, SA_PROBE, "probe", NULL)) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + 2; delay > jiffies; );	/* min 10ms delay */

	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i) & irqmask) {
			irqs ^= (1 << i);
			free_irq(i, NULL);
		}
	}
#ifdef DEBUG
	printk("probe_irq_on:  irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	return irqs;
}

int probe_irq_off (unsigned /*int*/ long irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i)) {
			free_irq(i, NULL);
		}
	}
#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	irqs &= irqmask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;
	return i;
}

void init_IRQ(void)
{
	unsigned long *vme2_ie  = (unsigned long *)0xFEFF006C;
	unsigned long *vme2_ic  = (unsigned long *)0xFEFF0074;
	unsigned long *vme2_il2 = (unsigned long *)0xFEFF007C;
	unsigned long *vme2_ioc = (unsigned long *)0xFEFF0088;
	unsigned char *vme2pci_ic = (unsigned char *)0x80802050;
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	int i;

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	if (request_irq(2, no_action, SA_INTERRUPT, "cascade", NULL))
		printk("Unable to get IRQ2 for cascade\n");
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
#if 0	
	/* Enable SIG0 */
	*vme2_ie = (*vme2_ie & 0xFFFBFFFF) | 0x00040000;
	/* Clear any pending interrupts */
	*vme2_ic = 0xFFFFFFFF;
	/* SIG0 -> Level 5 */
	*vme2_il2 = (*vme2_il2 & 0xFFFFF0FF) | 0x00000500;
	/* Master interrupt enable */
	*vme2_ioc |= 0x00800000;
#endif	
	/* Enable interrupts from VMECHIP */
	*vme2pci_ic |= 0x08;
	/* Route PCI interrupts */
	ibc_pirq[0] = 0x0A;  /* PIRQ0 -> ISA10 */
	ibc_pirq[1] = 0x0B;  /* PIRQ1 -> ISA11 */
	ibc_pirq[2] = 0x0E;  /* PIRQ2 -> ISA14 */
	ibc_pirq[3] = 0x0F;  /* PIRQ3 -> ISA15 */
	/* Enable PCI interrupts */
	*ibc_pcicon |= 0x20;
} 

PCI_irq(int irq)
{
	static short _irq[] = {10, 11, 14, 15};
	int res = _irq[(irq-1)&0x03];
#if 0	
	_printk("PCI IRQ #%d = %d\n", irq, res);
#endif	
	return (res);
}
