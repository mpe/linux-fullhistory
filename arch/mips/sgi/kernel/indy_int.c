/* $Id: indy_int.c,v 1.1 1997/06/06 09:36:21 ralf Exp $
 * indy_int.c: Routines for generic manipulation of the INT[23] ASIC
 *             found on INDY workstations..
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/config.h>

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
#include <asm/vector.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/sgi.h>
#include <asm/sgihpc.h>
#include <asm/sgint23.h>
#include <asm/sgialib.h>

/* #define DEBUG_SGINT */

struct sgi_int2_regs *sgi_i2regs;
struct sgi_int3_regs *sgi_i3regs;
struct sgi_ioc_ints *ioc_icontrol;
struct sgi_ioc_timers *ioc_timers;
volatile unsigned char *ioc_tclear;

static char lc0msk_to_irqnr[256];
static char lc1msk_to_irqnr[256];
static char lc2msk_to_irqnr[256];
static char lc3msk_to_irqnr[256];

extern asmlinkage void indyIRQ(void);

#ifdef CONFIG_REMOTE_DEBUG
extern void rs_kgdb_hook(int);
#endif

unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;

/* Local IRQ's are layed out logically like this:
 *
 * 0  --> 7   ==   local 0 interrupts
 * 8  --> 15  ==   local 1 interrupts
 * 16 --> 23  ==   vectored level 2 interrupts
 * 24 --> 31  ==   vectored level 3 interrupts (not used)
 */
void disable_local_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	switch(irq_nr) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
		ioc_icontrol->imask0 &= ~(1 << irq_nr);
		break;

	case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		ioc_icontrol->imask1 &= ~(1 << (irq_nr - 8));
		break;

	case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
		ioc_icontrol->cmeimask0 &= ~(1 << (irq_nr - 16));
		break;

	default:
		/* This way we'll see if anyone would ever want vectored
		 * level 3 interrupts.  Highly unlikely.
		 */
		printk("Yeeee, got passed irq_nr %d at disable_irq\n", irq_nr);
		panic("INVALID IRQ level!");
	};
	restore_flags(flags);
}

void enable_local_irq(unsigned int irq_nr)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	switch(irq_nr) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
		ioc_icontrol->imask0 |= (1 << irq_nr);
		break;

	case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		ioc_icontrol->imask1 |= (1 << (irq_nr - 8));
		break;

	case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
		enable_local_irq(7);
		ioc_icontrol->cmeimask0 |= (1 << (irq_nr - 16));
		break;

	default:
		printk("Yeeee, got passed irq_nr %d at disable_irq\n", irq_nr);
		panic("INVALID IRQ level!");
	};
	restore_flags(flags);
}

void disable_gio_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void enable_gio_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void disable_hpcdma_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void enable_hpcdma_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void disable_irq(unsigned int irq_nr)
{
	unsigned int n = irq_nr;
	if(n >= SGINT_END) {
		printk("whee, invalid irq_nr %d\n", irq_nr);
		panic("IRQ, you lose...");
	}
	if(n >= SGINT_LOCAL0 && n < SGINT_GIO) {
		disable_local_irq(n - SGINT_LOCAL0);
	} else if(n >= SGINT_GIO && n < SGINT_HPCDMA) {
		disable_gio_irq(n - SGINT_GIO);
	} else if(n >= SGINT_HPCDMA && n < SGINT_END) {
		disable_hpcdma_irq(n - SGINT_HPCDMA);
	} else {
		panic("how did I get here?");
	}
}

void enable_irq(unsigned int irq_nr)
{
	unsigned int n = irq_nr;
	if(n >= SGINT_END) {
		printk("whee, invalid irq_nr %d\n", irq_nr);
		panic("IRQ, you lose...");
	}
	if(n >= SGINT_LOCAL0 && n < SGINT_GIO) {
		enable_local_irq(n - SGINT_LOCAL0);
	} else if(n >= SGINT_GIO && n < SGINT_HPCDMA) {
		enable_gio_irq(n - SGINT_GIO);
	} else if(n >= SGINT_HPCDMA && n < SGINT_END) {
		enable_hpcdma_irq(n - SGINT_HPCDMA);
	} else {
		panic("how did I get here?");
	}
}

#if 0
/*
 * Currently unused.
 */
static void local_unex(int irq, void *data, struct pt_regs *regs)
{
	printk("Whee: unexpected local IRQ at %08lx\n",
	       (unsigned long) regs->cp0_epc);
	printk("DUMP: stat0<%x> stat1<%x> vmeistat<%x>\n",
	       ioc_icontrol->istat0, ioc_icontrol->istat1,
	       ioc_icontrol->vmeistat);
}
#endif

static struct irqaction *local_irq_action[24] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

int setup_indy_irq(int irq, struct irqaction * new)
{
	printk("setup_indy_irq: Yeee, don't know how to setup irq<%d> for %s  %p\n",
	       irq, new->name, new->handler);
	return 0;
}

