/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/dma.h>

extern void timer_interrupt(struct pt_regs * regs);

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
  static unsigned char cache_804 = 0xef;
  static unsigned char cache_805 = 0xff;
  static unsigned char cache_806 = 0xff;
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P)
  static unsigned char cache_26 = 0xdf;
  static unsigned char cache_27 = 0xff;
#endif

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	save_flags(flags);
	cli();
	mask = 1 << (irq_nr & 7);

	if (irq_nr < 8) {
		cache_21 |= mask;
		outb(cache_21,0x21);
	} else if (irq_nr < 16) {
		cache_A1 |= mask;
		outb(cache_A1,0xA1);
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	} else if (irq_nr < 24) {
		cache_804 |= mask;
		outb(cache_804, 0x804);
	} else if (irq_nr < 32) {
		cache_805 |= mask;
		outb(cache_805, 0x805);
	} else {
		cache_806 |= mask;
		outb(cache_806, 0x806);
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P) 
	} else if (irq_nr < 24) {
		cache_26 |= mask;
		outb(cache_26, 0x26);
	} else {
		cache_27 |= mask;
		outb(cache_27, 0x27);
#endif
	}
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	save_flags(flags);
	cli();

	if (irq_nr < 8) {
		cache_21 &= mask;
		outb(cache_21,0x21);
	} else if (irq_nr < 16) {
		cache_A1 &= mask;
		outb(cache_A1,0xA1);
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	} else if (irq_nr < 24) {
		cache_804 &= mask;
		outb(cache_804, 0x804);
	} else if (irq_nr < 32) {
		cache_805 &= mask;
		outb(cache_805, 0x805);
	} else {
		cache_806 &= mask;
		outb(cache_806, 0x806);
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P)
	} else if (irq_nr < 24) {
		cache_26 &= mask;
		outb(cache_26, 0x26);
	} else {
		cache_27 &= mask;
		outb(cache_27, 0x27);
#endif
	}
	restore_flags(flags);
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

static struct irqaction irq_action[NR_IRQS];

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action = irq_action;

	for (i = 0 ; i < NR_IRQS ; i++, action++) {
		if (!action->handler)
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s\n",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
	}
	return len;
}

static inline void ack_irq(int irq)
{
	if (irq < 16) {
		/* ACK the interrupt making it the lowest priority */
		/*  First the slave .. */
		if (irq > 7) {
			outb(0xE0 | (irq - 8), 0xa0);
			irq = 2;
		}
		/* .. then the master */
		outb(0xE0 | irq, 0x20);
	}
}

static inline void mask_irq(int irq)
{
	unsigned char mask;

	mask = 1 << (irq & 7);
	if (irq < 8) {
		cache_21 |= mask;
		outb(cache_21, 0x21);
	} else if (irq < 16) {
		cache_A1 |= mask;
		outb(cache_A1, 0xA1);
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	} else if (irq < 24) {
		cache_804 |= mask;
		outb(cache_804, 0x804);
	} else if (irq < 32) {
	        cache_805 |= mask;
		outb(cache_805, 0x805);
	} else {
		cache_806 |= mask;
		outb(cache_806, 0x806);
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB66P)
	} else if (irq < 24) {
		cache_26 |= mask;
		outb(cache_26, 0x26);
	} else {
		cache_27 |= mask;
		outb(cache_27, 0x27);
#endif
	}
}

static inline void unmask_irq(unsigned long irq)
{
	unsigned char mask = ~(1 << (irq & 7));

	if (irq < 8) {
		cache_21 &= mask;
		outb(cache_21, 0x21);
	} else if (irq < 16) {
		cache_A1 &= mask;
		outb(cache_A1, 0xA1);
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	} else if (irq < 24) {
		cache_804 &= mask;
		outb(cache_804, 0x804);
	} else if (irq < 32) {
	        cache_805 &= mask;
		outb(cache_805, 0x805);
	} else {
		cache_806 &= mask;
		outb(cache_806, 0x806);
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB66P)
	} else if (irq < 24) {
		cache_26 &= mask;
		outb(cache_26, 0x26);
	} else {
		cache_27 &= mask;
		outb(cache_27, 0x27);
#endif
	}
}

int request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	unsigned long irqflags, const char * devname)
{
	struct irqaction * action;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return -EINVAL;
	/* don't accept requests for irq #0 */
	if (!irq)
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
	enable_irq(irq);
	if (irq >= 8 && irq < 16) {
		enable_irq(2);	/* ensure cascade is enabled too */
	}
	restore_flags(flags);
	return 0;
}

