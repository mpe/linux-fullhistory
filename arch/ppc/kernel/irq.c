/*
 *  arch/ppc/kernel/irq.c
 *
 *  Power Macintosh version
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu) 
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
#include <linux/config.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#define IRQ_FLAG	((unsigned *)0xf3000020)
#define IRQ_ENABLE	((unsigned *)0xf3000024)
#define IRQ_ACK		((unsigned *)0xf3000028)
#define IRQ_LEVEL	((unsigned *)0xf300002c)

#define KEYBOARD_IRQ	20	/* irq number for command-power interrupt */

#undef SHOW_IRQ 1

unsigned lost_interrupts = 0;

unsigned int local_irq_count[NR_CPUS];
static struct irqaction irq_action[32];

/*
 * This contains the irq mask for both irq controllers
 */
static unsigned int cached_irq_mask = 0xffff;

#define cached_21	(((char *)(&cached_irq_mask))[0])
#define cached_A1	(((char *)(&cached_irq_mask))[1])


int __ppc_bh_counter;

void *null_handler(int,void *,struct pt_regs *);

/*
 * disable and enable intrs in software.  This is used
 * from the non-realtime parts of Linux to disable interrupts.
 * The realtime part disables/enables intrs in the hardware.
 * -- Cort
 */
unsigned long soft_intr_enable = 1;
void _soft_cli(void)
{
	soft_intr_enable = 0;
}

void _soft_sti(void)
{
	soft_intr_enable = 1;
	if ( lost_interrupts )
	{
		printk("lost_interrupts from _soft_sti() %x\n",lost_interrupts);
		fake_interrupt();
	}
}

void *
null_handler(int a, void *b, struct pt_regs *regs)
{
	/*printk("irq.c: null_handler() called.  Should not have happened.\n");*/
}

void
disable_irq(unsigned int irq_nr)
{
	int s = _disable_interrupts();
	unsigned char mask;

#ifdef CONFIG_PMAC
	out_le32(IRQ_ENABLE, ld_le32(IRQ_ENABLE) & ~(1 << irq_nr));
#else /* CONFIG_PMAC */
	mask = 1 << (irq_nr & 7);
	if (irq_nr < 8)
	{
		cached_21 |= mask;
		outb(cached_21,0x21);
	} else
	{
		cached_A1 |= mask;
		outb(cached_A1,0xA1);
	}	
#endif	/* CONFIG_PMAC */
	_enable_interrupts(s);
}

void
enable_irq(unsigned int irq_nr)
{
	int s = _disable_interrupts();
#ifdef CONFIG_PMAC
	unsigned bit = 1U << irq_nr;
	
	out_le32(IRQ_ENABLE, ld_le32(IRQ_ENABLE) & ~(1 << irq_nr));
	out_le32(IRQ_ENABLE, ld_le32(IRQ_ENABLE) | bit);

	/*
	 * Unfortunately, setting the bit in the enable register
	 * when the device interrupt is already on *doesn't* set
	 * the bit in the flag register or request another interrupt.
	 */
	if ((ld_le32(IRQ_LEVEL) & bit) && !(ld_le32(IRQ_FLAG) & bit))
		lost_interrupts |= bit;
#else /* CONFIG_PMAC */
	if (irq_nr < 8) {
		cached_21 &= ~(1 << (irq_nr & 7));
		outb(cached_21,0x21);
	} else
	{
		cached_A1 &= ~(1 << (irq_nr-8 & 7));
		outb(cached_A1,0xA1);
	}	
#endif	/* CONFIG_PMAC */

	_enable_interrupts(s);
}


int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_action + i;
		if (!action || !action->handler) 
			continue;
		len += sprintf(buf+len, "%2d: %10u   %s",
			i, kstat.interrupts[i], action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ", %s", action->name);
		}
		len += sprintf(buf+len, "\n");
	}
/*
 *	Linus - should you add NMI counts here ?????
 */
#ifdef __SMP_PROF__
	len+=sprintf(buf+len, "IPI: %8lu received\n",
		ipi_count);
