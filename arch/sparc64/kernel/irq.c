/* $Id: irq.c,v 1.13 1997/05/27 07:54:28 davem Exp $
 * irq.c: UltraSparc IRQ handling/init/registry.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/random.h> /* XXX ADD add_foo_randomness() calls... -DaveM */
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/sbus.h>
#include <asm/iommu.h>
#include <asm/upa.h>
#include <asm/oplib.h>
#include <asm/smp.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

/* Internal flag, should not be visible elsewhere at all. */
#define SA_SYSIO_MASKED		0x100

/* UPA nodes send interrupt packet to UltraSparc with first data reg value
 * low 5 bits holding the IRQ identifier being delivered.  We must translate
 * this into a non-vector IRQ so we can set the softint on this cpu.  To
 * make things even more swift we store the complete mask here.
 */

#define NUM_IVECS	2048	/* XXX may need more on sunfire/wildfire */

unsigned long ivector_to_mask[NUM_IVECS];

/* This is based upon code in the 32-bit Sparc kernel written mostly by
 * David Redman (djhr@tadpole.co.uk).
 */
#define MAX_STATIC_ALLOC	4
static struct irqaction static_irqaction[MAX_STATIC_ALLOC];
static int static_irq_count = 0;

static struct irqaction *irq_action[NR_IRQS+1] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction *action;

	for(i = 0; i < (NR_IRQS + 1); i++) {
		if(!(action = *(i + irq_action)))
			continue;
		len += sprintf(buf + len, "%2d: %8d %c %s",
			       i, kstat.interrupts[i],
			       (action->flags & SA_INTERRUPT) ? '+' : ' ',
			       action->name);
		for(action = action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				       (action->flags & SA_INTERRUPT) ? " +" : "",
				       action->name);
		}
		len += sprintf(buf + len, "\n");
	}
	return len;
}

/* INO number to Sparc PIL level. */
static unsigned char ino_to_pil[] = {
	0, 1, 2, 3, 5, 7, 8, 9,		/* SBUS slot 0 */
	0, 1, 2, 3, 5, 7, 8, 9,		/* SBUS slot 1 */
	0, 1, 2, 3, 5, 7, 8, 9,		/* SBUS slot 2 */
	0, 1, 2, 3, 5, 7, 8, 9,		/* SBUS slot 3 */
	3, /* Onboard SCSI */
	5, /* Onboard Ethernet */
/*XXX*/	8, /* Onboard BPP */
	0, /* Bogon */
       13, /* Audio */
/*XXX*/15, /* PowerFail */
	0, /* Bogon */
	0, /* Bogon */
       12, /* Zilog Serial Channels (incl. Keyboard/Mouse lines) */
       11, /* Floppy */
	0, /* Spare Hardware (bogon for now) */
	0, /* Keyboard (bogon for now) */
	0, /* Mouse (bogon for now) */
	0, /* Serial (bogon for now) */
     0, 0, /* Bogon, Bogon */
       10, /* Timer 0 */
       11, /* Timer 1 */
     0, 0, /* Bogon, Bogon */
       15, /* Uncorrectable SBUS Error */
       15, /* Correctable SBUS Error */
       15, /* SBUS Error */
/*XXX*/ 0, /* Power Management (bogon for now) */
};

/* INO number to IMAP register offset for SYSIO external IRQ's.
 * This should conform to both Sunfire/Wildfire server and Fusion
 * desktop designs.
 */
