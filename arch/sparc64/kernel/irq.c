/* $Id: irq.c,v 1.39 1997/08/31 03:11:18 davem Exp $
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

struct ino_bucket {
	struct ino_bucket *next;
	unsigned int ino;
	unsigned int *imap;
	unsigned int *iclr;
};

#define INO_HASHSZ	(NUM_HARD_IVECS >> 2)
#define NUM_INO_STATIC	4
static struct ino_bucket *ino_hash[INO_HASHSZ] = { NULL, };
static struct ino_bucket static_ino_buckets[NUM_INO_STATIC];
static int static_ino_bucket_count = 0;

static inline struct ino_bucket *__ino_lookup(unsigned int hash, unsigned int ino)
{
	struct ino_bucket *ret = ino_hash[hash];

	for(ret = ino_hash[hash]; ret && ret->ino != ino; ret = ret->next)
		;

	return ret;
}

static inline struct ino_bucket *ino_lookup(unsigned int ino)
{
	return __ino_lookup((ino & (INO_HASHSZ - 1)), ino);
}

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
#if 0
#ifdef CONFIG_PCI
	len += sprintf(buf + len, "ISTAT: PCI[%016lx] OBIO[%016lx]\n",
		       psycho_root->psycho_regs->pci_istate,
		       psycho_root->psycho_regs->obio_istate);
#endif
#endif
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

/* XXX Old compatability cruft, get rid of me when all drivers have been
 * XXX converted to dcookie registry calls... -DaveM
 */
static unsigned int *sysio_irq_to_imap(unsigned int irq)
{
	unsigned long offset;
	struct sysio_regs *sregs;

	if((irq == 14) ||
	   (irq >= NUM_SYSIO_OFFSETS) ||
	   ((offset = sysio_irq_offsets[irq]) == ((unsigned long)-1)))
		return NULL;
	sregs = SBus_chain->iommu->sysio_regs;
	offset += ((unsigned long) sregs);
	return ((unsigned int *)offset);
}

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
	7, 5, 5, 2,			/* PCI A slot 0  Int A, B, C, D */
	7, 5, 5, 2,			/* PCI A slot 1  Int A, B, C, D */
	0, 0, 0, 0,
	0, 0, 0, 0,
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
	12, /* Keyboard/Mouse/Serial */
	11, /* Floppy */
	2,  /* Spare Hardware */
	12, /* Keyboard */
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

#define psycho_imap_offset(ino)							\
	((ino & 0x20) ? (psycho_offset(imap_scsi) + (((ino) & 0x1f) << 3)) :	\
			(psycho_offset(imap_a_slot0) + (((ino) & 0x3c) << 1)))

#define psycho_iclr_offset(ino)							\
	((ino & 0x20) ? (psycho_offset(iclr_scsi) + (((ino) & 0x1f) << 3)) :	\
			(psycho_offset(iclr_a_slot0[0]) + (((ino) & 0x1f) << 3)))

#endif

/* Now these are always passed a true fully specified sun4u INO. */
void enable_irq(unsigned int ino)
{
	struct ino_bucket *bucket;
	unsigned long tid;
	unsigned int *imap;

#ifdef CONFIG_PCI
	if(PCI_IRQ_P(ino))
		ino &= (PCI_IRQ_IGN | PCI_IRQ_INO);
#endif
	bucket = ino_lookup(ino);
	if(!bucket)
		return;

	imap = bucket->imap;

	/* We send it to our UPA MID, for SMP this will be different. */
	__asm__ __volatile__("ldxa [%%g0] %1, %0" : "=r" (tid) : "i" (ASI_UPA_CONFIG));
	tid = ((tid & UPA_CONFIG_MID) << 9);

	/* NOTE NOTE NOTE, IGN and INO are read-only, IGN is a product
	 * of this SYSIO's preconfigured IGN in the SYSIO Control
	 * Register, the hardware just mirrors that value here.
	 * However for Graphics and UPA Slave devices the full
	 * SYSIO_IMAP_INR field can be set by the programmer here.
	 *
	 * Things like FFB can now be handled via the dcookie mechanism.
	 */
	*imap = SYSIO_IMAP_VALID | (tid & SYSIO_IMAP_TID);
}

