/*
 * $Id: irq.c,v 1.91 1998/12/28 10:28:47 paulus Exp $
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
#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/mbx.h>
#endif

extern void process_int(unsigned long vec, struct pt_regs *fp);
extern void apus_init_IRQ(void);
extern void amiga_disable_irq(unsigned int irq);
extern void amiga_enable_irq(unsigned int irq);
static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }
static volatile unsigned char *chrp_int_ack_special;
extern volatile unsigned long ipi_count;
static void pmac_fix_gatwick_interrupts(struct device_node *gw, int irq_base);

#ifdef CONFIG_APUS
/* Rename a few functions. Requires the CONFIG_APUS protection. */
#define request_irq nop_ppc_request_irq
#define free_irq nop_ppc_free_irq
#define get_irq_list nop_get_irq_list
#endif
#ifndef CONFIG_8xx
void (*mask_and_ack_irq)(int irq_nr);
void (*mask_irq)(unsigned int irq_nr);
void (*unmask_irq)(unsigned int irq_nr);
#else /* CONFIG_8xx */
/* init_IRQ() happens too late for the MBX because we initialize the
 * CPM early and it calls request_irq() before we have these function
 * pointers initialized.
 */
#define mask_and_ack_irq(irq)	mbx_mask_irq(irq)
#define mask_irq(irq) mbx_mask_irq(irq)
#define unmask_irq(irq) mbx_unmask_irq(irq)
#endif /* CONFIG_8xx */

#define VEC_SPUR    (24)
#undef SHOW_IRQ
#undef SHOW_GATWICK_IRQS
#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
#define cached_21	(((char *)(cached_irq_mask))[3])
#define cached_A1	(((char *)(cached_irq_mask))[2])
#define PREP_IRQ_MASK	(((unsigned int)cached_A1)<<8) | (unsigned int)cached_21

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
int max_irqs;
int max_real_irqs;
static struct irqaction *irq_action[NR_IRQS];
static int spurious_interrupts = 0;
static unsigned int cached_irq_mask[NR_MASK_WORDS];
unsigned int lost_interrupts[NR_MASK_WORDS];
atomic_t n_lost_interrupts;

/* pmac */
struct pmac_irq_hw {
	unsigned int	flag;
	unsigned int	enable;
	unsigned int	ack;
	unsigned int	level;
};

/* XXX these addresses should be obtained from the device tree */
volatile struct pmac_irq_hw *pmac_irq_hw[4] = {
	(struct pmac_irq_hw *) 0xf3000020,
	(struct pmac_irq_hw *) 0xf3000010,
	(struct pmac_irq_hw *) 0xf4000020,
	(struct pmac_irq_hw *) 0xf4000010,
};

/* This is the interrupt used on the main controller for the secondary
   controller. Happens on PowerBooks G3 Series (a second mac-io)
   -- BenH
 */
static int second_irq = -999;

/* Returns the number of 0's to the left of the most significant 1 bit */
static inline int cntlzw(int bits)
{
	int lz;

	asm ("cntlzw %0,%1" : "=r" (lz) : "r" (bits));
	return lz;
}

static inline void sync(void)
{
	asm volatile ("sync");
}

/* nasty hack for shared irq's since we need to do kmalloc calls but
 * can't very very early in the boot when we need to do a request irq.
 * this needs to be removed.
 * -- Cort
 */
static char cache_bitmask = 0;
static struct irqaction malloc_cache[4];
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

#ifndef CONFIG_8xx	
void i8259_mask_and_ack_irq(int irq_nr)
{
  /*	spin_lock(&irq_controller_lock);*/
	cached_irq_mask[0] |= 1 << irq_nr;
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
	/*	spin_unlock(&irq_controller_lock);*/
}

void __pmac pmac_mask_and_ack_irq(int irq_nr)
{
	unsigned long bit = 1UL << (irq_nr & 0x1f);
	int i = irq_nr >> 5;

	if ((unsigned)irq_nr >= max_irqs)
		return;
	/*spin_lock(&irq_controller_lock);*/

	clear_bit(irq_nr, cached_irq_mask);
	if (test_and_clear_bit(irq_nr, lost_interrupts))
		atomic_dec(&n_lost_interrupts);
	out_le32(&pmac_irq_hw[i]->ack, bit);
	out_le32(&pmac_irq_hw[i]->enable, cached_irq_mask[i]);
	out_le32(&pmac_irq_hw[i]->ack, bit);
	/* make sure ack gets to controller before we enable interrupts */
	sync();

	/*spin_unlock(&irq_controller_lock);*/
	/*if ( irq_controller_lock.lock )
	  panic("irq controller lock still held in mask and ack\n");*/
}

