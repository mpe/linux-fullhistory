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
#include <linux/malloc.h>
#include <linux/random.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/dma.h>

#define RTC_IRQ    8
#ifdef CONFIG_RTC
#define TIMER_IRQ  0        /* timer is the pit */
#else
#define TIMER_IRQ  RTC_IRQ  /* the timer is, in fact, the rtc */
#endif

#if NR_IRQS > 64
#  error Unable to handle more than 64 irq levels.
#endif

/* PROBE_MASK is the bitset of irqs that we consider for autoprobing: */
#if defined(CONFIG_ALPHA_P2K)
  /* always mask out unused timer irq 0 and RTC irq 8 */
# define PROBE_MASK (((1UL << NR_IRQS) - 1) & ~0x101UL)
#elif defined(CONFIG_ALPHA_ALCOR)
  /* always mask out unused timer irq 0, "irqs" 20-30, and the EISA cascade: */
# define PROBE_MASK (((1UL << NR_IRQS) - 1) & ~0xfff000000001UL)
#else
  /* always mask out unused timer irq 0: */
# define PROBE_MASK (((1UL << NR_IRQS) - 1) & ~1UL)
#endif

/* Reserved interrupts.  These must NEVER be requested by any driver!
 */
#define	IS_RESERVED_IRQ(irq)	((irq)==2)	/* IRQ 2 used by hw cascade */

/*
 * Shadow-copy of masked interrupts.
 *  The bits are used as follows:
 *	 0.. 7	first (E)ISA PIC (irq level 0..7)
 *	 8..15	second (E)ISA PIC (irq level 8..15)
 *   Systems with PCI interrupt lines managed by GRU (e.g., Alcor, XLT):
 *	16..47	PCI interrupts 0..31 (int at GRU_INT_MASK)
 *   Mikasa:
 *	16..31	PCI interrupts 0..15 (short at I/O port 536)
 *   Other systems (not Mikasa) with 16 PCI interrupt lines:
 *	16..23	PCI interrupts 0.. 7 (char at I/O port 26)
 *	24..31	PCI interrupts 8..15 (char at I/O port 27)
 *   Systems with 17 PCI interrupt lines (e.g., Cabriolet and eb164):
 *	16..32	PCI interrupts 0..31 (int at I/O port 804)
 */
static unsigned long irq_mask = ~0UL;

/*
 * Update the hardware with the irq mask passed in MASK.  The function
 * exploits the fact that it is known that only bit IRQ has changed.
 */
static void update_hw(unsigned long irq, unsigned long mask)
{
	switch (irq) {
#if NR_IRQS == 48
	      default:
		/* note inverted sense of mask bits: */
		*(unsigned int *)GRU_INT_MASK = ~(mask >> 16); mb();
		break;

#elif NR_IRQS == 33
	      default:
		outl(mask >> 16, 0x804);
		break;

#elif defined(CONFIG_ALPHA_MIKASA)
	      default:
		outw(~(mask >> 16), 0x536); /* note invert */
		break;

#elif NR_IRQS == 32
	      case 16 ... 23:
		outb(mask >> 16, 0x26);
		break;

	      default:
		outb(mask >> 24, 0x27);
		break;
#endif
		/* handle ISA irqs last---fast devices belong on PCI... */

	      case  0 ... 7:	/* ISA PIC1 */
		outb(mask, 0x21);
		break;

	      case  8 ...15:	/* ISA PIC2 */
		outb(mask >> 8, 0xA1);
		break;
	}
}

static inline void mask_irq(unsigned long irq)
{
	irq_mask |= (1UL << irq);
	update_hw(irq, irq_mask);
}

static inline void unmask_irq(unsigned long irq)
{
	irq_mask &= ~(1UL << irq);
	update_hw(irq, irq_mask);
}

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	mask_irq(irq_nr);
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	unmask_irq(irq_nr);
	restore_flags(flags);
}

/*
 * Initial irq handlers.
 */
static struct irqaction timer_irq = { NULL, 0, 0, NULL, NULL, NULL};
static struct irqaction *irq_action[NR_IRQS];

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %10u %c %s",
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
#if defined(CONFIG_ALPHA_ALCOR) || defined(CONFIG_ALPHA_XLT)
		/* on ALCOR/XLT, need to dismiss interrupt via GRU */
		*(int *)GRU_INT_CLEAR = 0x80000000; mb();
		*(int *)GRU_INT_CLEAR = 0x00000000; mb();