/* This now gets passed true ino's as well. */
void disable_irq(unsigned int ino)
{
	struct ino_bucket *bucket;
	unsigned int *imap;

#ifdef CONFIG_PCI
	if(PCI_IRQ_P(ino))
		ino &= (PCI_IRQ_IGN | PCI_IRQ_INO);
#endif
	bucket = ino_lookup(ino);
	if(!bucket)
		return;

	imap = bucket->imap;

	/* NOTE: We do not want to futz with the IRQ clear registers
	 *       and move the state to IDLE, the SCSI code does call
	 *       disable_irq() to assure atomicity in the queue cmd
	 *       SCSI adapter driver code.  Thus we'd lose interrupts.
	 */
	*imap &= ~(SYSIO_IMAP_VALID);
}

static void get_irq_translations(int *cpu_irq, int *ivindex_fixup,
				 unsigned int **imap, unsigned int **iclr,
				 void *busp, unsigned long flags,
				 unsigned int irq)
{
	if(*cpu_irq != -1 && *imap != NULL && *iclr != NULL)
		return;

	if(*cpu_irq != -1 || *imap != NULL || *iclr != NULL || busp == NULL) {
		printk("get_irq_translations: Partial specification, this is bad.\n");
		printk("get_irq_translations: cpu_irq[%d] imap[%p] iclr[%p] busp[%p]\n",
		       *cpu_irq, *imap, *iclr, busp);
		panic("Bad IRQ translations...");
	}

	if(SA_BUS(flags) == SA_SBUS) {
		struct linux_sbus *sbusp = busp;
		struct sysio_regs *sregs = sbusp->iommu->sysio_regs;
		unsigned long offset;

		*cpu_irq = sysio_ino_to_pil[irq];
		if(*cpu_irq == 0) {
			printk("get_irq_translations: Bad SYSIO INO[%x]\n", irq);
			panic("Bad SYSIO IRQ translations...");
		}
		offset = sysio_irq_offsets[irq];
		if(offset == ((unsigned long)-1)) {
			printk("get_irq_translations: Bad SYSIO INO[%x] cpu[%d]\n",
			       irq, *cpu_irq);
			panic("BAD SYSIO IRQ offset...");
		}
		offset += ((unsigned long)sregs);
		*imap = ((unsigned int *)offset);

		/* SYSIO inconsistancy.  For external SLOTS, we have to select
		 * the right ICLR register based upon the lower SBUS irq level
		 * bits.
		 */
		if(irq >= 0x20) {
			*iclr = sysio_imap_to_iclr(*imap);
		} else {
			unsigned long iclraddr;
			int sbus_slot = (irq & 0x18)>>3;
			int sbus_level = irq & 0x7;

			switch(sbus_slot) {
			case 0:
				*iclr = &sregs->iclr_slot0;
				break;
			case 1:
				*iclr = &sregs->iclr_slot1;
				break;
			case 2:
				*iclr = &sregs->iclr_slot2;
				break;
			case 3:
				*iclr = &sregs->iclr_slot3;
				break;
			};

			iclraddr = (unsigned long) *iclr;
			iclraddr += ((sbus_level - 1) * 8);
			*iclr = (unsigned int *) iclraddr;

#if 0 /* DEBUGGING */
			printk("SYSIO_FIXUP: slot[%x] level[%x] iclr[%p] ",
			       sbus_slot, sbus_level, *iclr);
#endif

			/* Also, make sure this is accounted for in ivindex
			 * computations done by the caller.
			 */
			*ivindex_fixup = sbus_level;
		}
		return;
	}
#ifdef CONFIG_PCI
	if(SA_BUS(flags) == SA_PCI) {
		struct pci_bus *pbusp = busp;
		struct linux_pbm_info *pbm = pbusp->sysdata;
		struct psycho_regs *pregs = pbm->parent->psycho_regs;
		unsigned long offset;

		*cpu_irq = psycho_ino_to_pil[irq & 0x3f];
		if(*cpu_irq == 0) {
			printk("get_irq_translations: Bad PSYCHO INO[%x]\n", irq);
			panic("Bad PSYCHO IRQ translations...");
		}
		offset = psycho_imap_offset(irq);
		if(offset == ((unsigned long)-1)) {
			printk("get_irq_translations: Bad PSYCHO INO[%x] cpu[%d]\n",
			       irq, *cpu_irq);
			panic("Bad PSYCHO IRQ offset...");
		}
		offset += ((unsigned long)pregs);
		*imap = ((unsigned int *)offset) + 1;
		*iclr = (unsigned int *)
			(((unsigned long)pregs) + psycho_imap_offset(irq));
		return;
	}
#endif
#if 0	/* XXX More to do before we can use this. -DaveM */
	if(SA_BUS(flags) == SA_FHC) {
		struct fhc_bus *fbusp = busp;
		struct fhc_regs *fregs = fbusp->regs;
		unsigned long offset;

		*cpu_irq = fhc_ino_to_pil[irq];
		if(*cpu_irq == 0) {
			printk("get_irq_translations: Bad FHC INO[%x]\n", irq);
			panic("Bad FHC IRQ translations...");
		}
		offset = fhc_irq_offset[*cpu_irq];
		if(offset == ((unsigned long)-1)) {
			printk("get_irq_translations: Bad FHC INO[%x] cpu[%d]\n",
			       irq, *cpu_irq);
			panic("Bad FHC IRQ offset...");
		}
		offset += ((unsigned long)pregs);
		*imap = (((unsigned int *)offset)+1);
		*iclr = fhc_imap_to_iclr(*imap);
		return;
	}
#endif
	printk("get_irq_translations: IRQ register for unknown bus type.\n");
	printk("get_irq_translations: BUS[%lx] IRQ[%x]\n",
	       SA_BUS(flags), irq);
	panic("Bad IRQ bus type...");
}