void __openfirmware chrp_mask_and_ack_irq(int irq_nr)
{
	/* spinlocks are done by i8259_mask_and_ack() - Cort */
	if (is_8259_irq(irq_nr))
	    i8259_mask_and_ack_irq(irq_nr);
}


static void i8259_set_irq_mask(int irq_nr)
{
	if (irq_nr > 7) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

static void __pmac pmac_set_irq_mask(int irq_nr)
{
	unsigned long bit = 1UL << (irq_nr & 0x1f);
	int i = irq_nr >> 5;

	if ((unsigned)irq_nr >= max_irqs)
		return;

	/* enable unmasked interrupts */
	out_le32(&pmac_irq_hw[i]->enable, cached_irq_mask[i]);

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

/*
 * These have to be protected by the spinlock
 * before being called.
 */
static void i8259_mask_irq(unsigned int irq_nr)
{
	cached_irq_mask[0] |= 1 << irq_nr;
	i8259_set_irq_mask(irq_nr);
}

static void i8259_unmask_irq(unsigned int irq_nr)
{
	cached_irq_mask[0] &= ~(1 << irq_nr);
	i8259_set_irq_mask(irq_nr);
}

static void __pmac pmac_mask_irq(unsigned int irq_nr)
{
	clear_bit(irq_nr, cached_irq_mask);
	pmac_set_irq_mask(irq_nr);
	sync();
}

static void __pmac pmac_unmask_irq(unsigned int irq_nr)
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
#else /* CONFIG_8xx */
static void mbx_mask_irq(unsigned int irq_nr)
{
	cached_irq_mask[0] &= ~(1 << (31-irq_nr));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =
							cached_irq_mask[0];
}

static void mbx_unmask_irq(unsigned int irq_nr)
{
	cached_irq_mask[0] |= (1 << (31-irq_nr));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =
							cached_irq_mask[0];
}
#endif /* CONFIG_8xx */

void disable_irq(unsigned int irq_nr)
{
	/*unsigned long flags;*/

	/*	spin_lock_irqsave(&irq_controller_lock, flags);*/
	mask_irq(irq_nr);
	/*	spin_unlock_irqrestore(&irq_controller_lock, flags);*/
	synchronize_irq();
}

void enable_irq(unsigned int irq_nr)
{
	/*unsigned long flags;*/

	/*	spin_lock_irqsave(&irq_controller_lock, flags);*/
	unmask_irq(irq_nr);
	/*	spin_unlock_irqrestore(&irq_controller_lock, flags);*/
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
		action = irq_action[i];
		if ((!action || !action->handler) && (i != second_irq))
			continue;
		len += sprintf(buf+len, "%3d: ", i);		
#ifdef __SMP__
		for (j = 0; j < smp_num_cpus; j++)
			len += sprintf(buf+len, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#else		
		len += sprintf(buf+len, "%10u ", kstat_irqs(i));
#endif /* __SMP__ */
		switch( _machine )
		{
		case _MACH_prep:
			len += sprintf(buf+len, "    82c59 ");
			break;
		case _MACH_Pmac:
			if (i < 64)
				len += sprintf(buf+len, " PMAC-PIC ");
			else
				len += sprintf(buf+len, "  GATWICK ");
			break;
		case _MACH_chrp:
			if ( is_8259_irq(i) )
				len += sprintf(buf+len, "    82c59 ");
			else
				len += sprintf(buf+len, "  OpenPIC ");
			break;
		case _MACH_mbx:
			len += sprintf(buf+len, "   MPC8xx ");
			break;
		}

		if (i != second_irq) {
			len += sprintf(buf+len, "    %s",action->name);
			for (action=action->next; action; action = action->next) {
				len += sprintf(buf+len, ", %s", action->name);
			}
			len += sprintf(buf+len, "\n");
		} else
			len += sprintf(buf+len, "    Gatwick secondary IRQ controller\n");
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
		len += sprintf(buf+len,"          ");
	len += sprintf(buf+len, "     spurious or short\n");
	return len;
}


/*
 * Global interrupt locks for SMP. Allow interrupts to come in on any
 * CPU, yet make cli/sti act globally to protect critical regions..
 */
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

#define MAXCOUNT 100000000
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

asmlinkage void do_IRQ(struct pt_regs *regs, int isfake)
{
	int irq;
	unsigned long bits;
	struct irqaction *action;
	int cpu = smp_processor_id();
	int status;
	int openpic_eoi_done = 0;

	/* save the HID0 in case dcache was off - see idle.c
	 * this hack should leave for a better solution -- Cort */
	unsigned dcache_locked;
	
	dcache_locked = unlock_dcache();	
	hardirq_enter(cpu);
#ifndef CONFIG_8xx		  
#ifdef __SMP__
	if ( cpu != 0 )
	{
		if (!isfake)
		{
			extern void smp_message_recv(void);
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

		/* Here, we handle interrupts coming from Gatwick,
		 * normal interrupt code will take care of acking and
		 * masking the irq on Gatwick itself but we ack&mask
		 * the Gatwick main interrupt on Heathrow now. It's
		 * unmasked later, after interrupt handling. -- BenH 
		 */
		if (irq == second_irq) {
			mask_and_ack_irq(second_irq);
			for (irq = max_irqs - 1; irq > max_real_irqs; irq -= 32) {
				int i = irq >> 5;
				bits = ld_le32(&pmac_irq_hw[i]->flag)
					| lost_interrupts[i];
				if (bits == 0)
					continue;
				irq -= cntlzw(bits);
				break;
			}
			/* If not found, on exit, irq is 63 (128-1-32-32).
			 * We set it to -1 and revalidate second controller
			 */
			if (irq < max_real_irqs) {
				irq = -1;
				unmask_irq(second_irq);
			}
#ifdef SHOW_GATWICK_IRQS
			printk("Gatwick irq %d (i:%d, bits:0x%08lx\n", irq, i, bits);
#endif
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
		else if (irq >= OPENPIC_VEC_TIMER)
		{
			/*
			 *  OpenPIC interrupts >64 will be used for other purposes
			 *  like interprocessor interrupts and hardware errors
			 */
			if (irq == OPENPIC_VEC_SPURIOUS) {
				/*
				 * Spurious interrupts should never be
				 * acknowledged
				 */
				spurious_interrupts++;
				openpic_eoi_done = 1;
			} else {
				/*
				 * Here we should process IPI timer
				 * for now the interrupt is dismissed.
				 */
			}
			goto out;
		}
		break;
	case _MACH_prep:
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

	if (irq < 0) {
		/* we get here with Gatwick but the 'bogus' isn't correct in that case -- Cort */
		if ( irq != second_irq )
		{
			printk(KERN_DEBUG "Bogus interrupt %d from PC = %lx\n",
			       irq, regs->nip);
			spurious_interrupts++;
		}
		goto out;
	}					
	
#else /* CONFIG_8xx */
	/* For MPC8xx, read the SIVEC register and shift the bits down
	 * to get the irq number.
	 */
	bits = ((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec;
	irq = bits >> 26;
#endif /* CONFIG_8xx */
	mask_and_ack_irq(irq);
	status = 0;
	action = irq_action[irq];
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
#ifndef CONFIG_8xx	  
		if ( irq == 7 ) /* i8259 gives us irq 7 on 'short' intrs */
#endif		  
			spurious_interrupts++;
		disable_irq( irq );
	}
	
	/* This was a gatwick sub-interrupt, we re-enable them on Heathrow
           now */
	if (_machine == _MACH_Pmac && irq >= max_real_irqs)
		unmask_irq(second_irq);

	/* make sure we don't miss any cascade intrs due to eoi-ing irq 2 */
#ifndef CONFIG_8xx											
	if ( is_prep && (irq > 7) )
		goto retry_cascade;
	/* do_bottom_half is called if necessary from int_return in head.S */
out:
	if (_machine == _MACH_chrp && !openpic_eoi_done)
		openpic_eoi(0);
#endif /* CONFIG_8xx */
	hardirq_exit(cpu);

#ifdef CONFIG_APUS
out2:
#endif
	/* restore the HID0 in case dcache was off - see idle.c
	 * this hack should leave for a better solution -- Cort */
	lock_dcache(dcache_locked);
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *old, **p, *action;
	unsigned long flags;

#ifdef SHOW_IRQ
	printk("request_irq(): irq %d handler %08x name %s dev_id %04x\n",
	       irq,(int)handler,devname,(int)dev_id);
#endif /* SHOW_IRQ */

	if (irq >= NR_IRQS)
		return -EINVAL;

	/* Cannot allocate second controller IRQ */
	if (irq == second_irq)
		return -EBUSY;

	if (!handler)
	{
		/* Free */
		for (p = irq + irq_action; (action = *p) != NULL; p = &action->next)
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
	p = irq_action + irq;
	
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

unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

#ifndef CONFIG_8xx 
__initfunc(static void i8259_init(void))
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
	if (request_irq(2, no_action, SA_INTERRUPT, "cascade", NULL) != 0)
		panic("Could not allocate cascade IRQ!");
	enable_irq(2);  /* Enable cascade interrupt */
}
#endif /* CONFIG_8xx */

/* On MBX8xx, the interrupt control (SIEL) was set by EPPC-bug.  External
 * interrupts can be either edge or level triggered, but there is no
 * reason for us to change the EPPC-bug values (it would not work if we did).
 */
__initfunc(void init_IRQ(void))
{
	extern void xmon_irq(int, void *, struct pt_regs *);
	int i;
	struct device_node *irqctrler;
	unsigned long addr;
	struct device_node *np;

#ifndef CONFIG_8xx
	switch (_machine)
	{
	case _MACH_Pmac:
		mask_and_ack_irq = pmac_mask_and_ack_irq;
		mask_irq = pmac_mask_irq;
		unmask_irq = pmac_unmask_irq;

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
			enable_irq(second_irq);
		}
		printk("System has %d possible interrupts\n", max_irqs);
		if (max_irqs != max_real_irqs)
			printk(KERN_DEBUG "%d interrupts on main controller\n",
				max_real_irqs);

#ifdef CONFIG_XMON
		request_irq(20, xmon_irq, 0, "NMI", 0);
#endif	/* CONFIG_XMON */
		break;
	case _MACH_chrp:
		mask_and_ack_irq = chrp_mask_and_ack_irq;
		mask_irq = chrp_mask_irq;
		unmask_irq = chrp_unmask_irq;
		
		if ( !(np = find_devices("pci") ) )
			printk("Cannot find pci to get ack address\n");
		else
		{
			chrp_int_ack_special = (volatile unsigned char *)
			   (*(unsigned long *)get_property(np,
					"8259-interrupt-acknowledge", NULL));
		}
		openpic_init(1);
		i8259_init();
		cached_irq_mask[0] = cached_irq_mask[1] = ~0UL;
#ifdef CONFIG_XMON
		request_irq(openpic_to_irq(HYDRA_INT_ADB_NMI),
			    xmon_irq, 0, "NMI", 0);
#endif	/* CONFIG_XMON */
		break;
	case _MACH_prep:
		mask_and_ack_irq = i8259_mask_and_ack_irq;
		mask_irq = i8259_mask_irq;
		unmask_irq = i8259_unmask_irq;
		cached_irq_mask[0] = ~0UL;
		
		i8259_init();
		/*
		 * According to the Carolina spec from ibm irqs 0,1,2, and 8
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
			if ( _prep_type == _PREP_IBM )
				irq_mode2 |= 0xa0;
		}
		break;
#ifdef CONFIG_APUS		
	case _MACH_apus:
		mask_irq = amiga_disable_irq;
		unmask_irq = amiga_enable_irq;
		apus_init_IRQ();
		break;
#endif	
	}
#endif /* CONFIG_8xx */
}

/* This routine will fix some missing interrupt values in the device tree
 * on the gatwick mac-io controller used by some PowerBooks
 */
static void __init pmac_fix_gatwick_interrupts(struct device_node *gw, int irq_base)
{
	struct device_node *node;
	static struct interrupt_info int_pool[4];
	
	memset(int_pool, 0, sizeof(int_pool));
	node = gw->child;
	while(node)
	{
		/* Fix SCC */
		if (strcasecmp(node->name, "escc") == 0)
			if (node->child && node->child->n_intrs == 0)
			{
				node->child->n_intrs = 1;
				node->child->intrs = &int_pool[0];
				int_pool[0].line = 15+irq_base;
				printk(KERN_INFO "irq: fixed SCC on second controller (%d)\n",
					int_pool[0].line);
			}
		/* Fix media-bay & left SWIM */
		if (strcasecmp(node->name, "media-bay") == 0)
		{
			struct device_node* ya_node;

			if (node->n_intrs == 0)
			{
				node->n_intrs = 1;
				node->intrs = &int_pool[1];
				int_pool[1].line = 29+irq_base;
				printk(KERN_INFO "irq: fixed media-bay on second controller (%d)\n",
					int_pool[1].line);
			}
			ya_node = node->child;
			while(ya_node)
			{
				if ((strcasecmp(ya_node->name, "floppy") == 0) &&
					ya_node->n_intrs == 0)
				{
					ya_node->n_intrs = 2;
					ya_node->intrs = &int_pool[2];
					int_pool[2].line = 19+irq_base;
					int_pool[3].line =  1+irq_base;
					printk(KERN_INFO "irq: fixed floppy on second controller (%d,%d)\n",
						int_pool[2].line, int_pool[3].line);
				} 
				ya_node = ya_node->sibling;
			}
		}
		node = node->sibling;
	}
	
}