static struct irqaction r4ktimer_action = {
	NULL, 0, 0, "R4000 timer/counter", NULL, NULL,
};

static struct irqaction indy_berr_action = {
	NULL, 0, 0, "IP22 Bus Error", NULL, NULL,
};

static struct irqaction *irq_action[16] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, &indy_berr_action, &r4ktimer_action,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	int num = 0;
	struct irqaction * action;

	for (i = 0 ; i < 16 ; i++, num++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			num, kstat.interrupts[num],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, " [on-chip]\n");
	}
	for (i = 0 ; i < 24 ; i++, num++) {
		action = local_irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			num, kstat.interrupts[num],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, " [local]\n");
	}
	return len;
}

atomic_t __mips_bh_counter;

#ifdef __SMP__
#error Send superfluous SMP boxes to ralf@uni-koblenz.de
#else
#define irq_enter(cpu, irq)     (++local_irq_count[cpu])
#define irq_exit(cpu, irq)      (--local_irq_count[cpu])
#endif

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction * action = *(irq + irq_action);

	lock_kernel();
	kstat.interrupts[irq]++;
	printk("Got irq %d, press a key.", irq);
	prom_getchar();
	romvec->imode();
        while (action) {
		if (action->flags & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		action->handler(irq, action->dev_id, regs);
		action = action->next;
        }
	unlock_kernel();
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
	struct irqaction * action = *(irq + irq_action);

	lock_kernel();
	printk("Got irq %d, press a key.", irq);
	prom_getchar();
	romvec->imode();
	kstat.interrupts[irq]++;
        while (action) {
		if (action->flags & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		action->handler(irq, action->dev_id, NULL);
		action = action->next;
        }
	unlock_kernel();
}

int request_local_irq(unsigned int lirq, void (*func)(int, void *, struct pt_regs *),
		      unsigned long iflags, const char *dname, void *devid)
{
	struct irqaction *action;

	lirq -= SGINT_LOCAL0;
	if(lirq >= 24 || !func)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if(!action)
		return -ENOMEM;

	action->handler = func;
	action->flags = iflags;
	action->mask = 0;
	action->name = dname;
	action->dev_id = devid;
	action->next = 0;
	local_irq_action[lirq] = action;
	enable_irq(lirq + SGINT_LOCAL0);
	return 0;
}

void free_local_irq(unsigned int lirq, void *dev_id)
{
	struct irqaction *action;

	lirq -= SGINT_LOCAL0;
	if(lirq >= 24) {
		printk("Aieee: trying to free bogus local irq %d\n",
		       lirq + SGINT_LOCAL0);
		return;
	}
	action = local_irq_action[lirq];
	local_irq_action[lirq] = NULL;
	disable_irq(lirq + SGINT_LOCAL0);
	kfree(action);
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= SGINT_END)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	if((irq >= SGINT_LOCAL0) && (irq < SGINT_GIO))
		return request_local_irq(irq, handler, irqflags, devname, dev_id);

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_indy_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= SGINT_END) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if((irq >= SGINT_LOCAL0) && (irq < SGINT_GIO)) {
		free_local_irq(irq, dev_id);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_flags(flags);
		cli();
		*p = action->next;
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

void init_IRQ(void)
{
	int i;

	for (i = 0; i < 16 ; i++)
		set_int_vector(i, 0);
	irq_setup();
}

void indy_local0_irqdispatch(struct pt_regs *regs)
{
	struct irqaction *action;
	unsigned char mask = ioc_icontrol->istat0;
	unsigned char mask2 = 0;
	int irq;

	mask &= ioc_icontrol->imask0;
	if(mask & ISTAT0_LIO2) {
		mask2 = ioc_icontrol->vmeistat;
		mask2 &= ioc_icontrol->cmeimask0;
		irq = lc2msk_to_irqnr[mask2];
		action = local_irq_action[irq];
	} else {
		irq = lc0msk_to_irqnr[mask];
		action = local_irq_action[irq];
	}
#if 0
	printk("local0_dispatch: got irq %d mask %2x mask2 %2x\n",
	       irq, mask, mask2);
	prom_getchar();
#endif
	kstat.interrupts[irq + 16]++;
	action->handler(irq, action->dev_id, regs);
}

void indy_local1_irqdispatch(struct pt_regs *regs)
{
	struct irqaction *action;
	unsigned char mask = ioc_icontrol->istat1;
	unsigned char mask2 = 0;
	int irq;

	mask &= ioc_icontrol->imask1;
	if(mask & ISTAT1_LIO3) {
		printk("WHee: Got an LIO3 irq, winging it...\n");
		mask2 = ioc_icontrol->vmeistat;
		mask2 &= ioc_icontrol->cmeimask1;
		irq = lc3msk_to_irqnr[ioc_icontrol->vmeistat];
		action = local_irq_action[irq];
	} else {
		irq = lc1msk_to_irqnr[mask];
		action = local_irq_action[irq];
	}
#if 0
	printk("local1_dispatch: got irq %d mask %2x mask2 %2x\n",
	       irq, mask, mask2);
	prom_getchar();
#endif
	kstat.interrupts[irq + 24]++;
	action->handler(irq, action->dev_id, regs);
}

void indy_buserror_irq(struct pt_regs *regs)
{
	kstat.interrupts[6]++;
	printk("Got a bus error IRQ, shouldn't happen yet\n");
	show_regs(regs);
	printk("Spinning...\n");
	while(1)
		;
}

/* Misc. crap just to keep the kernel linking... */
unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

void sgint_init(void)
{
	int i;
#ifdef CONFIG_REMOTE_DEBUG
	char *ctype;
#endif

	sgi_i2regs = (struct sgi_int2_regs *) (KSEG1 + SGI_INT2_BASE);
	sgi_i3regs = (struct sgi_int3_regs *) (KSEG1 + SGI_INT3_BASE);

	/* Init local mask --> irq tables. */
	for(i = 0; i < 256; i++) {
		if(i & 0x80) {
			lc0msk_to_irqnr[i] = 7;
			lc1msk_to_irqnr[i] = 15;
			lc2msk_to_irqnr[i] = 23;
			lc3msk_to_irqnr[i] = 31;
		} else if(i & 0x40) {
			lc0msk_to_irqnr[i] = 6;
			lc1msk_to_irqnr[i] = 14;
			lc2msk_to_irqnr[i] = 22;
			lc3msk_to_irqnr[i] = 30;
		} else if(i & 0x20) {
			lc0msk_to_irqnr[i] = 5;
			lc1msk_to_irqnr[i] = 13;
			lc2msk_to_irqnr[i] = 21;
			lc3msk_to_irqnr[i] = 29;
		} else if(i & 0x10) {
			lc0msk_to_irqnr[i] = 4;
			lc1msk_to_irqnr[i] = 12;
			lc2msk_to_irqnr[i] = 20;
			lc3msk_to_irqnr[i] = 28;
		} else if(i & 0x08) {
			lc0msk_to_irqnr[i] = 3;
			lc1msk_to_irqnr[i] = 11;
			lc2msk_to_irqnr[i] = 19;
			lc3msk_to_irqnr[i] = 27;
		} else if(i & 0x04) {
			lc0msk_to_irqnr[i] = 2;
			lc1msk_to_irqnr[i] = 10;
			lc2msk_to_irqnr[i] = 18;
			lc3msk_to_irqnr[i] = 26;
		} else if(i & 0x02) {
			lc0msk_to_irqnr[i] = 1;
			lc1msk_to_irqnr[i] = 9;
			lc2msk_to_irqnr[i] = 17;
			lc3msk_to_irqnr[i] = 25;
		} else if(i & 0x01) {
			lc0msk_to_irqnr[i] = 0;
			lc1msk_to_irqnr[i] = 8;
			lc2msk_to_irqnr[i] = 16;
			lc3msk_to_irqnr[i] = 24;
		} else {
			lc0msk_to_irqnr[i] = 0;
			lc1msk_to_irqnr[i] = 0;
			lc2msk_to_irqnr[i] = 0;
			lc3msk_to_irqnr[i] = 0;
		}
	}

	ioc_icontrol = &sgi_i3regs->ints;
	ioc_timers = &sgi_i3regs->timers;
	ioc_tclear = &sgi_i3regs->tclear;

	/* Mask out all interrupts. */
	ioc_icontrol->imask0 = 0;
	ioc_icontrol->imask1 = 0;
	ioc_icontrol->cmeimask0 = 0;
	ioc_icontrol->cmeimask1 = 0;

	/* Now safe to set the exception vector. */
	set_except_vector(0, indyIRQ);

#ifdef CONFIG_REMOTE_DEBUG
	ctype = prom_getcmdline();
	for(i = 0; i < strlen(ctype); i++) {
		if(ctype[i]=='k' && ctype[i+1]=='g' &&
		   ctype[i+2]=='d' && ctype[i+3]=='b' &&
		   ctype[i+4]=='=' && ctype[i+5]=='t' &&
		   ctype[i+6]=='t' && ctype[i+7]=='y' &&
		   ctype[i+8]=='d' &&
		   (ctype[i+9] == '1' || ctype[i+9] == '2')) {
			printk("KGDB: Using serial line /dev/ttyd%d for "
			       "session\n", (ctype[i+9] - '0'));
				if(ctype[i+9]=='1')
					rs_kgdb_hook(1);
				else if(ctype[i+9]=='2')
					rs_kgdb_hook(0);
				else {
					printk("KGDB: whoops bogon tty line "
					       "requested, disabling session\n");
				}

		}
	}
#endif
}