#ifdef CONFIG_PCI
static void pci_irq_frobnicate(int *cpu_irq, int *ivindex_fixup,
			       unsigned int **imap, unsigned int **iclr,
			       unsigned int irq)
{
	struct linux_psycho *psycho = psycho_root;
	struct psycho_regs *pregs = psycho->psycho_regs;
	unsigned long addr, imoff;

	addr = (unsigned long) &pregs->imap_a_slot0;
	imoff = (irq & PCI_IRQ_IMAP_OFF) >> PCI_IRQ_IMAP_OFF_SHFT;
	addr = addr + imoff;

	*imap = ((unsigned int *)addr) + 1;

	addr = (unsigned long) pregs;
	addr += psycho_iclr_offset(irq & (PCI_IRQ_INO));
	*iclr = ((unsigned int *)addr) + 1;

	*cpu_irq = psycho_ino_to_pil[irq & (PCI_IRQ_INO)];
	if(*cpu_irq == 0) {
		printk("get_irq_translations: BAD PSYCHO INO[%x]\n", irq);
		panic("Bad PSYCHO IRQ frobnication...");
	}

	/* IVINDEX fixup only needed for PCI slot irq lines. */
	if(!(irq & 0x20))
		*ivindex_fixup = irq & 0x03;
}
#endif

