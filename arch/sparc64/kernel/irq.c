/* $Id: irq.c,v 1.66 1998/10/21 15:02:25 ecd Exp $
 * irq.c: UltraSparc IRQ handling/init/registry.
 *
 * Copyright (C) 1997  David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1998  Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1998  Jakub Jelinek    (jj@ultra.linux.cz)
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
#include <linux/delay.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/sbus.h>
#include <asm/iommu.h>
#include <asm/upa.h>
#include <asm/oplib.h>
#include <asm/timer.h>
#include <asm/smp.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

#ifdef CONFIG_PCI
#include <linux/pci.h>
#include <asm/pbm.h>
#endif

/* Internal flag, should not be visible elsewhere at all. */
#define SA_IMAP_MASKED		0x100
#define SA_DMA_SYNC		0x200

#ifdef __SMP__
void distribute_irqs(void);
static int irqs_have_been_distributed = 0;
#endif

/* UPA nodes send interrupt packet to UltraSparc with first data reg value
 * low 5 bits holding the IRQ identifier being delivered.  We must translate
 * this into a non-vector IRQ so we can set the softint on this cpu.  To
 * make things even more swift we store the complete mask here.
 */

#define NUM_HARD_IVECS	2048
#define NUM_IVECS	(NUM_HARD_IVECS + 64)	/* For SMP IRQ distribution alg. */

unsigned long ivector_to_mask[NUM_IVECS];

/* This is based upon code in the 32-bit Sparc kernel written mostly by
 * David Redman (djhr@tadpole.co.uk).
 */
#define MAX_STATIC_ALLOC	4
static struct irqaction static_irqaction[MAX_STATIC_ALLOC];
static int static_irq_count = 0;

/* XXX Must be exported so that fast IRQ handlers can get at it... -DaveM */
struct irqaction *irq_action[NR_IRQS+1] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL
};

#define IBF_DMA_SYNC	0x01
#define IBF_PCI		0x02
#define IBF_ACTIVE	0x04

#define __imap(bucket) ((bucket)->iclr + (bucket)->imap_off)
#define __bucket(irq) ((struct ino_bucket *)(unsigned long)(irq))
#define __irq(bucket) ((unsigned int)(unsigned long)(bucket))

static struct ino_bucket *bucket_base, *buckets, *endbuckets;

__initfunc(unsigned long irq_init(unsigned long start_mem, unsigned long end_mem))
{
	start_mem = (start_mem + 15) & ~15;
	bucket_base = buckets = (struct ino_bucket *)start_mem;
	endbuckets = buckets + 2048;
	return (unsigned long)endbuckets;
}

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction *action;
#ifdef __SMP__
	int j;
