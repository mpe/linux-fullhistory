/* $Id: ip27-irq.c,v 1.6 2000/02/10 05:58:56 dagum Exp $
 *
 * ip27-irq.c: Highlevel interrupt handling for IP27 architecture.
 *
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>

#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/pci/bridge.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/arch.h>

extern asmlinkage void ip27_irq(void);
int (*irq_cannonicalize)(int irq);

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;

void disable_irq(unsigned int irq_nr)
{
	panic("disable_irq() called ...");
}

void enable_irq(unsigned int irq_nr)
{
	panic("enable_irq() called ...");
}

/* This is stupid for an Origin which can have thousands of IRQs ...  */
static struct irqaction *irq_action[NR_IRQS];

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < 32 ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s", i, kstat.irqs[0][i],
		               (action->flags & SA_INTERRUPT) ? '+' : ' ',
		               action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
			               (action->flags & SA_INTERRUPT)
			                ? " +" : "",
			                action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

/*
 * do_IRQ handles all normal device IRQ's (the special SMP cross-CPU interrupts
 * have their own specific handlers).
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction *action;
	int do_random, cpu;

	cpu = smp_processor_id();
	irq_enter(cpu);
	kstat.irqs[cpu][irq]++;

	action = *(irq + irq_action);
	if (action) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		action = *(irq + irq_action);
		do_random = 0;
        	do {
			do_random |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
        	} while (action);
		if (do_random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
	}
	irq_exit(cpu);

	/* unmasking and bottom half handling is done magically for us. */
}

/*
 * Find first bit set
 */
static int ms1bit(unsigned long x)
{
	int	b;

	if (x >> 32) 	b = 32, x >>= 32;
	else		b  =  0;
	if (x >> 16)	b += 16, x >>= 16;
	if (x >>  8)	b +=  8, x >>=  8;
	if (x >>  4)	b +=  4, x >>=  4;
	if (x >>  2)	b +=  2, x >>=  2;

	return b + (int) (x >> 1);
}
	
/* For now ...  */
void ip27_do_irq(struct pt_regs *regs)
{
	int irq;
	hubreg_t pend0, mask0;

	/* copied from Irix intpend0() */
	while (((pend0 = LOCAL_HUB_L(PI_INT_PEND0)) & 
				(mask0 = LOCAL_HUB_L(PI_INT_MASK0_A))) != 0) {
		do {
			irq = ms1bit(pend0);
			LOCAL_HUB_S(PI_INT_MASK0_A, mask0 & ~(1 << irq));
			LOCAL_HUB_S(PI_INT_PEND_MOD, irq);
			LOCAL_HUB_L(PI_INT_MASK0_A);		/* Flush */
			do_IRQ(irq, regs);
			LOCAL_HUB_S(PI_INT_MASK0_A, mask0);
			pend0 ^= 1ULL << irq;
		} while (pend0);
	}
}


/* Startup one of the (PCI ...) IRQs routes over a bridge.  */
static unsigned int bridge_startup(unsigned int irq)
{
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;
	bridgereg_t br;
	int pin;

	/* FIIIIIXME ...  Temporary kludge.  This knows how interrupts are
	   setup in _my_ Origin.  */
	switch (irq) {
	case IOC3_SERIAL_INT:	pin = 3; break;
	case IOC3_ETH_INT:	pin = 2; break;
	case SCSI1_INT:		pin = 1; break;
	case SCSI0_INT:		pin = 0; break;
	default:		panic("bridge_startup: whoops?");
	}

	br = LOCAL_HUB_L(PI_INT_MASK0_A);
	LOCAL_HUB_S(PI_INT_MASK0_A, br | (1 << irq));
	LOCAL_HUB_L(PI_INT_MASK0_A);			/* Flush */

	bridge->b_int_addr[pin].addr = 0x20000 | irq;
	bridge->b_int_enable |= (1 << pin);
	if (irq < 2) {
		bridgereg_t device;
#if 0
		/*
	 	 * Allocate enough RRBs on the bridge for the DMAs.
	 	 * Right now allocating 2 RRBs on the normal channel
	 	 * and 2 on the virtual channel for slot 0 on the bus.
		 * And same for slot 1, to get ioc3 eth working.
	 	 */
		Not touching b_even_resp	  /* boot doesn't go far */
		bridge->b_even_resp = 0xdd99cc88; /* boot doesn't go far */
		bridge->b_even_resp = 0xcccc8888; /* breaks eth0 */
		bridge->b_even_resp = 0xcc88;	  /* breaks eth0 */
#endif
		/* Turn on bridge swapping */
		device = bridge->b_device[irq].reg;
		device |= BRIDGE_DEV_SWAP_DIR;
		bridge->b_device[irq].reg = device;
	}
	bridge->b_widget.w_tflush;			/* Flush */

	return 0;	/* Never anything pending.  */
}