#define offset(x) ((unsigned long)(&(((struct sysio_regs *)0)->x)))
#define bogon     ((unsigned long) -1)
static unsigned long irq_offsets[] = {
/* SBUS Slot 0 --> 3, level 1 --> 7 */
offset(imap_slot0),offset(imap_slot0),offset(imap_slot0),offset(imap_slot0),
offset(imap_slot0),offset(imap_slot0),offset(imap_slot0),offset(imap_slot0),
offset(imap_slot1),offset(imap_slot1),offset(imap_slot1),offset(imap_slot1),
offset(imap_slot1),offset(imap_slot1),offset(imap_slot1),offset(imap_slot1),
offset(imap_slot2),offset(imap_slot2),offset(imap_slot2),offset(imap_slot2),
offset(imap_slot2),offset(imap_slot2),offset(imap_slot2),offset(imap_slot2),
offset(imap_slot3),offset(imap_slot3),offset(imap_slot3),offset(imap_slot3),
offset(imap_slot3),offset(imap_slot3),offset(imap_slot3),offset(imap_slot3),
/* Onboard devices (not relevant/used on SunFire). */
offset(imap_scsi), offset(imap_eth), offset(imap_bpp), bogon,
offset(imap_audio), offset(imap_pfail), bogon, bogon,
offset(imap_kms), offset(imap_flpy), offset(imap_shw),
offset(imap_kbd), offset(imap_ms), offset(imap_ser), bogon, bogon,
offset(imap_tim0), offset(imap_tim1), bogon, bogon,
offset(imap_ue), offset(imap_ce), offset(imap_sberr),
offset(imap_pmgmt),
};

#undef bogon

#define NUM_IRQ_ENTRIES (sizeof(irq_offsets) / sizeof(irq_offsets[0]))

/* Convert an "interrupts" property IRQ level to an SBUS/SYSIO
 * Interrupt Mapping register pointer, or NULL if none exists.
 */
static unsigned int *irq_to_imap(unsigned int irq)
{
	unsigned long offset;
	struct sysio_regs *sregs;

	if((irq == 14) ||
	   (irq >= NUM_IRQ_ENTRIES) ||
	   ((offset = irq_offsets[irq]) == ((unsigned long)-1)))
		return NULL;
	sregs = SBus_chain->iommu->sysio_regs;
	offset += ((unsigned long) sregs);
	return ((unsigned int *)offset) + 1;
}

/* Convert Interrupt Mapping register pointer to assosciated
 * Interrupt Clear register pointer.
 */
static unsigned int *imap_to_iclr(unsigned int *imap)
{
	unsigned long diff;

	diff = offset(iclr_unused0) - offset(imap_slot0);
	return (unsigned int *) (((unsigned long)imap) + diff);
}

#undef offset

/* For non-SBUS IRQ's we do nothing, else we must enable them in the
 * appropriate SYSIO interrupt map registers.
 */
void enable_irq(unsigned int irq)
{
	unsigned long tid;
	unsigned int *imap;

	/* If this is for the tick interrupt, just ignore, note
	 * that this is the one and only locally generated interrupt
	 * source, all others come from external sources (essentially
	 * any UPA device which is an interruptor).  (actually, on
	 * second thought Ultra can generate local interrupts for
	 * async memory errors and we may setup handlers for those
	 * at some point as well)
	 *
	 * XXX See commentary below in request_irq() this assumption
	 * XXX is broken and needs to be fixed.
	 */
	if(irq == 14)
		return;

	/* Check for bogons. */
	imap = irq_to_imap(irq);
	if(imap == NULL)
		goto do_the_stb_watoosi;

	/* We send it to our UPA MID, for SMP this will be different. */
	__asm__ __volatile__("ldxa [%%g0] %1, %0" : "=r" (tid) : "i" (ASI_UPA_CONFIG));
	tid = ((tid & UPA_CONFIG_MID) << 9);

	/* NOTE NOTE NOTE, IGN and INO are read-only, IGN is a product
	 * of this SYSIO's preconfigured IGN in the SYSIO Control
	 * Register, the hardware just mirrors that value here.
	 * However for Graphics and UPA Slave devices the full
	 * SYSIO_IMAP_INR field can be set by the programmer here.
	 * (XXX we will have to handle those for FFB etc. XXX)
	 */
	*imap = SYSIO_IMAP_VALID | (tid & SYSIO_IMAP_TID);
	return;

do_the_stb_watoosi:
	printk("Cannot enable irq(%d), doing the \"STB Watoosi\" instead.", irq);
	panic("Trying to enable bogon IRQ");
}