/* Once added, they are never removed. */
static struct ino_bucket *add_ino_hash(unsigned int ivindex,
				       unsigned int *imap, unsigned int *iclr,
				       unsigned long flags)
{
	struct ino_bucket *new = NULL, **hashp;
	unsigned int hash = (ivindex & (INO_HASHSZ - 1));

	new = __ino_lookup(hash, ivindex);
	if(new)
		return new;
	if(flags & SA_STATIC_ALLOC) {
		if(static_ino_bucket_count < NUM_INO_STATIC)
			new = &static_ino_buckets[static_ino_bucket_count++];
		else
			printk("Request for ino bucket SA_STATIC_ALLOC failed "
			       "using kmalloc\n");
	}
	if(new == NULL)
		new = kmalloc(sizeof(struct ino_bucket), GFP_KERNEL);
	if(new) {
		hashp = &ino_hash[hash];
		new->imap = imap;
		new->iclr = iclr;
		new->ino  = ivindex;
		new->next = *hashp;
		*hashp = new;
	}
	return new;
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char *name, void *dev_id)
{
	struct irqaction *action, *tmp = NULL;
	struct devid_cookie *dcookie = NULL;
	struct ino_bucket *bucket = NULL;
	unsigned long flags;
	unsigned int *imap, *iclr;
	void *bus_id = NULL;
	int ivindex, ivindex_fixup, cpu_irq = -1;
	
	if(!handler)
	    return -EINVAL;

	imap = iclr = NULL;

	ivindex_fixup = 0;
#ifdef CONFIG_PCI
	if(PCI_IRQ_P(irq)) {
		pci_irq_frobnicate(&cpu_irq, &ivindex_fixup, &imap, &iclr, irq);
	} else
#endif
	if(irqflags & SA_DCOOKIE) {
		if(!dev_id) {
			printk("request_irq: SA_DCOOKIE but dev_id is NULL!\n");
			panic("Bogus irq registry.");
		}
		dcookie		= dev_id;
		dev_id		= dcookie->real_dev_id;
		cpu_irq		= dcookie->pil;
		imap		= dcookie->imap;
		iclr		= dcookie->iclr;
		bus_id		= dcookie->bus_cookie;
		get_irq_translations(&cpu_irq, &ivindex_fixup, &imap,
				     &iclr, bus_id, irqflags, irq);
	} else {
		/* XXX NOTE: This code is maintained for compatability until I can
		 * XXX       verify that all drivers sparc64 will use are updated
		 * XXX       to use the new IRQ registry dcookie interface.  -DaveM
		 */
		if(irq == 14)
			cpu_irq = irq;
		else
			cpu_irq = sysio_ino_to_pil[irq];
		imap = sysio_irq_to_imap(irq);
		if(!imap) {
			printk("request_irq: BAD, null imap for old style "
			       "irq registry IRQ[%x].\n", irq);
			panic("Bad IRQ registery...");
		}
		iclr = sysio_imap_to_iclr(imap);
	}
	ivindex = (*imap & (SYSIO_IMAP_IGN | SYSIO_IMAP_INO));
	ivindex += ivindex_fixup;

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

	bucket = add_ino_hash(ivindex, imap, iclr, irqflags);
	if(!bucket) {
		kfree(action);
		restore_flags(flags);
		return -ENOMEM;
	}

	ivector_to_mask[ivindex] = (1 << cpu_irq);

	if(dcookie) {
		dcookie->ret_ino = ivindex;
		dcookie->ret_pil = cpu_irq;
	}

	action->mask = (unsigned long) bucket;
	action->handler = handler;
	action->flags = irqflags | SA_IMAP_MASKED;
	action->name = name;
	action->next = NULL;
	action->dev_id = dev_id;

	if(tmp)
		tmp->next = action;
	else
		*(cpu_irq + irq_action) = action;

	enable_irq(ivindex);
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
	unsigned int cpu_irq;
	int ivindex = -1;

	if(irq == 14) {
		cpu_irq = irq;
	} else {
#ifdef CONFIG_PCI
		if(PCI_IRQ_P(irq))
			cpu_irq = psycho_ino_to_pil[irq & PCI_IRQ_INO];
		else
#endif
			cpu_irq = sysio_ino_to_pil[irq];
	}
	action = *(cpu_irq + irq_action);
	if(!action->handler) {
		printk("Freeing free IRQ %d\n", irq);
		return;
	}
	if(dev_id) {
		for( ; action; action = action->next) {
			if(action->dev_id == dev_id)
				break;
			tmp = action;
		}
		if(!action) {
			printk("Trying to free free shared IRQ %d\n", irq);
			return;
		}
	} else if(action->flags & SA_SHIRQ) {
		printk("Trying to free shared IRQ %d with NULL device ID\n", irq);
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

	if(action->flags & SA_IMAP_MASKED) {
		struct ino_bucket *bucket = (struct ino_bucket *)action->mask;
		unsigned int *imap = bucket->imap;

		if(imap != NULL) {
			ivindex = bucket->ino;
			ivector_to_mask[ivindex] = 0;
		}
		else
			printk("free_irq: WHeee, SYSIO_MASKED yet no imap reg.\n");
	}

	kfree(action);
	if(ivindex != -1)
		disable_irq(ivindex);

	restore_flags(flags);
}

/* Only uniprocessor needs this IRQ locking depth, on SMP it lives in the per-cpu
 * structure for cache reasons.
 */
#ifndef __SMP__
unsigned int local_irq_count;
#endif

#ifndef __SMP__
int __sparc64_bh_counter = 0;

#define irq_enter(cpu, irq)	(local_irq_count++)
#define irq_exit(cpu, irq)	(local_irq_count--)

#else

atomic_t __sparc64_bh_counter = ATOMIC_INIT(0);

/* Who has global_irq_lock. */
unsigned char global_irq_holder = NO_PROC_ID;

/* This protects IRQ's. */
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;

/* This protects BH software state (masks, things like that). */
spinlock_t global_bh_lock = SPIN_LOCK_UNLOCKED;

/* Global IRQ locking depth. */
atomic_t global_irq_count = ATOMIC_INIT(0);

static unsigned long previous_irqholder;

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("wait_on_irq CPU#%d stuck at %08lx, waiting for %08lx (local=%d, global=%d)\n", cpu, where, previous_irqholder, local_count, atomic_read(&global_irq_count)); stuck = INIT_STUCK; }