#endif /* ALCOR || XLT */
	}
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int shared = 0;
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (IS_RESERVED_IRQ(irq))
		return -EINVAL;
	if (!handler)
		return -EINVAL;
	p = irq_action + irq;
	action = *p;
	if (action) {
		/* Can't share interrupts unless both agree to */
		if (!(action->flags & irqflags & SA_SHIRQ))
			return -EBUSY;

		/* Can't share interrupts unless both are same type */
		if ((action->flags ^ irqflags) & SA_INTERRUPT)
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &action->next;
			action = *p;
		} while (action);
		shared = 1;
	}

        if (irq == TIMER_IRQ)
 		action = &timer_irq;
        else
		action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
					     GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	if (irqflags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	save_flags(flags);
	cli();
	*p = action;

	if (!shared)
		unmask_irq(irq);

	restore_flags(flags);
	return 0;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= NR_IRQS) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (IS_RESERVED_IRQ(irq)) {
		printk("Trying to free reserved IRQ %d\n", irq);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_flags(flags);
		cli();
		*p = action->next;
		if (!irq[irq_action])
			mask_irq(irq);
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

static inline void handle_nmi(struct pt_regs * regs)
{
	printk("Whee.. NMI received. Probable hardware error\n");
	printk("61=%02x, 461=%02x\n", inb(0x61), inb(0x461));
}

unsigned int local_irq_count[NR_CPUS];
atomic_t __alpha_bh_counter;

#ifdef __SMP__
#error Me no hablo Alpha SMP
#else
#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])
#endif

static void unexpected_irq(int irq, struct pt_regs * regs)
{
	struct irqaction *action;
	int i;

	printk("IO device interrupt, irq = %d\n", irq);
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
	printk("Expecting: ");
	for (i = 0; i < 16; i++)
		if ((action = irq_action[i]))
			while (action->handler) {
				printk("[%s:%d] ", action->name, i);
				action = action->next;
			}
	printk("\n");
#if defined(CONFIG_ALPHA_JENSEN)
	printk("64=%02x, 60=%02x, 3fa=%02x 2fa=%02x\n",
		inb(0x64), inb(0x60), inb(0x3fa), inb(0x2fa));
	outb(0x0c, 0x3fc);
	outb(0x0c, 0x2fc);
	outb(0,0x61);
	outb(0,0x461);
#endif
}

static inline void handle_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq_action[irq];
	int cpu = smp_processor_id();

	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;
	if (!action) {
		unexpected_irq(irq, regs);
	} else {
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	}
	irq_exit(cpu, irq);
}