/* Startup one of the (PCI ...) IRQs routes over a bridge.  */
static unsigned int bridge_shutdown(unsigned int irq)
{
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;
	bridgereg_t br;
	int pin;

	/* FIIIIIXME ...  Temporary kludge.  This knows how interrupts are
	   setup in _my_ Origin.  */
	switch (irq) {
	case IOC3_SERIAL_INT:	pin = 3; break;
	case IOC3_ETH_INT:	pin = 2; break;
	case SCSI1_INT:		pin = 1; break;
	case SCSI0_INT:		pin = 0; break;
	default:		panic("bridge_startup: whoops?");
	}

	br = LOCAL_HUB_L(PI_INT_MASK0_A);
	LOCAL_HUB_S(PI_INT_MASK0_A, br & ~(1 << irq));
	LOCAL_HUB_L(PI_INT_MASK0_A);			/* Flush */

	bridge->b_int_enable &= ~(1 << pin);
	bridge->b_widget.w_tflush;			/* Flush */

	return 0;	/* Never anything pending.  */
}

static void bridge_init(void)
{
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;

	/* Hmm...  IRIX sets additional bits in the address which are
	   documented as reserved in the bridge docs ...  */
	bridge->b_int_mode = 0x0;			/* Don't clear ints */
#if 0
	bridge->b_wid_int_upper = 0x000a8000;           /* Ints to node 0 */
	bridge->b_wid_int_lower = 0x01000090;
	bridge->b_dir_map = 0xa00000;			/* DMA */
#endif /* shouldn't lower= 0x01800090 ??? */
        bridge->b_wid_int_upper = 0x00098000;           /* Ints to node 0 */
        bridge->b_wid_int_lower = 0x01800090;
        bridge->b_dir_map = 0x900000;                   /* DMA */

	bridge->b_int_enable = 0;
	bridge->b_widget.w_tflush;			/* Flush */
	set_cp0_status(SRB_DEV0 | SRB_DEV1, SRB_DEV0 | SRB_DEV1);
}

void irq_debug(void)
{
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;

	printk("bridge->b_int_status = 0x%x\n", bridge->b_int_status);
	printk("bridge->b_int_enable = 0x%x\n", bridge->b_int_enable);
	printk("PI_INT_PEND0   = 0x%x\n", LOCAL_HUB_L(PI_INT_PEND0));
	printk("PI_INT_MASK0_A = 0x%x\n", LOCAL_HUB_L(PI_INT_MASK0_A));
}

int setup_irq(int irq, struct irqaction *new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	save_and_cli(flags);
	p = irq_action + irq;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			restore_flags(flags);
			return -EBUSY;
		}

		/* Add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		bridge_startup(irq);
	}
	restore_flags(flags);

	return 0;
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval;
	struct irqaction *action;

	if (irq > 9)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(*action), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq > 9) {
		printk("Trying to free IRQ%d\n", irq);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		if (!irq[irq_action])
			bridge_shutdown(irq);
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

/* Useless ISA nonsense.  */
unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

static int indy_irq_cannonicalize(int irq)
{
	return irq;	/* Sane hardware, sane code ... */
}

void __init init_IRQ(void)
{
	irq_cannonicalize = indy_irq_cannonicalize;

	bridge_init();
	set_except_vector(0, ip27_irq);
}
