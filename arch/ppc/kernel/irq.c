/*
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu)
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
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
#include <linux/init.h>
#include <linux/malloc.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#undef SHOW_IRQ

unsigned lost_interrupts = 0;

unsigned int local_irq_count[NR_CPUS];
static struct irqaction irq_action[32];

/*
 * These are set to the appropriate functions by init_IRQ()
 */
void (*mask_and_ack_irq)(int irq_nr);
void (*set_irq_mask)(int irq_nr);

static unsigned int cached_irq_mask = 0xffffffff;
#define cached_21	(((char *)(&cached_irq_mask))[3])
#define cached_A1	(((char *)(&cached_irq_mask))[2])
static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }
int __ppc_bh_counter;

/* prep */
#define PREP_IRQ_MASK	(((unsigned int)cached_A1)<<8) | (unsigned int)cached_21
extern unsigned long route_pci_interrupts(void);

/* pmac */
#define IRQ_FLAG	((unsigned *)0xf3000020)
#define IRQ_ENABLE	((unsigned *)0xf3000024)
#define IRQ_ACK		((unsigned *)0xf3000028)
#define IRQ_LEVEL	((unsigned *)0xf300002c)
#define KEYBOARD_IRQ	20	/* irq number for command-power interrupt */
#define PMAC_IRQ_MASK	(~ld_le32(IRQ_ENABLE))

void prep_mask_and_ack_irq(int irq_nr)
{
	spin_lock(&irq_controller_lock);
	cached_irq_mask |= 1 << irq_nr;
	if (irq_nr > 7) {
		inb(0xA1);	/* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x62,0x20);	/* Specific EOI to cascade */
		/*outb(0x20,0xA0);*/
		outb(0x60|(irq_nr-8), 0xA0); /* specific eoi */
	} else {
		inb(0x21);	/* DUMMY */
		outb(cached_21,0x21);
		/*outb(0x20,0x20);*/
		outb(0x60|irq_nr,0x20); /* specific eoi */
		  
	}
	spin_unlock(&irq_controller_lock);
}

void pmac_mask_and_ack_irq(int irq_nr)
{
	spin_lock(&irq_controller_lock);
	cached_irq_mask |= 1 << irq_nr;
	out_le32(IRQ_ENABLE, ld_le32(IRQ_ENABLE) & ~(1 << irq_nr));
	out_le32(IRQ_ACK, 1U << irq_nr);
	spin_unlock(&irq_controller_lock);
}

void prep_set_irq_mask(int irq_nr)
{
	if (irq_nr > 7) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

void pmac_set_irq_mask(int irq_nr)
{
	/* this could be being enabled or disabled - so use cached_irq_mask */
	out_le32(IRQ_ENABLE, ~cached_irq_mask /* enable all unmasked */ );
	/*
	 * Unfortunately, setting the bit in the enable register
	 * when the device interrupt is already on *doesn't* set
	 * the bit in the flag register or request another interrupt.
	 */
	if ((ld_le32(IRQ_LEVEL) & (1UL<<irq_nr)) && !(ld_le32(IRQ_FLAG) & (1UL<<irq_nr)))
		lost_interrupts |= (1UL<<irq_nr);
}

/*
 * These have to be protected by the spinlock
 * before being called.
 */
static inline void mask_irq(unsigned int irq_nr)
{
	cached_irq_mask |= 1 << irq_nr;
	set_irq_mask(irq_nr);
}

static inline void unmask_irq(unsigned int irq_nr)
{
	cached_irq_mask &= ~(1 << irq_nr);
	set_irq_mask(irq_nr);
}

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	mask_irq(irq_nr);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
	synchronize_irq();
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	unmask_irq(irq_nr);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
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
#ifdef __SMP_PROF__
	len+=sprintf(buf+len, "IPI: %8lu received\n",
		ipi_count);
#endif
	return len;
}

