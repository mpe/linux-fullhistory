/*
 * $Id: irq.c,v 1.102 1999/02/03 01:36:59 paulus Exp $
 *
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *  
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * The MPC8xx has an interrupt mask in the SIU.  If a bit is set, the
 * interrupt is _enabled_.  As expected, IRQ0 is bit 0 in the 32-bit
 * mask register (of which only 16 are defined), hence the weird shifting
 * and compliment of the cached_irq_mask.  I want to be able to stuff
 * this right into the SIU SMASK register.
 * Many of the prep/chrp functions are conditional compiled on CONFIG_8xx
 * to reduce code space and undefined function references.
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
#include <linux/openpic.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <asm/bitops.h>
#include <asm/hydra.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/gg2.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/amigappc.h>
#include <asm/ptrace.h>
#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/mbx.h>
#endif

static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }
extern volatile unsigned long ipi_count;
static void dispatch_handler(struct pt_regs *regs, int irq);
void enable_irq(unsigned int irq_nr);
void disable_irq(unsigned int irq_nr);

static void i8259_mask_and_ack_irq(unsigned int irq_nr);
static void i8259_mask_irq(unsigned int irq_nr);
static void i8259_unmask_irq(unsigned int irq_nr);
#ifdef CONFIG_8xx
static void mbx_mask_and_ack(unsigned int irq_nr);
static void mbx_mask_irq(unsigned int irq_nr);
static void mbx_unmask_irq(unsigned int irq_nr);
static void mbx_i8259_action(int cpl, void *dev_id, struct pt_regs *regs);
#else /* CONFIG_8xx */
static volatile unsigned char *chrp_int_ack_special;
extern void process_int(unsigned long vec, struct pt_regs *fp);
extern void apus_init_IRQ(void);
extern void amiga_disable_irq(unsigned int irq);
extern void amiga_enable_irq(unsigned int irq);
static void pmac_fix_gatwick_interrupts(struct device_node *gw, int irq_base);
static void gatwick_action(int cpl, void *dev_id, struct pt_regs *regs);
static void pmac_mask_irq(unsigned int irq_nr);
static void pmac_unmask_irq(unsigned int irq_nr);
static void pmac_mask_and_ack_irq(unsigned int irq_nr);
static void chrp_mask_and_ack_irq(unsigned int irq_nr);
static void chrp_unmask_irq(unsigned int irq_nr);
static void chrp_mask_irq(unsigned int irq_nr);
#ifdef __SMP__	
static void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_message_recv(void);
#endif /* __SMP__ */
#endif /* CONFIG_8xx */

#ifdef CONFIG_APUS
/* Rename a few functions. Requires the CONFIG_APUS protection. */
#define request_irq nop_ppc_request_irq
#define free_irq nop_ppc_free_irq
#define get_irq_list nop_get_irq_list
#define VEC_SPUR    (24)
#endif

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
unsigned char cached_8259[2] = { 0xff, 0xff };
#define cached_A1 (cached_8259[0])
#define cached_21 (cached_8259[1])

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
int max_irqs;
int max_real_irqs;
static int spurious_interrupts = 0;
static unsigned int cached_irq_mask[NR_MASK_WORDS];
unsigned int lost_interrupts[NR_MASK_WORDS];
atomic_t n_lost_interrupts;

#ifndef CONFIG_8xx
#define GATWICK_IRQ_POOL_SIZE	10
static struct interrupt_info gatwick_int_pool[GATWICK_IRQ_POOL_SIZE];
/* pmac */
struct pmac_irq_hw {
	unsigned int	flag;
	unsigned int	enable;
	unsigned int	ack;
	unsigned int	level;
};

/* these addresses are obtained from the device tree now -- Cort */
volatile struct pmac_irq_hw *pmac_irq_hw[4] __pmac = {
	(struct pmac_irq_hw *) 0xf3000020,
	(struct pmac_irq_hw *) 0xf3000010,
	(struct pmac_irq_hw *) 0xf4000020,
	(struct pmac_irq_hw *) 0xf4000010,
};
#endif /* CONFIG_8xx */