void free_irq(unsigned int irq)
{
	struct irqaction * action = irq + irq_action;
	unsigned long flags;

	if (irq >= NR_IRQS) {
		printk("Trying to free IRQ%d\n", irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n", irq);
		return;
	}
	save_flags(flags);
	cli();
	mask_irq(irq);
	action->handler = NULL;
	action->flags = 0;
	action->mask = 0;
	action->name = NULL;
	restore_flags(flags);
}

static inline void handle_nmi(struct pt_regs * regs)
{
	printk("Whee.. NMI received. Probable hardware error\n");
	printk("61=%02x, 461=%02x\n", inb(0x61), inb(0x461));
}

static void unexpected_irq(int irq, struct pt_regs * regs)
{
	int i;

	printk("IO device interrupt, irq = %d\n", irq);
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
	printk("Expecting: ");
	for (i = 0; i < 16; i++)
		if (irq_action[i].handler)
			printk("[%s:%d] ", irq_action[i].name, i);
	printk("\n");
#if defined(CONFIG_ALPHA_JENSEN)
	printk("64=%02x, 60=%02x, 3fa=%02x 2fa=%02x\n",
		inb(0x64), inb(0x60), inb(0x3fa), inb(0x2fa));
	outb(0x0c, 0x3fc);
	outb(0x0c, 0x2fc);
	outb(0,0x61);
	outb(0,0x461);
#elif defined(CONFIG_ALPHA_NONAME)
	printk("61=%02x, 64=%02x, 60=%02x\n", inb(0x61), inb(0x64), inb(0x60));
#endif
}

static inline void handle_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq + irq_action;

	kstat.interrupts[irq]++;
	if (!action->handler) {
		unexpected_irq(irq, regs);
		return;
	}
	action->handler(irq, regs);
}

static inline void device_interrupt(int irq, int ack, struct pt_regs * regs)
{
	struct irqaction * action;

	if ((unsigned) irq > NR_IRQS) {
		printk("device_interrupt: unexpected interrupt %d\n", irq);
		return;
	}

	kstat.interrupts[irq]++;
	action = irq_action + irq;
#ifdef CONFIG_RANDOM
	if (action->flags & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
#endif
	/* quick interrupts get executed with no extra overhead */
	if (action->flags & SA_INTERRUPT) {
		action->handler(irq, regs);
		ack_irq(ack);
		return;
	}
	/*
	 * For normal interrupts, we mask it out, and then ACK it.
	 * This way another (more timing-critical) interrupt can
	 * come through while we're doing this one.
	 *
	 * Note! A irq without a handler gets masked and acked, but
	 * never unmasked. The autoirq stuff depends on this (it looks
	 * at the masks before and after doing the probing).
	 */
	mask_irq(ack);
	ack_irq(ack);
	if (!action->handler)
		return;
	action->handler(irq, regs);
	unmask_irq(ack);
}

/*
 * Handle ISA interrupt via the PICs.
 */
static inline void isa_device_interrupt(unsigned long vector,
					struct pt_regs * regs)
{
	unsigned long pic;
	int j;
	/* 
	 *  The first read of gives you *all* interrupting lines.
	 *  Therefore, read the mask register and and out those lines
	 *  not enabled.  Note that some documentation has 21 and a1 
	 *  write only.  This is not true.
	 */
	pic = inb(0x20) | (inb(0xA0) << 8);	/* read isr */
	pic &= ~((cache_A1 << 8) | cache_21);	/* apply mask */
	pic &= 0xFFFB;				/* mask out cascade */

	while (pic) {
		j = ffz(~pic);
		pic &= pic - 1;
		device_interrupt(j, j, regs);
	}
}

static inline void cabriolet_and_eb66p_device_interrupt(unsigned long vector,
							struct pt_regs * regs)
{
	unsigned long pld;
	unsigned int i;
	unsigned long flags;

	save_flags(flags);
	cli();

	/* read the interrupt summary registers */
	pld = inb(0x804) | (inb(0x805) << 8) | (inb(0x806) << 16);

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1;	/* clear least bit set */
		if (i == 4) {
			isa_device_interrupt(vector, regs);
		} else {
			device_interrupt(16 + i, 16 + i, regs);
		}
	}
	restore_flags(flags);
}

static inline void eb66_and_eb64p_device_interrupt(unsigned long vector,
						   struct pt_regs * regs)
{
	unsigned long pld;
	unsigned int i;
	unsigned long flags;

	save_flags(flags);
	cli();

	/* read the interrupt summary registers */
	pld = inb(0x26) | (inb(0x27) << 8);
	/*
	 * Now, for every possible bit set, work through
	 * them and call the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1;	/* clear least bit set */

		if (i == 5) {
			isa_device_interrupt(vector, regs);
		} else {
			device_interrupt(16 + i, 16 + i, regs);
		}
	}
	restore_flags(flags);
}

/*
 * Jensen is special: the vector is 0x8X0 for EISA interrupt X, and
 * 0x9X0 for the local motherboard interrupts..
 *
 *	0x660 - NMI
 *
 *	0x800 - IRQ0  interval timer (not used, as we use the RTC timer)
 *	0x810 - IRQ1  line printer (duh..)
 *	0x860 - IRQ6  floppy disk
 *	0x8E0 - IRQ14 SCSI controller
 *
 *	0x900 - COM1
 *	0x920 - COM2
 *	0x980 - keyboard
 *	0x990 - mouse
 *
 * PCI-based systems are more sane: they don't have the local
 * interrupts at all, and have only normal PCI interrupts from
 * devices.  Happily it's easy enough to do a sane mapping from the
 * Jensen..  Note that this means that we may have to do a hardware
 * "ack" to a different interrupt than we report to the rest of the
 * world.
 */