#endif		
	return len;
}

asmlinkage void handle_IRQ(struct pt_regs *regs)
{
	int irq;
	unsigned bits;
	struct irqaction *action;
	int cpu = smp_processor_id();

	hardirq_enter(cpu);

#ifdef CONFIG_PMAC 
	bits = ld_le32(IRQ_FLAG) | lost_interrupts;
	lost_interrupts = 0;
	
	for (irq = NR_IRQS; irq >= 0; --irq)
		if (bits & (1U << irq))
			break;
#else /* CONFIG_PMAC */
#if 1
	if ( lost_interrupts )
	{
		irq = ffz(~lost_interrupts);
		lost_interrupts &= ~irq;
		goto retry;
	}
	outb(0x0C, 0x20);
	irq = inb(0x20) & 7;
	if (irq == 2)
	{
retry_cascade:		
		outb(0x0C, 0xA0);
		irq = inb(0xA0);
		/* if no intr left */
		if ( !(irq & 128 ) )
			goto out;
		irq = (irq&7) + 8;
	}
retry:
#else
	/* get the irr from the intr controller */
	outb(0x0A, 0x20);
	bits = inb(0x20);
	/* handle cascade */
	if ( bits  ) 
	{
		bits &= 4;
		outb(0x0A, 0xA0);
		bits = inb(0xA0)<<8;
	}
	/* get lost interrupts */
	bits |= lost_interrupts;
	/* save intrs that are masked out */
	lost_interrupts = bits & cached_irq_mask;
	/* get rid of intrs being masked */
	bits &= ~cached_irq_mask;
        /* non-specifc eoi */
	outb(0x20,0x20);
	if ( bits & 0xff00 )
		outb(0x20,0xA0);
	
	printk("bits %04X lost %04X mask %04x\n",
	       bits, lost_interrupts,cached_irq_mask);
	
	for (irq = NR_IRQS; irq >= 0; --irq)
		if (bits & (1U << irq))
			break;
#endif
#endif	/* CONFIG_PMAC */
	
	if (irq < 0) {
		printk("Bogus interrupt from PC = %lx, irq %d\n",regs->nip,irq);
		goto out;
	}

#ifdef CONFIG_PMAC 
	out_le32(IRQ_ACK, 1U << irq);
#else /* CONFIG_PMAC */
        /* mask out the irq while handling it */	
	disable_irq(irq); 
	/*
	 * send eoi to interrupt controller right away or lower
	 * priority intrs would be ignored even if with intrs enabled
	 */
	if (irq > 7)
	{
		outb(0xE0|(irq-8), 0xA0);
		outb(0xE2, 0x20);
	} else
	{
		outb(0xE0|irq, 0x20);
	}
#endif /* !CONFIG_PMAC */

	/*
	 * now that we've acked the irq, if intrs are disabled in software
	 * we're in the real-time system and non-rt linux has disabled them
	 * so we just queue it up and return -- Cort
	 */
	if ( ! soft_intr_enable ) 
	{
		lost_interrupts |= 1UL << irq;
		/* can't printk - kernel expects intrs off! */
		/*printk("irq %d while intrs soft disabled\n", irq);*/
		goto out;
	}
	
	action = irq + irq_action;
	kstat.interrupts[irq]++;
	if (action->handler) {
		action->handler(irq, action->dev_id, regs);
		_disable_interrupts(); /* in case the handler turned them on */
	} else {
		disable_irq( irq );
	}
#ifdef CONFIG_PREP
	/* re-enable if the interrupt was good and isn't one-shot */
	if ( action->handler && !(action->flags & SA_ONESHOT) )
		enable_irq(irq);
	/* make sure we don't miss any cascade intrs due to eoi-ing irq 2 */
	if ( irq > 7 )
		goto retry_cascade;
#endif
	
	hardirq_exit(cpu);
	/*
	 * This should be conditional: we should really get
	 * a return code from the irq handler to tell us
	 * whether the handler wants us to do software bottom
	 * half handling or not..
	 */
	if (1)
		if (bh_active & bh_mask)
			do_bottom_half();
        return;
out:
	hardirq_exit(cpu);

}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction * action;
	unsigned long flags;
	