/* nasty hack for shared irq's since we need to do kmalloc calls but
 * can't very early in the boot when we need to do a request irq.
 * this needs to be removed.
 * -- Cort
 */
static char cache_bitmask = 0;
static struct irqaction malloc_cache[8];
extern int mem_init_done;

void *irq_kmalloc(size_t size, int pri)
{
	unsigned int i;
	if ( mem_init_done )
		return kmalloc(size,pri);
	for ( i = 0; i <= 3 ; i++ )
		if ( ! ( cache_bitmask & (1<<i) ) )
		{
			cache_bitmask |= (1<<i);
			return (void *)(&malloc_cache[i]);
		}
	return 0;
}

void irq_kfree(void *ptr)
{
	unsigned int i;
	for ( i = 0 ; i <= 3 ; i++ )
		if ( ptr == &malloc_cache[i] )
		{
			cache_bitmask &= ~(1<<i);
			return;
		}
	kfree(ptr);
}

struct hw_interrupt_type {
	const char * typename;
	void (*startup)(unsigned int irq);
	void (*shutdown)(unsigned int irq);
	void (*handle)(unsigned int irq, struct pt_regs * regs);
	void (*enable)(unsigned int irq);
	void (*disable)(unsigned int irq);
	void (*mask_and_ack)(unsigned int irq);
	int irq_offset;
};

#define mask_irq(irq) ({if (irq_desc[irq].ctl && irq_desc[irq].ctl->disable) irq_desc[irq].ctl->disable(irq);})
#define unmask_irq(irq) ({if (irq_desc[irq].ctl && irq_desc[irq].ctl->enable) irq_desc[irq].ctl->enable(irq);})
#define mask_and_ack_irq(irq) ({if (irq_desc[irq].ctl && irq_desc[irq].ctl->mask_and_ack) irq_desc[irq].ctl->mask_and_ack(irq);})

struct irqdesc {
	struct irqaction *action;
	struct hw_interrupt_type *ctl;
};
static struct irqdesc irq_desc[NR_IRQS] = {{0, 0}, };

static struct hw_interrupt_type i8259_pic = {
	" i8259    ",
	NULL,
	NULL,
	NULL,
	i8259_unmask_irq,
	i8259_mask_irq,
	i8259_mask_and_ack_irq,
	0
};
#ifndef CONFIG_8xx
static struct hw_interrupt_type pmac_pic = {
	" PMAC-PIC ",
	NULL,
	NULL,
	NULL,
	pmac_unmask_irq,
	pmac_mask_irq,
	pmac_mask_and_ack_irq,
	0
};

static struct hw_interrupt_type gatwick_pic = {
	" GATWICK  ",
	NULL,
	NULL,
	NULL,
	pmac_unmask_irq,
	pmac_mask_irq,
	pmac_mask_and_ack_irq,
	0
};

static struct hw_interrupt_type open_pic = {
	" OpenPIC  ",
	NULL,
	NULL,
	NULL,
	chrp_unmask_irq,
	chrp_mask_irq,
	chrp_mask_and_ack_irq,
	0
};
#else
static struct hw_interrupt_type ppc8xx_pic = {
	" 8xx SIU  ",
	NULL,
	NULL,
	NULL,
	mbx_unmask_irq,
	mbx_mask_irq,
	mbx_mask_and_ack,
	0
};
#endif /* CONFIG_8xx */

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *old, **p, *action;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
	{
		/* Free */
		for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next)
		{
			/* Found it - now free it */
			save_flags(flags);
			cli();
			*p = action->next;
			restore_flags(flags);
			irq_kfree(action);
			return 0;
		}
		return -ENOENT;
	}
	
	action = (struct irqaction *)
		irq_kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;
	
	save_flags(flags);
	cli();
	
	action->handler = handler;
	action->flags = irqflags;					
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	action->next = NULL;
	enable_irq(irq);
	
	p = &irq_desc[irq].action;
	
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & action->flags & SA_SHIRQ))
			return -EBUSY;
		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
	}
	*p = action;

	restore_flags(flags);	
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	request_irq(irq, NULL, 0, NULL, dev_id);
}