#endif

	for(i = 0; i < (NR_IRQS + 1); i++) {
		if(!(action = *(i + irq_action)))
			continue;
		len += sprintf(buf + len, "%3d: ", i);
#ifndef __SMP__
		len += sprintf(buf + len, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < smp_num_cpus; j++)
			len += sprintf(buf + len, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#endif
		len += sprintf(buf + len, "%c %s",
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

/* SBUS SYSIO INO number to Sparc PIL level. */
unsigned char sysio_ino_to_pil[] = {
	0, 1, 2, 7, 5, 7, 8, 9,		/* SBUS slot 0 */
	0, 1, 2, 7, 5, 7, 8, 9,		/* SBUS slot 1 */
	0, 1, 2, 7, 5, 7, 8, 9,		/* SBUS slot 2 */
	0, 1, 2, 7, 5, 7, 8, 9,		/* SBUS slot 3 */
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
static unsigned long sysio_irq_offsets[] = {
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

#define NUM_SYSIO_OFFSETS (sizeof(sysio_irq_offsets) / sizeof(sysio_irq_offsets[0]))

/* Convert Interrupt Mapping register pointer to assosciated
 * Interrupt Clear register pointer, SYSIO specific version.
 */
static unsigned int *sysio_imap_to_iclr(unsigned int *imap)
{
	unsigned long diff;

	diff = offset(iclr_unused0) - offset(imap_slot0);
	return (unsigned int *) (((unsigned long)imap) + diff);
}

#undef offset

#ifdef CONFIG_PCI
/* PCI PSYCHO INO number to Sparc PIL level. */
unsigned char psycho_ino_to_pil[] = {
	7, 5, 4, 2,			/* PCI A slot 0  Int A, B, C, D */
	7, 5, 4, 2,			/* PCI A slot 1  Int A, B, C, D */
	7, 5, 4, 2,			/* PCI A slot 2  Int A, B, C, D */
	7, 5, 4, 2,			/* PCI A slot 3  Int A, B, C, D */
	6, 4, 3, 1,			/* PCI B slot 0  Int A, B, C, D */
	6, 4, 3, 1,			/* PCI B slot 1  Int A, B, C, D */
	6, 4, 3, 1,			/* PCI B slot 2  Int A, B, C, D */
	6, 4, 3, 1,			/* PCI B slot 3  Int A, B, C, D */
	3,  /* SCSI */
	5,  /* Ethernet */
	8,  /* Parallel Port */
	13, /* Audio Record */
	14, /* Audio Playback */
	15, /* PowerFail */
	3,  /* second SCSI */
	11, /* Floppy */
	2,  /* Spare Hardware */
	9,  /* Keyboard */
	4,  /* Mouse */
	12, /* Serial */
	10, /* Timer 0 */
	11, /* Timer 1 */
	15, /* Uncorrectable ECC */
	15, /* Correctable ECC */
	15, /* PCI Bus A Error */
	15, /* PCI Bus B Error */
	1, /* Power Management */
};

/* INO number to IMAP register offset for PSYCHO external IRQ's.
 */
#define psycho_offset(x) ((unsigned long)(&(((struct psycho_regs *)0)->x)))

#define psycho_imap_offset(ino)						      \
	((ino & 0x20) ? (psycho_offset(imap_scsi) + (((ino) & 0x1f) << 3)) :  \
			(psycho_offset(imap_a_slot0) + (((ino) & 0x3c) << 1)))

#define psycho_iclr_offset(ino)						      \
	((ino & 0x20) ? (psycho_offset(iclr_scsi) + (((ino) & 0x1f) << 3)) :  \
			(psycho_offset(iclr_a_slot0[0]) + (((ino) & 0x1f)<<3)))

#endif

/* Now these are always passed a true fully specified sun4u INO. */
void enable_irq(unsigned int irq)
{
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long tid;
	unsigned int *imap;

	imap = __imap(bucket);
	if (!imap) return;

	/* We send it to our UPA MID, for SMP this will be different. */
	__asm__ __volatile__("ldxa [%%g0] %1, %0" : "=r" (tid) : "i" (ASI_UPA_CONFIG));
	tid = ((tid & UPA_CONFIG_MID) << 9);

	/* NOTE NOTE NOTE, IGN and INO are read-only, IGN is a product
	 * of this SYSIO's preconfigured IGN in the SYSIO Control
	 * Register, the hardware just mirrors that value here.
	 * However for Graphics and UPA Slave devices the full
	 * SYSIO_IMAP_INR field can be set by the programmer here.
	 *
	 * Things like FFB can now be handled via the new IRQ mechanism.
	 */
	*imap = SYSIO_IMAP_VALID | (tid & SYSIO_IMAP_TID);
}

/* This now gets passed true ino's as well. */
void disable_irq(unsigned int irq)
{
	struct ino_bucket *bucket = __bucket(irq);
	unsigned int *imap;

	imap = __imap(bucket);
	if (!imap) return;

	/* NOTE: We do not want to futz with the IRQ clear registers
	 *       and move the state to IDLE, the SCSI code does call
	 *       disable_irq() to assure atomicity in the queue cmd
	 *       SCSI adapter driver code.  Thus we'd lose interrupts.
	 */
	*imap &= ~(SYSIO_IMAP_VALID);
}

unsigned int build_irq(int pil, int inofixup, unsigned int *iclr, unsigned int *imap)
{
	if (buckets == endbuckets)
		panic("Out of IRQ buckets. Should not happen.\n");
	buckets->pil = pil;
	if (pil && (!iclr || !imap)) {
		prom_printf("Invalid build_irq %d %d %016lx %016lx\n", pil, inofixup, iclr, imap);
		prom_halt();
	}
	if (imap)
		buckets->ino = (*imap & (SYSIO_IMAP_IGN | SYSIO_IMAP_INO)) + inofixup;
	else
		buckets->ino = 0;
		
	buckets->iclr = iclr;
	buckets->flags = 0;
	buckets->imap_off = imap - iclr;
	return __irq(buckets++);
}

unsigned int sbus_build_irq(void *buscookie, unsigned int ino)
{
	struct linux_sbus *sbus = (struct linux_sbus *)buscookie;
	struct sysio_regs *sregs = sbus->iommu->sysio_regs;
	unsigned long offset;
	int pil;
	unsigned int *imap, *iclr;
	int sbus_level = 0;

	pil = sysio_ino_to_pil[ino];
	if(!pil) {
		printk("sbus_irq_build: Bad SYSIO INO[%x]\n", ino);
		panic("Bad SYSIO IRQ translations...");
	}
	offset = sysio_irq_offsets[ino];
	if(offset == ((unsigned long)-1)) {
		printk("get_irq_translations: Bad SYSIO INO[%x] cpu[%d]\n",
			ino, pil);
		panic("BAD SYSIO IRQ offset...");
	}
	offset += ((unsigned long)sregs);
	imap = ((unsigned int *)offset);

	/* SYSIO inconsistancy.  For external SLOTS, we have to select
	 * the right ICLR register based upon the lower SBUS irq level
	 * bits.
	 */
	if(ino >= 0x20) {
		iclr = sysio_imap_to_iclr(imap);
	} else {
		unsigned long iclraddr;
		int sbus_slot = (ino & 0x18)>>3;
		
		sbus_level = ino & 0x7;

		switch(sbus_slot) {
		case 0:
			iclr = &sregs->iclr_slot0;
			break;
		case 1:
			iclr = &sregs->iclr_slot1;
			break;
		case 2:
			iclr = &sregs->iclr_slot2;
			break;
		default:
		case 3:
			iclr = &sregs->iclr_slot3;
			break;
		};

		iclraddr = (unsigned long) iclr;
		iclraddr += ((sbus_level - 1) * 8);
		iclr = (unsigned int *) iclraddr;
	}
	return build_irq(pil, sbus_level, iclr, imap);
}

#ifdef CONFIG_PCI
unsigned int psycho_build_irq(void *buscookie, int imap_off, int ino, int need_dma_sync)
{
	struct linux_psycho *psycho = (struct linux_psycho *)buscookie;
	struct psycho_regs *pregs = psycho->psycho_regs;
	unsigned long addr;
	struct ino_bucket *bucket;
	int pil;
	unsigned int *imap, *iclr;
	int inofixup = 0;

	pil = psycho_ino_to_pil[ino & PCI_IRQ_INO];
	
	addr = (unsigned long) &pregs->imap_a_slot0;
	addr = addr + imap_off;
	imap = ((unsigned int *)addr) + 1;

	addr = (unsigned long) pregs;
	addr += psycho_iclr_offset(ino & (PCI_IRQ_INO));
	iclr = ((unsigned int *)addr) + 1;

	if(!(ino & 0x20))
		inofixup = ino & 0x03;

	bucket = __bucket(build_irq(pil, inofixup, iclr, imap));
	
	if (need_dma_sync)
		bucket->flags |= IBF_DMA_SYNC;
		
	bucket->flags |= IBF_PCI;
	return __irq(bucket);
}
#endif

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char *name, void *dev_id)
{
	struct irqaction *action, *tmp = NULL;
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long flags;
	int pending = 0;

	if (irq < 0x400000 || (irq & 0x80000000)) {
		prom_printf("request_irq with old style irq %08x %016lx\n", irq, handler);
		prom_halt();
	}
	
	if(!handler)
	    return -EINVAL;

	if (!bucket->pil)
		irqflags &= ~SA_IMAP_MASKED;
	else {
		irqflags |= SA_IMAP_MASKED;
		if (bucket->flags & IBF_PCI) {
			/*
			 * PCI IRQs should never use SA_INTERRUPT.
			 */
			irqflags &= ~(SA_INTERRUPT);

			/*
			 * Check wether we _should_ use DMA Write Sync
			 * (for devices behind bridges behind APB). 
			 *
			 * XXX: Not implemented, yet.
			 */
			if (bucket->flags & IBF_DMA_SYNC)
				irqflags |= SA_DMA_SYNC;
		}
	}

	action = *(bucket->pil + irq_action);
	if(action) {
		if((action->flags & SA_SHIRQ) && (irqflags & SA_SHIRQ))
			for (tmp = action; tmp->next; tmp = tmp->next)
				;
		else
			return -EBUSY;

		if((action->flags & SA_INTERRUPT) ^ (irqflags & SA_INTERRUPT)) {
			printk("Attempt to mix fast and slow interrupts on IRQ%d "
			       "denied\n", bucket->pil);
			return -EBUSY;
		}   
		action = NULL;		/* Or else! */
	}

	save_and_cli(flags);

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if(irqflags & SA_STATIC_ALLOC) {
	    if(static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed "
		       "using kmalloc\n", irq, name);
	}	
	if(action == NULL)
	    action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						 GFP_KERNEL);
	
	if(!action) { 
		restore_flags(flags);
		return -ENOMEM;
	}

	if (irqflags & SA_IMAP_MASKED) {
		pending = ((ivector_to_mask[bucket->ino] & 0x80000000) != 0);
		ivector_to_mask[bucket->ino] = (1 << bucket->pil);
		if(pending)
			ivector_to_mask[bucket->ino] |= 0x80000000;
		bucket->flags |= IBF_ACTIVE;
	}

	action->mask = (unsigned long) bucket;
	action->handler = handler;
	action->flags = irqflags;
	action->name = name;
	action->next = NULL;
	action->dev_id = dev_id;

	if(tmp)
		tmp->next = action;
	else
		*(bucket->pil + irq_action) = action;

	enable_irq(irq);

	/* We ate the IVEC already, this makes sure it does not get lost. */
	if(pending)
		set_softint(1 << bucket->pil);

	restore_flags(flags);
#ifdef __SMP__
	if(irqs_have_been_distributed)
		distribute_irqs();
#endif
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action;
	struct irqaction *tmp = NULL;
	unsigned long flags;
	struct ino_bucket *bucket = __bucket(irq), *bp;

	if (irq < 0x400000 || (irq & 0x80000000)) {
		prom_printf("free_irq with old style irq %08x\n", irq);
		prom_halt();
	}
	
	action = *(bucket->pil + irq_action);
	if(!action->handler) {
		printk("Freeing free IRQ %d\n", bucket->pil);
		return;
	}
	if(dev_id) {
		for( ; action; action = action->next) {
			if(action->dev_id == dev_id)
				break;
			tmp = action;
		}
		if(!action) {
			printk("Trying to free free shared IRQ %d\n", bucket->pil);
			return;
		}
	} else if(action->flags & SA_SHIRQ) {
		printk("Trying to free shared IRQ %d with NULL device ID\n", bucket->pil);
		return;
	}

	if(action->flags & SA_STATIC_ALLOC) {
		printk("Attempt to free statically allocated IRQ %d (%s)\n",
		       bucket->pil, action->name);
		return;
	}

	save_and_cli(flags);
	if(action && tmp)
		tmp->next = action->next;
	else
		*(bucket->pil + irq_action) = action->next;

	if(action->flags & SA_IMAP_MASKED) {
		unsigned int *imap = __imap(bucket);

		/*
		 * Only free when no other shared irq uses this bucket.
		 */
		tmp = *(bucket->pil + irq_action);
		for( ; tmp; tmp = tmp->next)
			if ((struct ino_bucket *)tmp->mask == bucket)
				goto out;

		ivector_to_mask[bucket->ino] = 0;

		bucket->flags &= ~IBF_ACTIVE;
		for (bp = bucket_base; bp < endbuckets; bp++)
			if (__imap(bp) == imap && (bp->flags & IBF_ACTIVE))
				break;
		/*
		 * Only disable when no other sub-irq levels of
		 * the same imap are active.
		 */
		if (bp == endbuckets)
			disable_irq(irq);
	}

out:
	kfree(action);
	restore_flags(flags);
}

/* Only uniprocessor needs this IRQ/BH locking depth, on SMP it
 * lives in the per-cpu structure for cache reasons.
 */
#ifndef __SMP__
unsigned int local_irq_count;
unsigned int local_bh_count;

#define irq_enter(cpu, irq)	(local_irq_count++)
#define irq_exit(cpu, irq)	(local_irq_count--)
#else
atomic_t global_bh_lock = ATOMIC_INIT(0);
spinlock_t global_bh_count = SPIN_LOCK_UNLOCKED;

/* Who has global_irq_lock. */
unsigned char global_irq_holder = NO_PROC_ID;

/* This protects IRQ's. */
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;

/* Global IRQ locking depth. */
atomic_t global_irq_count = ATOMIC_INIT(0);

#define irq_enter(cpu, irq)			\
do {	hardirq_enter(cpu);			\
	spin_unlock_wait(&global_irq_lock);	\
} while(0)
#define irq_exit(cpu, irq)	hardirq_exit(cpu)

static void show(char * str)
{
	int cpu = smp_processor_id();

	printk("\n%s, CPU %d:\n", str, cpu);
	printk("irq:  %d [%d %d]\n",
	       atomic_read(&global_irq_count),
	       cpu_data[0].irq_count, cpu_data[1].irq_count);
	printk("bh:   %d [%d %d]\n",
	       (spin_is_locked(&global_bh_count) ? 1 : 0),
	       cpu_data[0].bh_count, cpu_data[1].bh_count);
}

#define MAXCOUNT 100000000

static inline void wait_on_bh(void)
{
	int count = MAXCOUNT;
	do {
		if(!--count) {
			show("wait_on_bh");
			count = 0;
		}
		membar("#LoadLoad");
	} while(spin_is_locked(&global_bh_count));
}

#define SYNC_OTHER_ULTRAS(x)	udelay(x+1)

static inline void wait_on_irq(int cpu)
{
	int count = MAXCOUNT;
	for(;;) {
		membar("#LoadLoad");
		if (!atomic_read (&global_irq_count)) {
			if (local_bh_count || ! spin_is_locked(&global_bh_count))
				break;
		}
		spin_unlock (&global_irq_lock);
		membar("#StoreLoad | #StoreStore");
		for(;;) {
			if (!--count) {
				show("wait_on_irq");
				count = ~0;
			}
			__sti();
			SYNC_OTHER_ULTRAS(cpu);
			__cli();
			if (atomic_read(&global_irq_count))
				continue;
			if (spin_is_locked (&global_irq_lock))
				continue;
			if (!local_bh_count && spin_is_locked (&global_bh_count))
				continue;
			if (spin_trylock(&global_irq_lock))
				break;
		}
	}
}

void synchronize_bh(void)
{
	if (spin_is_locked (&global_bh_count) && !in_interrupt())
		wait_on_bh();
}

void synchronize_irq(void)
{
	if (atomic_read(&global_irq_count)) {
		cli();
		sti();
	}
}

static inline void get_irqlock(int cpu)
{
	if (! spin_trylock(&global_irq_lock)) {
		if ((unsigned char) cpu == global_irq_holder)
			return;
		do {
			while (spin_is_locked (&global_irq_lock))
				membar("#LoadLoad");
		} while(! spin_trylock(&global_irq_lock));
	}
	wait_on_irq(cpu);
	global_irq_holder = cpu;
}

void __global_cli(void)
{
	unsigned long flags;

	__save_flags(flags);
	if(flags == 0) {
		int cpu = smp_processor_id();
		__cli();
		if (! local_irq_count)
			get_irqlock(cpu);
	}
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (! local_irq_count)
		release_irqlock(cpu);
	__sti();
}

unsigned long __global_save_flags(void)
{
	unsigned long flags, local_enabled, retval;

	__save_flags(flags);
	local_enabled = ((flags == 0) ? 1 : 0);
	retval = 2 + local_enabled;
	if (! local_irq_count) {
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
	{
		unsigned long pc;
		__asm__ __volatile__("mov %%i7, %0" : "=r" (pc));
		printk("global_restore_flags: Bogon flags(%016lx) caller %016lx\n",
		       flags, pc);
	}
	}
}

#endif /* __SMP__ */

void report_spurious_ivec(struct pt_regs *regs)
{
	extern unsigned long ivec_spurious_cookie;

#if 0
	printk("IVEC: Spurious interrupt vector (%016lx) received at (%016lx)\n",
	       ivec_spurious_cookie, regs->tpc);
#endif

	/* We can actually see this on Ultra/PCI PCI cards, which are bridges
	 * to other devices.  Here a single IMAP enabled potentially multiple
	 * unique interrupt sources (which each do have a unique ICLR register.
	 *
	 * So what we do is just register that the IVEC arrived, when registered
	 * for real the request_irq() code will check the high bit and signal
	 * a local CPU interrupt for it.
	 */
	ivector_to_mask[ivec_spurious_cookie] |= (0x80000000);
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
	struct ino_bucket *bucket = NULL;
	struct irqaction *action, *act;
	int cpu = smp_processor_id();

#ifndef __SMP__
	/*
	 * Check for TICK_INT on level 14 softint.
	 */
	if ((irq == 14) && (get_softint() & (1UL << 0)))
		irq = 0;
#endif
	clear_softint(1 << irq);

	irq_enter(cpu, irq);
	action = *(irq + irq_action);
	kstat.irqs[cpu][irq]++;
	if(!action) {
		unexpected_irq(irq, 0, regs);
	} else {
		act = action;
		do {
			if(act->flags & SA_IMAP_MASKED) {
				bucket = (struct ino_bucket *)act->mask;
				if(!(ivector_to_mask[bucket->ino] & 0x80000000))
					continue;
			}
			act->handler(__irq(bucket), act->dev_id, regs);
		} while((act = act->next) != NULL);
		act = action;
		do {
			if(act->flags & SA_IMAP_MASKED) {
				bucket = (struct ino_bucket *)act->mask;
				if(!(ivector_to_mask[bucket->ino] & 0x80000000))
					continue;
				ivector_to_mask[bucket->ino] &= ~(0x80000000);
				*(bucket->iclr) = SYSIO_ICLR_IDLE;
			}
		} while((act = act->next) != NULL);
	}
	irq_exit(cpu, irq);
}

#ifdef CONFIG_BLK_DEV_FD
extern void floppy_interrupt(int irq, void *dev_cookie, struct pt_regs *regs);

void sparc_floppy_irq(int irq, void *dev_cookie, struct pt_regs *regs)
{
	struct irqaction *action = *(irq + irq_action);
	struct ino_bucket *bucket;
	int cpu = smp_processor_id();

	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq]++;
	bucket = (struct ino_bucket *)action->mask;
	floppy_interrupt(irq, dev_cookie, regs);
	ivector_to_mask[bucket->ino] &= ~(0x80000000);
	*(bucket->iclr) = SYSIO_ICLR_IDLE;
	irq_exit(cpu, irq);
}
#endif

/* The following assumes that the branch lies before the place we
 * are branching to.  This is the case for a trap vector...
 * You have been warned.
 */
#define SPARC_BRANCH(dest_addr, inst_addr) \
          (0x10800000 | ((((dest_addr)-(inst_addr))>>2)&0x3fffff))

#define SPARC_NOP (0x01000000)

static void install_fast_irq(unsigned int cpu_irq,
			     void (*handler)(int, void *, struct pt_regs *))
{
	extern unsigned long sparc64_ttable_tl0;
	unsigned long ttent = (unsigned long) &sparc64_ttable_tl0;
	unsigned int *insns;

	ttent += 0x820;
	ttent += (cpu_irq - 1) << 5;
	insns = (unsigned int *) ttent;
	insns[0] = SPARC_BRANCH(((unsigned long) handler),
				((unsigned long)&insns[0]));
	insns[1] = SPARC_NOP;
	__asm__ __volatile__("membar #StoreStore; flush %0" : : "r" (ttent));
}

int request_fast_irq(unsigned int irq,
		     void (*handler)(int, void *, struct pt_regs *),
		     unsigned long irqflags, const char *name, void *dev_id)
{
	struct irqaction *action;
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long flags;

	if (irq < 0x400000 || (irq & 0x80000000)) {
		prom_printf("request_irq with old style irq %08x %016lx\n", irq, handler);
		prom_halt();
	}
	
	if(!handler)
		return -EINVAL;

	if ((bucket->pil == 0) || (bucket->pil == 14)) {
		printk("request_fast_irq: Trying to register shared IRQ 0 or 14.\n");
		return -EBUSY;
	}

	action = *(bucket->pil + irq_action);
	if(action) {
		if(action->flags & SA_SHIRQ)
			panic("Trying to register fast irq when already shared.\n");
		if(irqflags & SA_SHIRQ)
			panic("Trying to register fast irq as shared.\n");
		printk("request_fast_irq: Trying to register yet already owned.\n");
		return -EBUSY;
	}
	save_and_cli(flags);
	if(irqflags & SA_STATIC_ALLOC) {
		if(static_irq_count < MAX_STATIC_ALLOC)
			action = &static_irqaction[static_irq_count++];
		else
			printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed "
			       "using kmalloc\n", bucket->pil, name);
	}
	if(action == NULL)
		action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						     GFP_KERNEL);
	if(!action) {
		restore_flags(flags);
		return -ENOMEM;
	}
	install_fast_irq(bucket->pil, handler);

	ivector_to_mask[bucket->ino] = (1 << bucket->pil);

	action->mask = (unsigned long) bucket;
	action->handler = handler;
	action->flags = irqflags | SA_IMAP_MASKED;
	action->dev_id = NULL;
	action->name = name;
	action->next = NULL;

	*(bucket->pil + irq_action) = action;
	enable_irq(irq);

	restore_flags(flags);
#ifdef __SMP__
	if(irqs_have_been_distributed)
		distribute_irqs();
#endif
	return 0;
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

/* This is gets the master TICK_INT timer going. */
void init_timers(void (*cfunc)(int, void *, struct pt_regs *),
		 unsigned long *clock)
{
	unsigned long flags;
	extern unsigned long timer_tick_offset;
	int node, err;
#ifdef __SMP__
	extern void smp_tick_init(void);
#endif

	node = linux_cpus[0].prom_node;
	*clock = prom_getint(node, "clock-frequency");
	timer_tick_offset = *clock / HZ;
#ifdef __SMP__
	smp_tick_init();
#endif

	/* Register IRQ handler. */
	err = request_irq(build_irq(0, 0, NULL, NULL), cfunc, (SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", NULL);

	if(err) {
		prom_printf("Serious problem, cannot register TICK_INT\n");
		prom_halt();
	}

	save_and_cli(flags);

	/* Set things up so user can access tick register for profiling
	 * purposes.
	 */
	__asm__ __volatile__("
		sethi	%%hi(0x80000000), %%g1
		sllx	%%g1, 32, %%g1
		rd	%%tick, %%g2
		add	%%g2, 6, %%g2
		andn	%%g2, %%g1, %%g2
		wrpr	%%g2, 0, %%tick
"	: /* no outputs */
	: /* no inputs */
	: "g1", "g2");

	__asm__ __volatile__("
		rd	%%tick, %%g1
		add	%%g1, %0, %%g1
		wr	%%g1, 0x0, %%tick_cmpr"
	: /* no outputs */
	: "r" (timer_tick_offset)
	: "g1");

	restore_flags(flags);
	sti();
}

#ifdef __SMP__
/* Called from smp_commence, when we know how many cpus are in the system
 * and can have device IRQ's directed at them.
 */
/* #define SMP_IRQ_VERBOSE */
void distribute_irqs(void)
{
	unsigned long flags;
	int cpu, level;

#ifdef SMP_IRQ_VERBOSE
	printk("SMP: redistributing interrupts...\n");
#endif
	save_and_cli(flags);
	cpu = 0;
	for(level = 0; level < NR_IRQS; level++) {
		struct irqaction *p = irq_action[level];

		while(p) {
			if(p->flags & SA_IMAP_MASKED) {
				struct ino_bucket *bucket = (struct ino_bucket *)p->mask;
				unsigned int *imap = __imap(bucket);
				unsigned int val;
				unsigned long tid = __cpu_logical_map[cpu] << 26;

				val = *imap;
				*imap = SYSIO_IMAP_VALID | (tid & SYSIO_IMAP_TID);

#ifdef SMP_IRQ_VERBOSE
				printk("SMP: Redirecting IGN[%x] INO[%x] "
				       "to cpu %d [%s]\n",
				       (val & SYSIO_IMAP_IGN) >> 6,
				       (val & SYSIO_IMAP_INO), cpu,
				       p->name);
#endif

				cpu++;
				if (cpu >= NR_CPUS || __cpu_logical_map[cpu] == -1)
					cpu = 0;
			}
			p = p->next;
		}
	}
	restore_flags(flags);
	irqs_have_been_distributed = 1;
}
#endif


struct sun5_timer *prom_timers;
static u64 prom_limit0, prom_limit1;

static void map_prom_timers(void)
{
	unsigned int addr[3];
	int tnode, err;

	/* PROM timer node hangs out in the top level of device siblings... */
	tnode = prom_finddevice("/counter-timer");

	/* Assume if node is not present, PROM uses different tick mechanism
	 * which we should not care about.
	 */
	if(tnode == 0 || tnode == -1) {
		prom_timers = (struct sun5_timer *) 0;
		return;
	}

	/* If PROM is really using this, it must be mapped by him. */
	err = prom_getproperty(tnode, "address", (char *)addr, sizeof(addr));
	if(err == -1) {
		prom_printf("PROM does not have timer mapped, trying to continue.\n");
		prom_timers = (struct sun5_timer *) 0;
		return;
	}
	prom_timers = (struct sun5_timer *) ((unsigned long)addr[0]);
}

static void kill_prom_timer(void)
{
	if(!prom_timers)
		return;

	/* Save them away for later. */
	prom_limit0 = prom_timers->limit0;
	prom_limit1 = prom_timers->limit1;

	/* Just as in sun4c/sun4m PROM uses timer which ticks at IRQ 14.
	 * We turn both off here just to be paranoid.
	 */
	prom_timers->limit0 = 0;
	prom_timers->limit1 = 0;

	/* Wheee, eat the interrupt packet too... */
	__asm__ __volatile__("
	mov	0x40, %%g2
	ldxa	[%%g0] %0, %%g1
	ldxa	[%%g2] %1, %%g1
	stxa	%%g0, [%%g0] %0
	membar	#Sync
"	: /* no outputs */
	: "i" (ASI_INTR_RECEIVE), "i" (ASI_UDB_INTR_R)
	: "g1", "g2");
}

void enable_prom_timer(void)
{
	if(!prom_timers)
		return;

	/* Set it to whatever was there before. */
	prom_timers->limit1 = prom_limit1;
	prom_timers->count1 = 0;
	prom_timers->limit0 = prom_limit0;
	prom_timers->count0 = 0;
}

__initfunc(void init_IRQ(void))
{
	static int called = 0;

	if (called == 0) {
		int i;

		called = 1;
		map_prom_timers();
		kill_prom_timer();
		for(i = 0; i < NUM_IVECS; i++)
			ivector_to_mask[i] = 0;
	}

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