static inline void wait_on_irq(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;
	int local_count = local_irq_count;

	while(local_count != atomic_read(&global_irq_count)) {
		atomic_sub(local_count, &global_irq_count);
		spin_unlock(&global_irq_lock);
		for(;;) {
			STUCK;
			membar("#StoreLoad | #LoadLoad");
			if (atomic_read(&global_irq_count))
				continue;
			if (*((volatile unsigned char *)&global_irq_lock))
				continue;
			membar("#LoadLoad | #LoadStore");
			if (spin_trylock(&global_irq_lock))
				break;
		}
		atomic_add(local_count, &global_irq_count);
	}
}

#undef INIT_STUCK
#define INIT_STUCK 10000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("get_irqlock stuck at %08lx, waiting for %08lx\n", where, previous_irqholder); stuck = INIT_STUCK;}

static inline void get_irqlock(int cpu, unsigned long where)
{
	int stuck = INIT_STUCK;

	if (!spin_trylock(&global_irq_lock)) {
		membar("#StoreLoad | #LoadLoad");
		if ((unsigned char) cpu == global_irq_holder)
			return;
		do {
			do {
				STUCK;
				membar("#LoadLoad");
			} while(*((volatile unsigned char *)&global_irq_lock));
		} while (!spin_trylock(&global_irq_lock));
	}
	wait_on_irq(cpu, where);
	global_irq_holder = cpu;
	previous_irqholder = where;
}

void __global_cli(void)
{
	int cpu = smp_processor_id();
	unsigned long where;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (where));
	__cli();
	get_irqlock(cpu, where);
}

void __global_sti(void)
{
	release_irqlock(smp_processor_id());
	__sti();
}

void __global_restore_flags(unsigned long flags)
{
	if (flags & 1) {
		__global_cli();
	} else {
		if (global_irq_holder == (unsigned char) smp_processor_id()) {
			global_irq_holder = NO_PROC_ID;
			spin_unlock(&global_irq_lock);
		}
		if (!(flags & 2))
			__sti();
	}
}