void disable_irq(unsigned int irq_nr)
{
	mask_irq(irq_nr);
	synchronize_irq();
}

void enable_irq(unsigned int irq_nr)
{
	unmask_irq(irq_nr);
}

int get_irq_list(char *buf)
{
	int i, len = 0, j;
	struct irqaction * action;

	len += sprintf(buf+len, "           ");
	for (j=0; j<smp_num_cpus; j++)
		len += sprintf(buf+len, "CPU%d       ",j);
	*(char *)(buf+len++) = '\n';

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_desc[i].action;
		if ( !action || !action->handler )
			continue;
		len += sprintf(buf+len, "%3d: ", i);		
#ifdef __SMP__
		for (j = 0; j < smp_num_cpus; j++)
			len += sprintf(buf+len, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#else		
		len += sprintf(buf+len, "%10u ", kstat_irqs(i));
#endif /* __SMP__ */
		if ( irq_desc[i].ctl )		
			len += sprintf(buf+len, " %s ", irq_desc[i].ctl->typename );
		len += sprintf(buf+len, "    %s",action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ", %s", action->name);
		}
		len += sprintf(buf+len, "\n");
	}
#ifdef __SMP__
	/* should this be per processor send/receive? */
	len += sprintf(buf+len, "IPI: %10lu", ipi_count);
	for ( i = 0 ; i <= smp_num_cpus-1; i++ )
		len += sprintf(buf+len,"          ");
	len += sprintf(buf+len, "     interprocessor messages received\n");
#endif		
	len += sprintf(buf+len, "BAD: %10u",spurious_interrupts);
	for ( i = 0 ; i <= smp_num_cpus-1; i++ )
		len += sprintf(buf+len,"        ");
	len += sprintf(buf+len, "         spurious or short\n");
	return len;
}

static void dispatch_handler(struct pt_regs *regs, int irq)
{
	int status;
	struct irqaction *action;
	int cpu = smp_processor_id();
	
	mask_and_ack_irq(irq);
	status = 0;
	action = irq_desc[irq].action;
	kstat.irqs[cpu][irq]++;
	if (action && action->handler) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		do { 
			status |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while ( action );
		__cli();
		unmask_irq(irq);
	} else {
		spurious_interrupts++;
		disable_irq( irq );
	}
}