void disable_irq(unsigned int irq)
{
	unsigned int *imap;

	/* XXX Grrr, I know this is broken... */
	if(irq == 14)
		return;

	/* Check for bogons. */
	imap = irq_to_imap(irq);
	if(imap == NULL)
		goto do_the_stb_watoosi;

	/* NOTE: We do not want to futz with the IRQ clear registers
	 *       and move the state to IDLE, the SCSI code does call
	 *       disable_irq() to assure atomicity in the queue cmd
	 *       SCSI adapter driver code.  Thus we'd lose interrupts.
	 */
	*imap &= ~(SYSIO_IMAP_VALID);
	return;

do_the_stb_watoosi:
	printk("Cannot disable irq(%d), doing the \"STB Watoosi\" instead.", irq);
	panic("Trying to enable bogon IRQ");
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char *name, void *dev_cookie)
{
	struct irqaction *action, *tmp = NULL;
	unsigned long flags;
	unsigned int cpu_irq, *imap, *iclr;
	
	/* XXX This really is not the way to do it, the "right way"
	 * XXX is to have drivers set SA_SBUS or something like that
	 * XXX in irqflags and we base our decision here on whether
	 * XXX that flag bit is set or not.
	 */
	if(irq == 14)
		cpu_irq = irq;
	else
		cpu_irq = ino_to_pil[irq];

	if(!handler)
	    return -EINVAL;

	imap = irq_to_imap(irq);

	action = *(cpu_irq + irq_action);
	if(action) {
		if((action->flags & SA_SHIRQ) && (irqflags & SA_SHIRQ))
			for (tmp = action; tmp->next; tmp = tmp->next)
				;
		else
			return -EBUSY;

		if((action->flags & SA_INTERRUPT) ^ (irqflags & SA_INTERRUPT)) {
			printk("Attempt to mix fast and slow interrupts on IRQ%d "
			       "denied\n", irq);
			return -EBUSY;
		}   
		action = NULL;		/* Or else! */
	}

	save_and_cli(flags);

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if(irqflags & SA_STATIC_ALLOC)
	    if(static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed "
		       "using kmalloc\n", irq, name);
	
	if(action == NULL)
	    action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						 GFP_KERNEL);
	
	if(!action) { 
		restore_flags(flags);
		return -ENOMEM;
	}

	if(imap) {
		int ivindex = (*imap & (SYSIO_IMAP_IGN | SYSIO_IMAP_INO));

		ivector_to_mask[ivindex] = (1<<cpu_irq);
		iclr = imap_to_iclr(imap);
		action->mask = (unsigned long) iclr;
		irqflags |= SA_SYSIO_MASKED;
	} else {
		action->mask = 0;
	}

	action->handler = handler;
	action->flags = irqflags;
	action->name = name;
	action->next = NULL;
	action->dev_id = dev_cookie;

	if(tmp)
		tmp->next = action;
	else
		*(cpu_irq + irq_action) = action;

	enable_irq(irq);
	restore_flags(flags);
	return 0;
}

void free_irq(unsigned int irq, void *dev_cookie)
{
	struct irqaction *action;
	struct irqaction *tmp = NULL;
	unsigned long flags;
	unsigned int cpu_irq;

	if(irq == 14)
		cpu_irq = irq;
	else
		cpu_irq = ino_to_pil[irq];
	action = *(cpu_irq + irq_action);
	if(!action->handler) {
		printk("Freeing free IRQ %d\n", irq);
		return;
	}
	if(dev_cookie) {
		for( ; action; action = action->next) {
			if(action->dev_id == dev_cookie)
				break;
			tmp = action;
		}
		if(!action) {
			printk("Trying to free free shared IRQ %d\n", irq);
			return;
		}
	} else if(action->flags & SA_SHIRQ) {
		printk("Trying to free shared IRQ %d with NULL device cookie\n", irq);
		return;
	}

	if(action->flags & SA_STATIC_ALLOC) {
		printk("Attempt to free statically allocated IRQ %d (%s)\n",
		       irq, action->name);
		return;
	}

	save_and_cli(flags);
	if(action && tmp)
		tmp->next = action->next;
	else
		*(cpu_irq + irq_action) = action->next;

	if(action->flags & SA_SYSIO_MASKED) {
		unsigned int *imap = irq_to_imap(irq);
		if(imap != NULL)
			ivector_to_mask[*imap & (SYSIO_IMAP_IGN | SYSIO_IMAP_INO)] = 0;
		else
			printk("free_irq: WHeee, SYSIO_MASKED yet no imap reg.\n");
	}

	kfree(action);
	if(!*(cpu_irq + irq_action))
		disable_irq(irq);

	restore_flags(flags);
}