#undef INIT_STUCK
#define INIT_STUCK 200000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("irq_enter stuck (irq=%d, cpu=%d, global=%d)\n",irq,cpu,global_irq_holder); stuck = INIT_STUCK;}

void irq_enter(int cpu, int irq)
{
	int stuck = INIT_STUCK;

	hardirq_enter(cpu);
	while (*((volatile unsigned char *)&global_irq_lock)) {
		if ((unsigned char) cpu == global_irq_holder)
			printk("irq_enter: Frosted Lucky Charms, "
			       "they're magically delicious!\n");
		STUCK;
		membar("#LoadLoad");
	}
}

void irq_exit(int cpu, int irq)
{
	hardirq_exit(cpu);
	release_irqlock(cpu);
}

void synchronize_irq(void)
{
	int local_count = local_irq_count;
	unsigned long flags;

	if (local_count != atomic_read(&global_irq_count)) {
		save_and_cli(flags);
		restore_flags(flags);
	}
}

#endif /* __SMP__ */

void report_spurious_ivec(struct pt_regs *regs)
{
	extern unsigned long ivec_spurious_cookie;
	static int times = 0;

	printk("IVEC: Spurious interrupt vector (%016lx) received at (%016lx)\n",
	       ivec_spurious_cookie, regs->tpc);
	if(times++ > 1)
		prom_halt();
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

	clear_softint(1 << irq);

	irq_enter(cpu, irq);
	action = *(irq + irq_action);
	kstat.interrupts[irq]++;
	if(!action) {
		unexpected_irq(irq, 0, regs);
	} else {
		do {
			struct ino_bucket *bucket = NULL;
			unsigned int ino = 0;

			if(action->flags & SA_IMAP_MASKED) {
				bucket = (struct ino_bucket *)action->mask;

				ino = bucket->ino;
				if(!(ivector_to_mask[ino] & 0x80000000))
					continue;
			}

			action->handler(irq, action->dev_id, regs);
			if(bucket) {
				ivector_to_mask[ino] &= ~(0x80000000);
				*(bucket->iclr) = SYSIO_ICLR_IDLE;
			}
		} while((action = action->next) != NULL);
	}
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
	if(action->flags & SA_IMAP_MASKED)
		*((unsigned int *)action->mask) = SYSIO_ICLR_IDLE;
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
		     unsigned long irqflags, const char *name)
{
	struct irqaction *action;
	unsigned long flags;
	unsigned int cpu_irq, *imap, *iclr;
	int ivindex = -1;

	/* XXX This really is not the way to do it, the "right way"
	 * XXX is to have drivers set SA_SBUS or something like that
	 * XXX in irqflags and we base our decision here on whether
	 * XXX that flag bit is set or not.
	 *
	 * In this case nobody can have a fast interrupt at the level
	 * where TICK interrupts live.
	 */
	if(irq == 14)
		return -EINVAL;
	cpu_irq = sysio_ino_to_pil[irq];

	if(!handler)
		return -EINVAL;
	imap = sysio_irq_to_imap(irq);
	action = *(cpu_irq + irq_action);
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
			       "using kmalloc\n", irq, name);
	}
	if(action == NULL)
		action = (struct irqaction *)kmalloc(sizeof(struct irqaction),
						     GFP_KERNEL);
	if(!action) {
		restore_flags(flags);
		return -ENOMEM;
	}
	install_fast_irq(cpu_irq, handler);

	if(imap) {
		ivindex = (*imap & (SYSIO_IMAP_IGN | SYSIO_IMAP_INO));
		ivector_to_mask[ivindex] = (1 << cpu_irq);
		iclr = sysio_imap_to_iclr(imap);
		action->mask = (unsigned long) iclr;
		irqflags |= SA_IMAP_MASKED;
		add_ino_hash(ivindex, imap, iclr, irqflags);
	} else
		action->mask = 0;

	action->handler = handler;
	action->flags = irqflags;
	action->dev_id = NULL;
	action->name = name;
	action->next = NULL;

	*(cpu_irq + irq_action) = action;

	if(ivindex != -1)
		enable_irq(ivindex);

	restore_flags(flags);
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