#define MAXCOUNT 100000000
asmlinkage void do_IRQ(struct pt_regs *regs, int isfake)
{
	int irq;
	unsigned long bits = 0;
	int cpu = smp_processor_id();
	int openpic_eoi_done = 0;

	hardirq_enter(cpu);
#ifndef CONFIG_8xx		  
#ifdef __SMP__
	/* IPI's are a hack on the powersurge -- Cort */
	if ( (_machine == _MACH_Pmac) && (cpu != 0) )
	{
		if (!isfake)
		{
#ifdef CONFIG_XMON
			static int xmon_2nd;
			if (xmon_2nd)
				xmon(regs);
#endif
			smp_message_recv();
			goto out;
		}
		/* could be here due to a do_fake_interrupt call but we don't
		   mess with the controller from the second cpu -- Cort */
		goto out;
	}

	{
		unsigned int loops = MAXCOUNT;
		while (test_bit(0, &global_irq_lock)) {
			if (smp_processor_id() == global_irq_holder) {
				printk("uh oh, interrupt while we hold global irq lock!\n");
#ifdef CONFIG_XMON
				xmon(0);
#endif
				break;
			}
			if (loops-- == 0) {
				printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
				xmon(0);
#endif
			}
		}
	}
#endif /* __SMP__ */			

	switch ( _machine )
	{
	case _MACH_Pmac:
		for (irq = max_real_irqs - 1; irq > 0; irq -= 32) {
			int i = irq >> 5;
			bits = ld_le32(&pmac_irq_hw[i]->flag)
				| lost_interrupts[i];
			if (bits == 0)
				continue;
			irq -= cntlzw(bits);
			break;
		}
		break;
	case _MACH_chrp:
		irq = openpic_irq(0);
		if (irq == IRQ_8259_CASCADE)
		{
			/*
			 * This magic address generates a PCI IACK cycle.
			 * 
			 * This should go in the above mask/ack code soon. -- Cort
			 */
			irq = *chrp_int_ack_special;
			/*
			 * Acknowledge as soon as possible to allow i8259
			 * interrupt nesting
			 */
			openpic_eoi(0);
			openpic_eoi_done = 1;
		}
		if (irq == OPENPIC_VEC_SPURIOUS)
		{
				/*
				 * Spurious interrupts should never be
				 * acknowledged
				 */
			spurious_interrupts++;
			openpic_eoi_done = 1;
		}
		bits = 1UL << irq;
		break;
	case _MACH_prep:
		outb(0x0C, 0x20);
		irq = inb(0x20) & 7;
		if (irq == 2)
		{
			outb(0x0C, 0xA0);
			irq = (inb(0xA0) & 7) + 8;
			bits |= 1UL << irq;
#if 0			
			/* It's possible to loose intrs here
			 * if we get 2 intrs in the upper 8
			 * bits.  We eoi irq 2 and handle one of
			 * the upper intrs but then ignore it
			 * since we've already eoi-d 2.  So,
			 * we must keep track of lost intrs.
			 * -- Cort
			 */
			while (1)
			{
				int i;
				outb(0x0C, 0xA0);
				i = inb(0xA0);
				if ( !(i & 128) )
					break;
				irq &= 7;
				irq += 8;
				bits |= 1UL << irq;
			}
#endif			
		}
		else
			bits = 1UL << irq;
		  
		break;
#ifdef CONFIG_APUS
	case _MACH_apus:
	{
		int old_level, new_level;

		old_level = ~(regs->mq) & IPLEMU_IPLMASK;
		new_level = (~(regs->mq) >> 3) & IPLEMU_IPLMASK;
		
		if (new_level == 0)
		{
			goto apus_out;
		}
		
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
		APUS_WRITE(APUS_IPL_EMU, (IPLEMU_SETRESET
					  | (~(new_level) & IPLEMU_IPLMASK)));
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
		
		process_int (VEC_SPUR+new_level, regs);
		
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_DISABLEINT);
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
		APUS_WRITE(APUS_IPL_EMU, (IPLEMU_SETRESET
					  | (~(old_level) & IPLEMU_IPLMASK)));
apus_out:
		hardirq_exit(cpu);
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
		goto out2;
	}
#endif	
	}

	if (irq < 0)
	{
		printk(KERN_DEBUG "Bogus interrupt %d from PC = %lx\n",
		       irq, regs->nip);
		spurious_interrupts++;
		goto out;
	}					
	