/* Per-processor IRQ locking depth, both SMP and non-SMP code use this. */
unsigned int local_irq_count[NR_CPUS];

#ifndef __SMP__
int __sparc64_bh_counter = 0;

#define irq_enter(cpu, irq)	(local_irq_count[cpu]++)
#define irq_exit(cpu, irq)	(local_irq_count[cpu]--)

#else
#error SMP not supported on sparc64 just yet
#endif /* __SMP__ */

void report_spurious_ivec(struct pt_regs *regs)
{
	printk("IVEC: Spurious interrupt vector received at (%016lx)\n",
	       regs->tpc);
	return;
}

void unexpected_irq(int irq, void *dev_cookie, struct pt_regs *regs)
{
	int i;
	struct irqaction *action;
	unsigned int cpu_irq;

	cpu_irq = irq & NR_IRQS;
	action = *(cpu_irq + irq_action);

	prom_printf("Unexpected IRQ[%d]: ", irq);
	prom_printf("PC[%016lx] NPC[%016lx] FP[%016lx]\n",
		    regs->tpc, regs->tnpc, regs->u_regs[14]);

	if(action) {
		prom_printf("Expecting: ");
		for(i = 0; i < 16; i++) {
			if(action->handler)
				prom_printf("[%s:%d:0x%016lx] ", action->name,
					    i, (unsigned long) action->handler);
		}
	}
	prom_printf("AIEEE\n");
	prom_printf("bogus interrupt received\n");
	prom_cmdline ();
}

void handler_irq(int irq, struct pt_regs *regs)
{
	struct irqaction *action;
	int cpu = smp_processor_id();

	/* XXX */
	if(irq != 14)
		clear_softint(1 << irq);

	irq_enter(cpu, irq);
	action = *(irq + irq_action);
	kstat.interrupts[irq]++;
	do {
		if(!action || !action->handler)
			unexpected_irq(irq, 0, regs);
		action->handler(irq, action->dev_id, regs);
		if(action->flags & SA_SYSIO_MASKED)
			*((unsigned int *)action->mask) = SYSIO_ICLR_IDLE;
	} while((action = action->next) != NULL);
	irq_exit(cpu, irq);
}

#ifdef CONFIG_BLK_DEV_FD
extern void floppy_interrupt(int irq, void *dev_cookie, struct pt_regs *regs);

void sparc_floppy_irq(int irq, void *dev_cookie, struct pt_regs *regs)
{
	struct irqaction *action = *(irq + irq_action);
	int cpu = smp_processor_id();

	irq_enter(cpu, irq);
	floppy_interrupt(irq, dev_cookie, regs);
	if(action->flags & SA_SYSIO_MASKED)
		*((unsigned int *)action->mask) = SYSIO_ICLR_IDLE;
	irq_exit(cpu, irq);
}
#endif

/* XXX This needs to be written for floppy driver, and soon will be necessary
 * XXX for serial driver as well.
 */
int request_fast_irq(unsigned int irq,
		     void (*handler)(int, void *, struct pt_regs *),
		     unsigned long irqflags, const char *name)
{
	return -1;
}

/* We really don't need these at all on the Sparc.  We only have
 * stubs here because they are exported to modules.
 */
unsigned long probe_irq_on(void)
{
  return 0;
}

int probe_irq_off(unsigned long mask)
{
  return 0;
}

/* XXX This is a hack, make it per-cpu so that SMP port will work correctly
 * XXX with mixed MHZ Ultras in the machine. -DaveM
 */
static unsigned long cpu_cfreq;
static unsigned long tick_offset;