static inline void device_interrupt(int irq, int ack, struct pt_regs * regs)
{
	struct irqaction * action;
	int cpu = smp_processor_id();

	if ((unsigned) irq > NR_IRQS) {
		printk("device_interrupt: unexpected interrupt %d\n", irq);
		return;
	}

	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;
	action = irq_action[irq];
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
	if (action) {
		if (action->flags & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
		unmask_irq(ack);
	}
	irq_exit(cpu, irq);
}

#ifdef CONFIG_PCI

/*
 * Handle ISA interrupt via the PICs.
 */
static inline void isa_device_interrupt(unsigned long vector,
					struct pt_regs * regs)
{
#if defined(CONFIG_ALPHA_APECS)
#	define IACK_SC	APECS_IACK_SC
#elif defined(CONFIG_ALPHA_LCA)
#	define IACK_SC	LCA_IACK_SC
#elif defined(CONFIG_ALPHA_CIA)
#	define IACK_SC	CIA_IACK_SC
#else
	/*
	 * This is bogus but necessary to get it to compile
	 * on all platforms.  If you try to use this on any
	 * other than the intended platforms, you'll notice
	 * real fast...
	 */
#	define IACK_SC	1L
#endif
	int j;

#if 1
	/*
	 * Generate a PCI interrupt acknowledge cycle.  The PIC will
	 * respond with the interrupt vector of the highest priority
	 * interrupt that is pending.  The PALcode sets up the
	 * interrupts vectors such that irq level L generates vector
	 * L.
	 */
	j = *(volatile int *) IACK_SC;
	j &= 0xff;
	if (j == 7) {
		if (!(inb(0x20) & 0x80)) {
			/* it's only a passive release... */
			return;
		}
	}
	device_interrupt(j, j, regs);
#else
	unsigned long pic;

	/*
	 * It seems to me that the probability of two or more *device*
	 * interrupts occurring at almost exactly the same time is
	 * pretty low.  So why pay the price of checking for
	 * additional interrupts here if the common case can be
	 * handled so much easier?
	 */
	/* 
	 *  The first read of gives you *all* interrupting lines.
	 *  Therefore, read the mask register and and out those lines
	 *  not enabled.  Note that some documentation has 21 and a1 
	 *  write only.  This is not true.
	 */
	pic = inb(0x20) | (inb(0xA0) << 8);	/* read isr */
	pic &= ~irq_mask;			/* apply mask */
	pic &= 0xFFFB;				/* mask out cascade & hibits */

	while (pic) {
		j = ffz(~pic);
		pic &= pic - 1;
		device_interrupt(j, j, regs);
	}
#endif
}

#if defined(CONFIG_ALPHA_ALCOR) || defined(CONFIG_ALPHA_XLT)
/* we have to conditionally compile this because of GRU_xxx symbols */
static inline void alcor_and_xlt_device_interrupt(unsigned long vector,
                                                  struct pt_regs * regs)
{
        unsigned long pld;
        unsigned int i;
        unsigned long flags;

        save_flags(flags);
        cli();

        /* read the interrupt summary register of the GRU */
        pld = (*(unsigned int *)GRU_INT_REQ) & GRU_INT_REQ_BITS;

#if 0
        printk("[0x%08lx/0x%04x]", pld, inb(0x20) | (inb(0xA0) << 8));
#endif

        /*
         * Now for every possible bit set, work through them and call
         * the appropriate interrupt handler.
         */
        while (pld) {
                i = ffz(~pld);
                pld &= pld - 1; /* clear least bit set */
                if (i == 31) {
                        isa_device_interrupt(vector, regs);
                } else {
                        device_interrupt(16 + i, 16 + i, regs);
                }
        }
        restore_flags(flags);
}
#endif /* ALCOR || XLT */

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

#if 0
	printk("[0x%04X/0x%04X]", pld, inb(0x20) | (inb(0xA0) << 8));
#endif

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

static inline void mikasa_device_interrupt(unsigned long vector,
					   struct pt_regs * regs)
{
	unsigned long pld;
	unsigned int i;
	unsigned long flags;

	save_flags(flags);
	cli();

        /* read the interrupt summary registers */
        pld = (((unsigned long) (~inw(0x534)) & 0x0000ffffUL) << 16) |
	       (((unsigned long) inb(0xa0))  <<  8) |
	       ((unsigned long) inb(0x20));

#if 0
        printk("[0x%08lx]", pld);
#endif

        /*
         * Now for every possible bit set, work through them and call
         * the appropriate interrupt handler.
         */
        while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i < 16) {
			isa_device_interrupt(vector, regs);
		} else {
			device_interrupt(i, i, regs);
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

#endif /* CONFIG_PCI */

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
	unsigned long flags;

	save_flags(flags);
	cli();


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

	restore_flags(flags) ;
}

/*
 * Start listening for interrupts..
 */
unsigned long probe_irq_on(void)
{
	struct irqaction * action;
	unsigned long irqs = 0;
	unsigned long delay;
	unsigned int i;

	for (i = NR_IRQS - 1; i > 0; i--) {
		if (!(PROBE_MASK & (1UL << i))) {
			continue;
		}
		action = irq_action[i];
		if (!action) {
			enable_irq(i);
			irqs |= (1UL << i);
		}
	}
	/*
	 * Wait about 100ms for spurious interrupts to mask themselves
	 * out again...
	 */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		barrier();

	/* now filter out any obviously spurious interrupts */
	return irqs & ~irq_mask;
}

/*
 * Get the result of the IRQ probe.. A negative result means that
 * we have several candidates (but we return the lowest-numbered
 * one).
 */
int probe_irq_off(unsigned long irqs)
{
	unsigned long delay;
	int i;
	
        irqs &= irq_mask;
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
#elif defined(CONFIG_ALPHA_CIA)
	extern void cia_machine_check(unsigned long vector, unsigned long la,
					struct pt_regs * regs);
	cia_machine_check(vector, la, regs);
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
		        handle_irq(RTC_IRQ, &regs);
			return;
		case 2:
			machine_check(vector, la_ptr, &regs);
			return;
		case 3:
#if defined(CONFIG_ALPHA_JENSEN) || defined(CONFIG_ALPHA_NONAME) || \
    defined(CONFIG_ALPHA_P2K) || defined(CONFIG_ALPHA_SRM)
			srm_device_interrupt(vector, &regs);
#elif NR_IRQS == 48
			alcor_and_xlt_device_interrupt(vector, &regs);
#elif NR_IRQS == 33
			cabriolet_and_eb66p_device_interrupt(vector, &regs);
#elif defined(CONFIG_ALPHA_MIKASA)
			mikasa_device_interrupt(vector, &regs);
#elif NR_IRQS == 32
			eb66_and_eb64p_device_interrupt(vector, &regs);
#elif NR_IRQS == 16
			isa_device_interrupt(vector, &regs);
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
#if NR_IRQS == 48
	*(unsigned int *)GRU_INT_MASK  = ~(irq_mask >> 16); mb();/* invert */
	*(unsigned int *)GRU_INT_EDGE  = 0UL; mb();/* all are level */
	*(unsigned int *)GRU_INT_HILO  = 0x80000000UL; mb();/* ISA only HI */
	*(unsigned int *)GRU_INT_CLEAR = 0UL; mb();/* all clear */
	enable_irq(16 + 31);	/* enable (E)ISA PIC cascade */
#elif NR_IRQS == 33
	outl(irq_mask >> 16, 0x804);
	enable_irq(16 +  4);	/* enable SIO cascade */
#elif defined(CONFIG_ALPHA_MIKASA)
	outw(~(irq_mask >> 16), 0x536); /* note invert */
#elif NR_IRQS == 32
	outb(irq_mask >> 16, 0x26);
	outb(irq_mask >> 24, 0x27);
	enable_irq(16 +  5);	/* enable SIO cascade */
#endif
	enable_irq(2);		/* enable cascade */
}