#else /* CONFIG_8xx */
	/* For MPC8xx, read the SIVEC register and shift the bits down
	 * to get the irq number.
	 */
	bits = ((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec;
	irq = bits >> 26;
	irq += ppc8xx_pic.irq_offset;
	bits = 1UL << irq;
#endif /* CONFIG_8xx */
#if 0
	/*
	 * this allows for > 1 interrupt at a time so we can
	 * clear out any 'double' interrupts on prep and
	 * finish up the lost interrupts.
	 * It doesn't currently work for irqs > 31 so I'm leaving
	 * it commented out for now.
	 * -- Cort
	 */
	for ( i = 0 ; i < sizeof(bits)*8 ; i++ )
		if ( bits & (1UL<<i) )
			dispatch_handler( regs, i );
#else	
	dispatch_handler( regs, irq );
#endif	
	
#ifndef CONFIG_8xx
out:
	if (_machine == _MACH_chrp && !openpic_eoi_done)
		openpic_eoi(0);
#endif /* CONFIG_8xx */
	hardirq_exit(cpu);
#ifdef CONFIG_APUS
out2:
#endif
}

unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

static void i8259_mask_and_ack_irq(unsigned int irq_nr)
{
	if ( irq_nr >= i8259_pic.irq_offset )	
		irq_nr -= i8259_pic.irq_offset;
	if (irq_nr > 7) {
		cached_A1 |= 1 << (irq_nr-8);
		inb(0xA1);	/* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x62,0x20);	/* Specific EOI to cascade */
		/*outb(0x20,0xA0);*/
		outb(0x60|(irq_nr-8), 0xA0); /* specific eoi */
	} else {
		cached_21 |= 1 << irq_nr;
		inb(0x21);	/* DUMMY */
		outb(cached_21,0x21);
		/*outb(0x20,0x20);*/
		outb(0x60|irq_nr,0x20); /* specific eoi */
	}
}

static void i8259_set_irq_mask(int irq_nr)
{
	outb(cached_A1,0xA1);
	outb(cached_21,0x21);
}

static void i8259_mask_irq(unsigned int irq_nr)
{
	if ( irq_nr >= i8259_pic.irq_offset )	
		irq_nr -= i8259_pic.irq_offset;
	if ( irq_nr < 8 )
		cached_21 |= 1 << irq_nr;
	else
		cached_A1 |= 1 << (irq_nr-8);
	i8259_set_irq_mask(irq_nr);
}

static void i8259_unmask_irq(unsigned int irq_nr)
{
  
	if ( irq_nr >= i8259_pic.irq_offset )	
		irq_nr -= i8259_pic.irq_offset;
	if ( irq_nr < 8 )
		cached_21 &= ~(1 << irq_nr);
	else
		cached_A1 &= ~(1 << (irq_nr-8));
	i8259_set_irq_mask(irq_nr);
}

#ifndef CONFIG_8xx
static void gatwick_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int irq, bits;
	
	for (irq = max_irqs - 1; irq > max_real_irqs; irq -= 32) {
		int i = irq >> 5;
		bits = ld_le32(&pmac_irq_hw[i]->flag)
			| lost_interrupts[i];
		if (bits == 0)
			continue;
		irq -= cntlzw(bits);
		break;
	}
	/* The previous version of this code allowed for this case, we
	 * don't.  Put this here to check for it.
	 * -- Cort
	 */
	if ( irq_desc[irq].ctl != &gatwick_pic )
		printk("gatwick irq not from gatwick pic\n");
	else
		dispatch_handler( regs, irq );
}

void pmac_mask_and_ack_irq(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;

        if ((unsigned)irq_nr >= max_irqs)
                return;

        clear_bit(irq_nr, cached_irq_mask);
        if (test_and_clear_bit(irq_nr, lost_interrupts))
                atomic_dec(&n_lost_interrupts);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        out_le32(&pmac_irq_hw[i]->enable, cached_irq_mask[i]);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        do {
                /* make sure ack gets to controller before we enable interrupts */
		mb();
        } while(in_le32(&pmac_irq_hw[i]->flag) & bit);

}

void __openfirmware chrp_mask_and_ack_irq(unsigned int irq_nr)
{
	if (is_8259_irq(irq_nr))
	    i8259_mask_and_ack_irq(irq_nr);
}

static void pmac_set_irq_mask(int irq_nr)
{
	unsigned long bit = 1UL << (irq_nr & 0x1f);
	int i = irq_nr >> 5;

	if ((unsigned)irq_nr >= max_irqs)
		return;

	/* enable unmasked interrupts */
	out_le32(&pmac_irq_hw[i]->enable, cached_irq_mask[i]);
	
	do {
		/* make sure mask gets to controller before we
		   return to user */
		mb();
	} while((in_le32(&pmac_irq_hw[i]->enable) & bit)
		!= (cached_irq_mask[i] & bit));

	/*
	 * Unfortunately, setting the bit in the enable register
	 * when the device interrupt is already on *doesn't* set
	 * the bit in the flag register or request another interrupt.
	 */
	if ((bit & cached_irq_mask[i])
	    && (ld_le32(&pmac_irq_hw[i]->level) & bit)
	    && !(ld_le32(&pmac_irq_hw[i]->flag) & bit)) {
		if (!test_and_set_bit(irq_nr, lost_interrupts))
			atomic_inc(&n_lost_interrupts);
	}
}

static void pmac_mask_irq(unsigned int irq_nr)
{
	clear_bit(irq_nr, cached_irq_mask);
	pmac_set_irq_mask(irq_nr);
	mb();
}