asmlinkage void do_IRQ(struct pt_regs *regs)
{
	int irq;
	unsigned long bits;
	struct irqaction *action;
	int cpu = smp_processor_id();
	int status;

	hardirq_enter(cpu);

	if ( _machine == _MACH_Pmac )
	{
		bits = ld_le32(IRQ_FLAG) | lost_interrupts;
		lost_interrupts = 0;
	}
	else /* prep */
	{
#if 1
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
		bits = 1UL << irq;
#else
		/*
		 * get the isr from the intr controller since
		 * the bit in the irr has been cleared
		 */
		outb(0x0a, 0x20);
		bits = inb(0x20)&0xff;
		/* handle cascade */
		if ( bits & 4 )
		{
			bits &= ~4UL;
			outb(0x0a, 0xA0);
			bits |= inb(0xA0)<<8;
		}
		/* ignore masked irqs */
		bits &= ~cached_irq_mask;
#endif
	}
	
	for (irq = NR_IRQS - 1; irq >= 0; --irq)
		if (bits & (1U << irq))
			break;

	if (irq < 0) {
		printk("Bogus interrupt from PC = %lx\n", regs->nip);
		goto out;
	}

	mask_and_ack_irq(irq);

	status = 0;
	action = irq_action + irq;
	kstat.interrupts[irq]++;
	if ( action )
	{
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		status |= action->flags;
		action->handler(irq, action->dev_id, regs);
		/*if (status & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);*/
		__cli(); /* in case the handler turned them on */
		spin_lock(&irq_controller_lock);
		unmask_irq(irq);
		spin_unlock(&irq_controller_lock);
	} else {
		disable_irq( irq );
	}
	
	/* make sure we don't miss any cascade intrs due to eoi-ing irq 2 */
	if ( (_machine != _MACH_Pmac)/*prep*/ && (irq > 7) )
		goto retry_cascade;
	/* do_bottom_half is called if necessary from int_return in head.S */
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

	if (irq >= NR_IRQS)
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

	if (irq >= NR_IRQS) {
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
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

__initfunc(void init_IRQ(void))
{
	extern void xmon_irq(int, void *, struct pt_regs *);

	if ( _machine == _MACH_Pmac )
	{
		mask_and_ack_irq = pmac_mask_and_ack_irq;
		set_irq_mask = pmac_set_irq_mask;
		
		*IRQ_ENABLE = 0;
#ifdef CONFIG_XMON
		request_irq(KEYBOARD_IRQ, xmon_irq, 0, "NMI", 0);
#endif	/* CONFIG_XMON */
	}
	else /* prep */
	{
		mask_and_ack_irq = prep_mask_and_ack_irq;
		set_irq_mask = prep_set_irq_mask;
		
		/* init master interrupt controller */
		outb(0x11, 0x20); /* Start init sequence */
		outb(0x40, 0x21); /* Vector base */
		/*outb(0x04, 0x21);*/ /* edge tiggered, Cascade (slave) on IRQ2 */
		outb(0x0C, 0x21); /* level triggered, Cascade (slave) on IRQ2 */
		outb(0x01, 0x21); /* Select 8086 mode */
		outb(0xFF, 0x21); /* Mask all */

		/* init slave interrupt controller */
		outb(0x11, 0xA0); /* Start init sequence */
		outb(0x48, 0xA1); /* Vector base */
		/*outb(0x02, 0xA1);*/ /* edge triggered, Cascade (slave) on IRQ2 */
		outb(0x0A, 0x21); /* level triggered, Cascade (slave) on IRQ2 */
		outb(0x01, 0xA1); /* Select 8086 mode */
		outb(0xFF, 0xA1); /* Mask all */

		/*
		 * According to the Carolina spec from ibm irq's 0,1,2, and 8
		 * must be edge triggered.  Also, the pci intrs must be level
		 * triggered and _only_ isa intrs can be level sensitive
		 * which are 3-7,9-12,14-15. 13 is special - it can be level.
		 *
		 * power on default is 0's in both regs - all edge.
		 *
		 * These edge/level control regs allow edge/level status
		 * to be decided on a irq basis instead of on a PIC basis.
		 * It's still pretty ugly.
		 * - Cort
		 */
		{
			unsigned char irq_mode1 = 0, irq_mode2 = 0;
			irq_mode1 = 0; /* to get rid of compiler warnings */
			/*
			 * On Carolina, irq 15 and 13 must be level (scsi/ide/net).
			 */
			if ( _machine == _MACH_IBM )
				irq_mode2 |= 0xa0;
			/*
			 * Sound on the Powerstack reportedly needs to be edge triggered
			 */
			if ( _machine == _MACH_Motorola )
			{}

			/*outb( irq_mode1 , 0x4d0 );
			  outb( irq_mode2 , 0x4d1 );*/
		}
		outb(cached_A1, 0xA1);
		outb(cached_21, 0x21);
		if (request_irq(2, no_action, SA_INTERRUPT, "cascade", NULL) != 0)
			panic("Could not allocate cascade IRQ!");
		enable_irq(2);  /* Enable cascade interrupt */

		route_pci_interrupts();
	}
}