/* XXX This doesn't belong here, just do this cruft in the timer.c handler code. */
static void timer_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	extern void timer_interrupt(int, void *, struct pt_regs *);
	unsigned long compare;
	
	if (!(get_softint () & 1)) {
		/* Just to be sure... */
		clear_softint(1 << 14);
		printk("Spurious level14 at %016lx\n", regs->tpc);
		return;
	}

	timer_interrupt(irq, dev_id, regs);

	/* Acknowledge INT_TIMER */
	clear_softint(1 << 0);

	/* Set up for next timer tick. */
	__asm__ __volatile__("rd	%%tick_cmpr, %0\n\t"
			     "add	%0, %1, %0\n\t"
			     "wr	%0, 0x0, %%tick_cmpr"
			     : "=r" (compare)
			     : "r" (tick_offset));
}

/* This is called from time_init() to get the jiffies timer going. */
void init_timers(void (*cfunc)(int, void *, struct pt_regs *))
{
	int node, err;

	/* XXX FIX this for SMP -JJ */
	node = linux_cpus [0].prom_node;
	cpu_cfreq = prom_getint(node, "clock-frequency");
	tick_offset = cpu_cfreq / HZ;
	err = request_irq(14, timer_handler, (SA_INTERRUPT|SA_STATIC_ALLOC),
			  "timer", NULL);
	if(err) {
		prom_printf("Serious problem, cannot register timer interrupt\n");
		prom_halt();
	} else {
		unsigned long flags;

		save_and_cli(flags);

		__asm__ __volatile__("wr	%0, 0x0, %%tick_cmpr\n\t"
				     "wrpr	%%g0, 0x0, %%tick"
				     : /* No outputs */
				     : "r" (tick_offset));
				     
		clear_softint (get_softint ());

		restore_flags(flags);
	}
	sti();
}

/* We use this nowhere else, so only define it's layout here. */
struct sun5_timer {
	volatile u32 count0, _unused0;
	volatile u32 limit0, _unused1;
	volatile u32 count1, _unused2;
	volatile u32 limit1, _unused3;
} *prom_timers;

static void map_prom_timers(void)
{
	unsigned int addr[3];
	int tnode, err;

	/* PROM timer node hangs out in the top level of device siblings... */
	tnode = prom_finddevice("/counter-timer");

	/* Assume if node is not present, PROM uses different tick mechanism
	 * which we should not care about.
	 */
	if(tnode == 0) {
		prom_timers = (struct sun5_timer *) 0;
		prom_printf("AIEEE, no timers\n");
		return;
	}

	/* If PROM is really using this, it must be mapped by him. */
	err = prom_getproperty(tnode, "address", (char *)addr, sizeof(addr));
	if(err == -1) {
		prom_printf("PROM does not have timer mapped, trying to continue.\n");
		prom_timers = (struct sun5_timer *) 0;
		return;
	}
	prom_timers = (struct sun5_timer *) addr[0];
}

static void kill_prom_timer(void)
{
	if(!prom_timers)
		return;

	/* Just as in sun4c/sun4m PROM uses timer which ticks at IRQ 14.
	 * We turn both off here just to be paranoid.
	 */
	prom_timers->limit0 = 0;
	prom_timers->limit1 = 0;
}

#if 0 /* Unused at this time. -DaveM */
static void enable_prom_timer(void)
{
	if(!prom_timers)
		return;

	/* Set it to fire off every 10ms. */
	prom_timers->limit1 = 0xa000270f;
	prom_timers->count1 = 0;
}
#endif

__initfunc(void init_IRQ(void))
{
	int i;

	map_prom_timers();
	kill_prom_timer();
	for(i = 0; i < NUM_IVECS; i++)
		ivector_to_mask[i] = 0;

	/* We need to clear any IRQ's pending in the soft interrupt
	 * registers, a spurious one could be left around from the
	 * PROM timer which we just disabled.
	 */
	clear_softint(get_softint());

	/* Now that ivector table is initialized, it is safe
	 * to receive IRQ vector traps.  We will normally take
	 * one or two right now, in case some device PROM used
	 * to boot us wants to speak to us.  We just ignore them.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %%g1\n\t"
			     "or	%%g1, %0, %%g1\n\t"
			     "wrpr	%%g1, 0x0, %%pstate"
			     : /* No outputs */
			     : "i" (PSTATE_IE)
			     : "g1");
}