static void pmac_unmask_irq(unsigned int irq_nr)
{
	set_bit(irq_nr, cached_irq_mask);
	pmac_set_irq_mask(irq_nr);
}

static void __openfirmware chrp_mask_irq(unsigned int irq_nr)
{
	if (is_8259_irq(irq_nr))
		i8259_mask_irq(irq_nr);
	else
		openpic_disable_irq(irq_to_openpic(irq_nr));
}

static void __openfirmware chrp_unmask_irq(unsigned int irq_nr)
{
	if (is_8259_irq(irq_nr))
		i8259_unmask_irq(irq_nr);
	else
		openpic_enable_irq(irq_to_openpic(irq_nr));
}

/* This routine will fix some missing interrupt values in the device tree
 * on the gatwick mac-io controller used by some PowerBooks
 */
static void __init pmac_fix_gatwick_interrupts(struct device_node *gw, int irq_base)
{
	struct device_node *node;
	int count;
	
	memset(gatwick_int_pool, 0, sizeof(gatwick_int_pool));
	node = gw->child;
	count = 0;
	while(node)
	{
		/* Fix SCC */
		if (strcasecmp(node->name, "escc") == 0)
			if (node->child) {
				if (node->child->n_intrs < 3) {
					node->child->intrs = &gatwick_int_pool[count];
					count += 3;
				}
				node->child->n_intrs = 3;				
				node->child->intrs[0].line = 15+irq_base;
				node->child->intrs[1].line =  4+irq_base;
				node->child->intrs[2].line =  5+irq_base;
				printk(KERN_INFO "irq: fixed SCC on second controller (%d,%d,%d)\n",
					node->child->intrs[0].line,
					node->child->intrs[1].line,
					node->child->intrs[2].line);
			}
		/* Fix media-bay & left SWIM */
		if (strcasecmp(node->name, "media-bay") == 0) {
			struct device_node* ya_node;

			if (node->n_intrs == 0)
				node->intrs = &gatwick_int_pool[count++];
			node->n_intrs = 1;
			node->intrs[0].line = 29+irq_base;
			printk(KERN_INFO "irq: fixed media-bay on second controller (%d)\n",
					node->intrs[0].line);
			
			ya_node = node->child;
			while(ya_node)
			{
				if (strcasecmp(ya_node->name, "floppy") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 19+irq_base;
					ya_node->intrs[1].line =  1+irq_base;
					printk(KERN_INFO "irq: fixed floppy on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				} 
				if (strcasecmp(ya_node->name, "ata4") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 14+irq_base;
					ya_node->intrs[1].line =  3+irq_base;
					printk(KERN_INFO "irq: fixed ide on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				} 
				ya_node = ya_node->sibling;
			}
		}
		node = node->sibling;
	}
	if (count > 10) {
		printk("WARNING !! Gatwick interrupt pool overflow\n");
		printk("  GATWICK_IRQ_POOL_SIZE = %d\n", GATWICK_IRQ_POOL_SIZE);
		printk("              requested = %d\n", count);
	}
}

#ifdef __SMP__
static void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	smp_message_recv();
}
#endif /* __SMP__ */


#else /* CONFIG_8xx */

static void mbx_i8259_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int bits, irq;

	/* A bug in the QSpan chip causes it to give us 0xff always
	 * when doing a character read.  So read 32 bits and shift.
	 * This doesn't seem to return useful values anyway, but
	 * read it to make sure things are acked.
	 * -- Cort
	 */
	irq = (inl(0x508) >> 24)&0xff;
	if ( irq != 0xff ) printk("iack %d\n", irq);
	
	outb(0x0C, 0x20);
	irq = inb(0x20) & 7;
	if (irq == 2)
	{
		outb(0x0C, 0xA0);
		irq = inb(0xA0);
		irq = (irq&7) + 8;
	}
	bits = 1UL << irq;
	irq += i8259_pic.irq_offset;
	dispatch_handler( regs, irq );
}

static void mbx_mask_and_ack(unsigned int irq_nr)
{
	/* this shouldn't be masked, we mask the 8259 if we need to -- Cort */
	if ( irq_nr != ISA_BRIDGE_INT )
		mbx_mask_irq(irq_nr);
	if ( irq_nr >= ppc8xx_pic.irq_offset )
		irq_nr -= ppc8xx_pic.irq_offset;
	/* clear the pending bits */
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend = 1 << (31-irq_nr);
}

static void mbx_mask_irq(unsigned int irq_nr)
{
	if ( irq_nr == ISA_BRIDGE_INT ) return;
	if ( irq_nr >= ppc8xx_pic.irq_offset )
		irq_nr -= ppc8xx_pic.irq_offset;
	cached_irq_mask[0] &= ~(1 << (31-irq_nr));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =	cached_irq_mask[0];
}

static void mbx_unmask_irq(unsigned int irq_nr)
{
	if ( irq_nr >= ppc8xx_pic.irq_offset )
		irq_nr -= ppc8xx_pic.irq_offset;
	cached_irq_mask[0] |= (1 << (31-irq_nr));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =	cached_irq_mask[0];
}
#endif /* CONFIG_8xx */

static void __init i8259_init(void)
{
	/* init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */
	outb(0xFF, 0x21); /* Mask all */
	/* init slave interrupt controller */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x08, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* edge triggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */
	outb(0xFF, 0xA1); /* Mask all */
	outb(cached_A1, 0xA1);
	outb(cached_21, 0x21);
	request_irq( i8259_pic.irq_offset + 2, no_action, SA_INTERRUPT,
		     "8259 secondary cascade", NULL );
	enable_irq(i8259_pic.irq_offset + 2);  /* Enable cascade interrupt */
}

void __init init_IRQ(void)
{
	extern void xmon_irq(int, void *, struct pt_regs *);
	int i;
	static int once = 0;
#ifndef CONFIG_8xx
	struct device_node *irqctrler;
	unsigned long addr;
	struct device_node *np;
	int second_irq = -999;
#endif
	if ( once )
		return;
	else
		once++;
	
#ifndef CONFIG_8xx
	switch (_machine)
	{
	case _MACH_Pmac:
		/* G3 powermacs have 64 interrupts, G3 Series PowerBook have 128, 
		   others have 32 */
		max_irqs = max_real_irqs = 32;
		irqctrler = find_devices("mac-io");
		if (irqctrler)
		{
			max_real_irqs = 64;
			if (irqctrler->next)
				max_irqs = 128;
			else
				max_irqs = 64;
		}
		for ( i = 0; i < max_real_irqs ; i++ )
			irq_desc[i].ctl = &pmac_pic;

		/* get addresses of first controller */
		if (irqctrler) {
			if  (irqctrler->n_addrs > 0) {
				addr = (unsigned long) 
					ioremap(irqctrler->addrs[0].address, 0x40);
				for (i = 0; i < 2; ++i)
					pmac_irq_hw[i] = (volatile struct pmac_irq_hw*)
						(addr + (2 - i) * 0x10);
			}
			
			/* get addresses of second controller */
			irqctrler = (irqctrler->next) ? irqctrler->next : NULL;
			if (irqctrler && irqctrler->n_addrs > 0) {
				addr = (unsigned long) 
					ioremap(irqctrler->addrs[0].address, 0x40);
				for (i = 2; i < 4; ++i)
					pmac_irq_hw[i] = (volatile struct pmac_irq_hw*)
						(addr + (4 - i) * 0x10);
			}
		}

		/* disable all interrupts in all controllers */
		for (i = 0; i * 32 < max_irqs; ++i)
			out_le32(&pmac_irq_hw[i]->enable, 0);
		
		/* get interrupt line of secondary interrupt controller */
		if (irqctrler) {
			second_irq = irqctrler->intrs[0].line;
			printk(KERN_INFO "irq: secondary controller on irq %d\n",
				(int)second_irq);
			if (device_is_compatible(irqctrler, "gatwick"))
				pmac_fix_gatwick_interrupts(irqctrler, max_real_irqs);
			for ( i = max_real_irqs ; i < max_irqs ; i++ )
				irq_desc[i].ctl = &gatwick_pic;
			request_irq( second_irq, gatwick_action, SA_INTERRUPT,
				     "gatwick cascade", 0 );
		}
		printk("System has %d possible interrupts\n", max_irqs);
		if (max_irqs != max_real_irqs)
			printk(KERN_DEBUG "%d interrupts on main controller\n",
				max_real_irqs);

#ifdef CONFIG_XMON
		request_irq(20, xmon_irq, 0, "NMI - XMON", 0);
#endif	/* CONFIG_XMON */
		break;
	case _MACH_chrp:
		if ( !(np = find_devices("pci") ) )
			printk("Cannot find pci to get ack address\n");
		else
		{
			chrp_int_ack_special = (volatile unsigned char *)
				(*(unsigned long *)get_property(np,
					"8259-interrupt-acknowledge", NULL));
		}
		for ( i = 16 ; i < 36 ; i++ )
			irq_desc[i].ctl = &open_pic;
		/* openpic knows that it's at irq 16 offset
		 * so we don't need to set it in the pic structure
		 * -- Cort
		 */
		openpic_init(1);
		for ( i = 0 ; i < 16  ; i++ )
			irq_desc[i].ctl = &i8259_pic;
		i8259_init();
#ifdef CONFIG_XMON
		request_irq(openpic_to_irq(HYDRA_INT_ADB_NMI),
			    xmon_irq, 0, "NMI", 0);
#endif	/* CONFIG_XMON */
#ifdef __SMP__
		request_irq(openpic_to_irq(OPENPIC_VEC_SPURIOUS),
			    openpic_ipi_action, 0, "IPI0", 0);
#endif	/* __SMP__ */
		break;
	case _MACH_prep:
		for ( i = 0 ; i < 16  ; i++ )
			irq_desc[i].ctl = &i8259_pic;
		i8259_init();
		break;
#ifdef CONFIG_APUS		
	case _MACH_apus:
		apus_init_IRQ();
		break;
#endif	
	}
#else /* CONFIG_8xx */
	ppc8xx_pic.irq_offset = 16;
	for ( i = 16 ; i < 32 ; i++ )
		irq_desc[i].ctl = &ppc8xx_pic;
	unmask_irq(CPM_INTERRUPT);

	for ( i = 0 ; i < 16 ; i++ )
		irq_desc[i].ctl = &i8259_pic;
	i8259_init();
	request_irq(ISA_BRIDGE_INT, mbx_i8259_action, 0, "8259 cascade", NULL);
	enable_irq(ISA_BRIDGE_INT);
#endif  /* CONFIG_8xx */
}

#ifdef __SMP__
unsigned char global_irq_holder = NO_PROC_ID;
unsigned volatile int global_irq_lock;
atomic_t global_irq_count;

atomic_t global_bh_count;
atomic_t global_bh_lock;

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
	stack = (unsigned long *) &str;
	for (i = 40; i ; i--) {
		unsigned long x = *++stack;
		if (x > (unsigned long) &init_task_union && x < (unsigned long) &vsprintf) {
			printk("<[%08lx]> ", x);
		}
	}
}

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
			/* don't worry about the lock race Linus found
			 * on intel here. -- Cort
			 */
			__cli();
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
	unsigned int loops = MAXCOUNT;

	if (test_and_set_bit(0,&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		do {
			do {
				if (loops-- == 0) {
					printk("get_irqlock(%d) waiting, global_irq_holder=%d\n", cpu, global_irq_holder);
#ifdef CONFIG_XMON
					xmon(0);
#endif
				}
			} while (test_bit(0,&global_irq_lock));
		} while (test_and_set_bit(0,&global_irq_lock));		
	}
	/* 
	 * We also need to make sure that nobody else is running
	 * in an interrupt context. 
	 */
	wait_on_irq(cpu);

	/*
	 * Ok, finally..
	 */
	global_irq_holder = cpu;
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
	
	__save_flags(flags);
	if (flags & (1 << 15)) {
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
	local_enabled = (flags >> 15) & 1;
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
#endif /* __SMP__ */