#ifdef SHOW_IRQ	
	printk("request_irq(): irq %d handler %08x name %s dev_id %04x\n",
	       irq,handler,devname,dev_id);
#endif /* SHOW_IRQ */
	
	if (irq > NR_IRQS)
		return -EINVAL;
	action = irq + irq_action;
	if (action->handler)
		return -EBUSY;
	if (!handler)
		return -EINVAL;
	save_flags(flags);
	cli();
	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	enable_irq(irq);
	restore_flags(flags);
	return 0;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action = irq + irq_action;
	unsigned long flags;

#ifdef SHOW_IRQ	
	printk("free_irq(): irq %d dev_id %04x\n", irq, dev_id);
#endif /* SHOW_IRQ */
	
	if (irq > NR_IRQS) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	disable_irq(irq);
	save_flags(flags);
	cli();
	action->handler = NULL;
	action->flags = 0;
	action->mask = 0;
	action->name = NULL;
	action->dev_id = NULL;
	restore_flags(flags);
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, snaffle up any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!request_irq(i, null_handler, SA_ONESHOT, "probe", NULL)) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + 2; delay > jiffies; );	/* min 10ms delay */

	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned int)cached_A1)<<8) | (unsigned int)cached_21;
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

int probe_irq_off (unsigned long irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cached_A1)<<8) | (unsigned int)cached_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i)) {
			free_irq(i, NULL);
		}
	}
#ifdef SHOW_IRQ
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
#ifdef CONFIG_PMAC  
	extern void xmon_irq(int, void *, struct pt_regs *);

	*IRQ_ENABLE = 0;
	request_irq(KEYBOARD_IRQ, xmon_irq, 0, "NMI", 0);
#else /* CONFIG_PMAC */
	/* Initialize interrupt controllers */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x40, 0x21); /* Vector base */
#if 1
	outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
#else
	outb(0x0C, 0x21); /* level triggered, Cascade (slave) on IRQ2 */
#endif	
	
	outb(0x01, 0x21); /* Select 8086 mode */
	outb(0xFF, 0x21); /* Mask all */

	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x48, 0xA1); /* Vector base */
#if 1
	outb(0x02, 0xA1); /* edge triggered, Cascade (slave) on IRQ2 */
#else
	outb(0x0A, 0x21); /* level triggered, Cascade (slave) on IRQ2 */
#endif
	outb(0x01, 0xA1); /* Select 8086 mode */
	outb(0xFF, 0xA1); /* Mask all */

	/*
	 * Program level mode for irq's that 'can' be level triggered.
	 * This does not effect irq's 0,1,2 and 8 since they must be
	 * edge triggered. This is not the PIC controller.  The default
	 * here is for edge trigger mode.  -- Cort
	 */
#if 0	
	outb(0xf8, 0x4d0); /* level triggered */
	outb(0xff, 0x4d1);
#endif
#if 0	
	outb(0x00, 0x4D0); /* All edge triggered */
	outb(0xCF, 0x4D1); /* Trigger mode */
#endif
	outb(cached_A1, 0xA1);
	outb(cached_21, 0x21);
	if (request_irq(2, null_handler, SA_INTERRUPT, "cascade", NULL))
		printk("Unable to get IRQ2 for cascade\n");
	enable_irq(2);  /* Enable cascade interrupt */
	
#define TIMER0_COUNT 0x40
#define TIMER_CONTROL 0x43
	/* set timer to periodic mode */
	outb_p(0x34,TIMER_CONTROL);		/* binary, mode 2, LSB/MSB, ch 0 */
	/* set the clock to ~100 Hz */
	outb_p(LATCH & 0xff , TIMER0_COUNT);	/* LSB */
	outb(LATCH >> 8 , TIMER0_COUNT);	/* MSB */

	route_pci_interrupts();
#endif	/* CONFIG_PMAC */
}