struct sun5_timer *linux_timers = NULL;

/* This is gets the master level10 timer going. */
void init_timers(void (*cfunc)(int, void *, struct pt_regs *))
{
	struct linux_prom64_registers pregs[3];
	struct devid_cookie dcookie;
	unsigned int *imap, *iclr;
	u32 pirqs[2];
	int node, err;

	node = prom_finddevice("/counter-timer");
	if(node == 0 || node == -1) {
		prom_printf("init_timers: Cannot find counter-timer PROM node.\n");
		prom_halt();
	}
	err = prom_getproperty(node, "reg", (char *)&pregs[0], sizeof(pregs));
	if(err == -1) {
		prom_printf("init_timers: Cannot obtain 'reg' for counter-timer.\n");
		prom_halt();
	}
	err = prom_getproperty(node, "interrupts", (char *)&pirqs[0], sizeof(pirqs));
	if(err == -1) {
		prom_printf("init_timers: Cannot obtain 'interrupts' "
			    "for counter-timer.\n");
		prom_halt();
	}
	linux_timers = (struct sun5_timer *) __va(pregs[0].phys_addr);
	iclr = (((unsigned int *)__va(pregs[1].phys_addr))+1);
	imap = (((unsigned int *)__va(pregs[2].phys_addr))+1);

	/* Shut it up first. */
	linux_timers->limit0 = 0;

	/* Register IRQ handler. */
	dcookie.real_dev_id = NULL;
	dcookie.imap = imap;
	dcookie.iclr = iclr;
	dcookie.pil = 10;
	dcookie.bus_cookie = NULL;

	err = request_irq(pirqs[0], cfunc,
			  (SA_DCOOKIE | SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", &dcookie);

	if(err) {
		prom_printf("Serious problem, cannot register timer interrupt\n");
		prom_halt();
	} else {
		unsigned long flags;

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
"		: /* no outputs */
		: /* no inputs */
		: "g1", "g2");

		linux_timers->limit0 =
			(SUN5_LIMIT_ENABLE | SUN5_LIMIT_ZRESTART | SUN5_LIMIT_TOZERO |
			 (SUN5_HZ_TO_LIMIT(HZ) & SUN5_LIMIT_CMASK));

		restore_flags(flags);
	}

	sti();
}

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

#ifdef __SMP__
/* Called from smp_commence, when we know how many cpus are in the system
 * and can have device IRQ's directed at them.
 */
void distribute_irqs(void)
{
	unsigned long flags;
	int cpu, level;

	printk("SMP: redistributing interrupts...\n");
	save_and_cli(flags);
	cpu = 0;
	for(level = 0; level < NR_IRQS; level++) {
		struct irqaction *p = irq_action[level];

		while(p) {
			if(p->flags & SA_IMAP_MASKED) {
				struct ino_bucket *bucket = (struct ino_bucket *)p->mask;
				unsigned int *imap = bucket->imap;
				unsigned int val;
				unsigned long tid = linux_cpus[cpu].mid << 9;

				val = *imap;
				*imap = SYSIO_IMAP_VALID | (tid & SYSIO_IMAP_TID);

				printk("SMP: Redirecting IGN[%x] INO[%x] "
				       "to cpu %d [%s]\n",
				       (val & SYSIO_IMAP_IGN) >> 6,
				       (val & SYSIO_IMAP_INO), cpu,
				       p->name);

				cpu += 1;
				while(!(cpu_present_map & (1UL << cpu))) {
					cpu += 1;
					if(cpu >= smp_num_cpus)
						cpu = 0;
				}
			}
			p = p->next;
		}
	}
	restore_flags(flags);
	irqs_have_been_distributed = 1;
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