static inline void srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;

#ifdef CONFIG_ALPHA_JENSEN
	switch (vector) {
	      case 0x660: handle_nmi(regs); return;
		/* local device interrupts: */
	      case 0x900: handle_irq(4, regs); return;	/* com1 -> irq 4 */
	      case 0x920: handle_irq(3, regs); return;	/* com2 -> irq 3 */
	      case 0x980: handle_irq(1, regs); return;	/* kbd -> irq 1 */
	      case 0x990: handle_irq(9, regs); return;	/* mouse -> irq 9 */
	      default:
		if (vector > 0x900) {
			printk("Unknown local interrupt %lx\n", vector);
		}
	}
	/* irq1 is supposed to be the keyboard, silly Jensen (is this really needed??) */
	if (irq == 1)
		irq = 7;
#endif /* CONFIG_ALPHA_JENSEN */

	device_interrupt(irq, ack, regs);
}

#if NR_IRQS > 64
#  error Number of irqs limited to 64 due to interrupt-probing.
#endif

/*
 * Start listening for interrupts..
 */
unsigned long probe_irq_on(void)
{
	unsigned long irqs = 0, irqmask;
	unsigned long delay;
	unsigned int i;

	for (i = NR_IRQS - 1; i > 0; i--) {
		if (!irq_action[i].handler) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		/* about 100 ms delay */;
	
	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned long)cache_A1)<<8) | (unsigned long) cache_21;
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	irqmask |= ((((unsigned long)cache_804)<<16) |
		    (((unsigned long)cache_805)<<24) |
		    (((unsigned long)cache_806)<<24));
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P)
	irqmask |= ((((unsigned long)cache_26)<<16) |
		    (((unsigned long)cache_27)<<24));
#endif
	irqs &= ~irqmask;
	return irqs;
}

/*
 * Get the result of the IRQ probe.. A negative result means that
 * we have several candidates (but we return the lowest-numbered
 * one).
 */
int probe_irq_off(unsigned long irqs)
{
	unsigned long irqmask;
	int i;
	
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	irqmask |= ((((unsigned long)cache_804)<<16) |
		    (((unsigned long)cache_805)<<24) |
		    (((unsigned long)cache_806)<<24));
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P)
	irqmask |= ((((unsigned long)cache_26)<<16) |
		    (((unsigned long)cache_27)<<24));
#endif
	irqs &= irqmask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (1UL << i))
		i = -i;
	return i;
}

static void machine_check(unsigned long vector, unsigned long la, struct pt_regs * regs)
{
#if defined(CONFIG_ALPHA_LCA)
	extern void lca_machine_check (unsigned long vector, unsigned long la,
				       struct pt_regs *regs);
	lca_machine_check(vector, la, regs);
#elif defined(CONFIG_ALPHA_APECS)
	extern void apecs_machine_check(unsigned long vector, unsigned long la,
					struct pt_regs * regs);
	apecs_machine_check(vector, la, regs);
#else
	printk("Machine check\n");
#endif
}

asmlinkage void do_entInt(unsigned long type, unsigned long vector, unsigned long la_ptr,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	switch (type) {
		case 0:
			printk("Interprocessor interrupt? You must be kidding\n");
			break;
		case 1:
			timer_interrupt(&regs);
			return;
		case 2:
			machine_check(vector, la_ptr, &regs);
			break;
		case 3:
#if defined(CONFIG_ALPHA_JENSEN) || defined(CONFIG_ALPHA_NONAME) || \
    defined(CONFIG_ALPHA_SRM)
			srm_device_interrupt(vector, &regs);
#elif defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
			cabriolet_and_eb66p_device_interrupt(vector, &regs);
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P)
			eb66_and_eb64p_device_interrupt(vector, &regs);
#endif
			return;
		case 4:
			printk("Performance counter interrupt\n");
			break;;
		default:
			printk("Hardware intr %ld %lx? Huh?\n", type, vector);
	}
	printk("PC = %016lx PS=%04lx\n", regs.pc, regs.ps);
}

extern asmlinkage void entInt(void);

void init_IRQ(void)
{
	wrent(entInt, 0);
	dma_outb(0, DMA1_RESET_REG);
	dma_outb(0, DMA2_RESET_REG);
	dma_outb(0, DMA1_CLR_MASK_REG);
	dma_outb(0, DMA2_CLR_MASK_REG);
#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P)
	outb(cache_804, 0x804);
	outb(cache_805, 0x805);
	outb(cache_806, 0x806);
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P)
	outb(cache_26, 0x26);
	outb(cache_27, 0x27);
#endif
}
