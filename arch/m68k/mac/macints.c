/*
 *	Macintosh interrupts
 *
 * General design:
 * In contrary to the Amiga and Atari platforms, the Mac hardware seems to 
 * exclusively use the autovector interrupts (the 'generic level0-level7' 
 * interrupts with exception vectors 0x19-0x1f). The following interrupt levels
 * are used:
 *	1	- VIA1
 *		  - slot 0: one second interrupt
 *		  - slot 1: VBlank
 *		  - slot 2: ADB data ready (SR full)
 *		  - slot 3: ADB data  (CB2)
 *		  - slot 4: ADB clock (CB1)
 *		  - slot 5: timer 2
 *		  - slot 6: timer 1
 *		  - slot 7: status of IRQ; signals 'any enabled int.'
 *
 *	2	- VIA2, RBV or OSS
 *		  - slot 0: SCSI DRQ
 *		  - slot 1: NUBUS IRQ
 *		  - slot 3: SCSI IRQ
 *
 *	4	- SCC
 *		  - subdivided into Channel B and Channel A interrupts 
 *
 *	6	- Off switch (??)
 *
 *	7	- Debug output
 *
 * AV Macs only, handled by PSC:
 *
 *	3	- MACE ethernet IRQ (DMA complete on level 4)
 *
 *	5	- DSP ?? 
 *
 * Using the autovector irq numbers for Linux/m68k hardware interrupts without
 * the IRQ_MACHSPEC bit set would interfere with the general m68k interrupt 
 * handling in kernel versions 2.0.x, so the following strategy is used:
 *
 * - mac_init_IRQ installs the low-level entry points for the via1 and via2 
 *   exception vectors and the corresponding handlers (C functions); these 
 *   entry points just add the machspec bit and call the handlers proper.
 *   (in principle, the C functions can be installed as the exception vectors 
 *   directly, as they are hardcoded anyway; that's the current method). 
 *
 * - via[12]_irq determine what interrupt sources have triggered the interrupt,
 *   and call the corresponding device interrupt handlers. 
 *   (currently, via1_irq and via2_irq just call via_irq, passing the via base
 *   address. RBV interrupts are handled by (you guessed it) rbv_irq).
 *   Some interrupt functions want to have the interrupt number passed, so 
 *   via_irq and rbv_irq need to generate the 'fake' numbers from scratch.
 *
 * - for the request/free/enable/disable business, interrupt sources are 
 *   numbered internally (suggestion: keep irq 0-7 unused :-). One bit in the 
 *   irq number specifies the via# to use, i.e. via1 interrupts are 8-16, 
 *   via2 interrupts 17-32, rbv interrupts ...
 *   The device interrupt table and the irq_enable bitmap is maintained by 
 *   the machspec interrupt code; all device drivers should only use these 
 *   functions ! 
 *
 * - For future porting to version 2.1 (and removing of the machspec bit) it 
 *   should be sufficient to use the same numbers (everything > 7 is assumed 
 *   to be machspec, according to Jes!).
 *
 *   TODO:
 * - integrate Nubus interrupts in request/free_irq
 *
 * - 
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h> /* for intr_count */
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machw.h>
#include <asm/macintosh.h>
#include "via6522.h"

#include <asm/macints.h>

/*
 * Interrupt handler and parameter types
 */
struct irqhandler {
	void	(*handler)(int, void *, struct pt_regs *);
	void	*dev_id;
};

struct irqparam {
	unsigned long	flags;
	const char	*devname;
};

struct irqflags {
        unsigned int disabled;
	unsigned int pending;
};

/*
 * Array with irq's and their parameter data. 
 */
static struct irqhandler  via1_handler[8];
static struct irqhandler  via2_handler[8];
static struct irqhandler   rbv_handler[8];
static struct irqhandler  psc3_handler[8];
static struct irqhandler   scc_handler[8];
static struct irqhandler  psc5_handler[8];
static struct irqhandler  psc6_handler[8];
static struct irqhandler nubus_handler[8];

static struct irqhandler *handler_table[8];

/*
 * This array hold the rest of parameters of int handlers: type
 * (slow,fast,prio) and the name of the handler. These values are only
 * accessed from C
 */
static struct irqparam  via1_param[8];
static struct irqparam  via2_param[8];
static struct irqparam   rbv_param[8];
static struct irqparam  psc3_param[8];
static struct irqparam   scc_param[8];
static struct irqparam  psc5_param[8];
static struct irqparam  psc6_param[8];
static struct irqparam nubus_param[8];

static struct irqparam *param_table[8];

/*
 * This array holds the 'disabled' and 'pending' software flags maintained
 * by mac_{enable,disable}_irq and the generic via_irq function.
 */

static struct irqflags irq_flags[8];

/*
 * This array holds the pointers to the various VIA or other interrupt 
 * controllers, indexed by interrupt level
 */

static volatile unsigned char *via_table[8];

/*
 * Arrays with irq statistics
 */
static unsigned long via1_irqs[8];
static unsigned long via2_irqs[8];
static unsigned long rbv_irqs[8];
static unsigned long psc3_irqs[8];
static unsigned long scc_irqs[8];
static unsigned long psc5_irqs[8];
static unsigned long psc6_irqs[8];
static unsigned long nubus_irqs[8];

static unsigned long *mac_irqs[8];

/*
 * Some special nutcases ...
 */

static unsigned long mac_ide_irqs = 0;
static unsigned long nubus_stuck_events = 0;

/*
 * VIA/RBV/OSS/PSC register base pointers
 */

volatile unsigned char *via2_regp=(volatile unsigned char *)VIA2_BAS;
volatile unsigned char *rbv_regp=(volatile unsigned char *)VIA2_BAS_IIci;
volatile unsigned char *oss_regp=(volatile unsigned char *)OSS_BAS;
volatile unsigned char *psc_regp=(volatile unsigned char *)PSC_BAS;

/*
 * Flags to control via2 / rbv behaviour
 */ 

static int via2_is_rbv = 0;
static int via2_is_oss = 0;
static int rbv_clear = 0;

/* fake VIA2 to OSS bit mapping */
static int oss_map[8] = {2, 7, 0, 1, 3, 4, 5};

void oss_irq(int irq, void *dev_id, struct pt_regs *regs);
static void oss_do_nubus(int irq, void *dev_id, struct pt_regs *regs);

/* PSC ints */
void psc_irq(int irq, void *dev_id, struct pt_regs *regs);

/*
 * PSC hooks
 */

extern void psc_init(void);

/*
 * console_loglevel determines NMI handler function
 */

extern int console_loglevel;

/*
 * ADB test hooks
 */
extern int in_keybinit;
void adb_queue_poll(void);

/* Defined in entry.S; only increments 'num_spurious' */
asmlinkage void bad_interrupt(void);

void nubus_wtf(int slot, void *via, struct pt_regs *regs);

void mac_nmi_handler(int irq, void *dev_id, struct pt_regs *regs);
void mac_debug_handler(int irq, void *dev_id, struct pt_regs *regs);

static void via_do_nubus(int slot, void *via, struct pt_regs *regs);

/* #define DEBUG_MACINTS */

#define DEBUG_SPURIOUS
#define DEBUG_NUBUS_SPURIOUS
#define DEBUG_NUBUS_INT

/* #define DEBUG_VIA */
#define DEBUG_VIA_NUBUS

void mac_init_IRQ(void)
{
        int i;

#ifdef DEBUG_MACINTS
	printk("Mac interrupt stuff initializing ...\n");
#endif

	via2_regp = (unsigned char *)VIA2_BAS;
	rbv_regp  = (unsigned char *)VIA2_BAS_IIci;

        /* initialize the hardwired (primary, autovector) IRQs */

	/* level 1 IRQ: VIA1, always present */
	sys_request_irq(1, via1_irq, IRQ_FLG_LOCK, "via1", via1_irq);

	/* via2 or rbv?? */
	if (macintosh_config->via_type == MAC_VIA_IIci) {
		/*
		 * A word of caution: the definitions here only affect interrupt
		 * handling, see via6522.c for yet another file to change
		 * base addresses and RBV flags
		 */

		/* yes, this is messy - the IIfx deserves a class of his own */
		if (macintosh_config->ident == MAC_MODEL_IIFX) {
			/* no real VIA2, the OSS seems _very_ different */	
			via2_is_oss = 1;
			/* IIfx has OSS, at a different base address than RBV */
			rbv_regp = (unsigned char *) OSS_BAS;
			sys_request_irq(2, oss_irq, IRQ_FLG_LOCK, "oss", oss_irq);
		} else {
			/* VIA2 is part of the RBV: different base, other offsets */
			via2_is_rbv = 1;

			/* LC III weirdness: IFR seems to behave like VIA2 */
			/* FIXME: maybe also for LC II ?? */ 
			if (macintosh_config->ident == MAC_MODEL_LCIII) {
				rbv_clear = 0x0;
			} else {
				rbv_clear = 0x80;
			}
			/* level 2 IRQ: RBV/OSS; we only care about RBV for now */
			sys_request_irq(2, rbv_irq, IRQ_FLG_LOCK, "rbv", rbv_irq);
		}
	} else
		/* level 2 IRQ: VIA2 */
		sys_request_irq(2, via2_irq, IRQ_FLG_LOCK, "via2", via2_irq);

	/* 
	 * level 4 IRQ: SCC - use 'master' interrupt routine that calls the 
	 *		registered channel-specific interrupts in turn.
	 *		Currently, one interrupt per channel is used, solely
	 *		to pass the correct async_info as parameter!
	 */

	sys_request_irq(4, mac_debug_handler, IRQ_FLG_STD, "INT4", mac_debug_handler);

	/* level 6 */
	sys_request_irq(6, mac_bang, IRQ_FLG_LOCK, "offswitch", mac_bang);

	/* level 7 (or NMI) : debug stuff */
	sys_request_irq(7, mac_nmi_handler, IRQ_FLG_STD, "NMI", mac_nmi_handler);

	/* initialize the handler tables for VIAs */
	for (i = 0; i < 8; i++) {
		via1_handler[i].handler = mac_default_handler;
		via1_handler[i].dev_id  = NULL;
		via1_param[i].flags     = IRQ_FLG_STD;
		via1_param[i].devname   = NULL;

		via2_handler[i].handler = mac_default_handler;
		via2_handler[i].dev_id  = NULL;
		via2_param[i].flags     = IRQ_FLG_STD;
		via2_param[i].devname   = NULL;

		rbv_handler[i].handler = mac_default_handler;
		rbv_handler[i].dev_id  = NULL;
		rbv_param[i].flags     = IRQ_FLG_STD;
		rbv_param[i].devname   = NULL;

		scc_handler[i].handler = mac_default_handler;
		scc_handler[i].dev_id  = NULL;
		scc_param[i].flags     = IRQ_FLG_STD;
		scc_param[i].devname   = NULL;

		/* NUBUS interrupts routed through VIA2 slot 2 - special */
		nubus_handler[i].handler = nubus_wtf;
		nubus_handler[i].dev_id  = NULL;
		nubus_param[i].flags     = IRQ_FLG_STD;
		nubus_param[i].devname   = NULL;

	}

	/* initialize the handler tables (level 1 -> via_handler[0] !!!) */
	via_table[0]     =  via1_regp;
	handler_table[0] =  &via1_handler[0];
	param_table[0]   =  &via1_param[0];
	mac_irqs[0]	 =  &via1_irqs[0];

	if (via2_is_rbv || via2_is_oss) {
		via_table[1]     =  rbv_regp;
		handler_table[1] =  &rbv_handler[0];
		param_table[1]   =  &rbv_param[0];
		mac_irqs[1]	 =  &rbv_irqs[0];
	} else {
		via_table[1]     =  via2_regp;
		handler_table[1] =  &via2_handler[0];
		param_table[1]   =  &via2_param[0];
		mac_irqs[1]	 =  &via2_irqs[0];
	}
	via_table[2]     =  NULL;
	via_table[3]     =  NULL;
	
	handler_table[2] =  &rbv_handler[0];
	handler_table[3] =  &scc_handler[0];
	handler_table[4] =  NULL;
	handler_table[5] =  NULL;
	handler_table[6] =  NULL;
	handler_table[7] =  &nubus_handler[0];

	param_table[2]   =  &rbv_param[0];
	param_table[3]   =  &scc_param[0];
	param_table[7]   =  &nubus_param[0];

	mac_irqs[2]	 =  &rbv_irqs[0];
	mac_irqs[3]	 =  &scc_irqs[0];
	mac_irqs[7]	 =  &nubus_irqs[0];

	/*
	 * Nubus Macs: turn off the Nubus dispatch interrupt for now
	 */

	mac_turnoff_irq(IRQ_MAC_NUBUS);

	/*
	 *	AV Macs: shutup the PSC ints
	 */
	if (macintosh_config->ident == MAC_MODEL_C660
	 || macintosh_config->ident == MAC_MODEL_Q840) 
	{
		psc_init();

		handler_table[2] = &psc3_handler[0];
		/* handler_table[3] = &psc4_handler[0]; */
		handler_table[4] = &psc5_handler[0];
		handler_table[5] = &psc6_handler[0];

		param_table[2]   = &psc3_param[0];
		/* param_table[3]   = &psc4_param[0]; */
		param_table[4]   = &psc5_param[0];
		param_table[5]   = &psc6_param[0];

		mac_irqs[2]	 = &psc3_irqs[0]; 
		/* mac_irqs[3]	 = &psc4_irqs[0]; */
		mac_irqs[4]	 = &psc5_irqs[0];
		mac_irqs[5]	 = &psc6_irqs[0];

		sys_request_irq(3, psc_irq, IRQ_FLG_STD, "PSC3", psc_irq);
		sys_request_irq(4, psc_irq, IRQ_FLG_STD, "PSC4", psc_irq);
		sys_request_irq(5, psc_irq, IRQ_FLG_STD, "PSC5", psc_irq);
		sys_request_irq(6, psc_irq, IRQ_FLG_STD, "PSC6", psc_irq);
	}

#ifdef DEBUG_MACINTS
	printk("Mac interrupt init done!\n");
#endif
}

/*
 *	We have no machine specific interrupts on a macintoy
 *      Yet, we need to register/unregister interrupts ... :-)
 *      Currently unimplemented: Test for valid irq number, chained irqs,
 *      Nubus interrupts (use nubus_request_irq!).
 */
 
int mac_request_irq (unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
                              unsigned long flags, const char *devname, void *dev_id)
{
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx =  (irq & IRQ_IDX_MASK);
	struct irqhandler *via_handler;
	struct irqparam   *via_param;
	volatile unsigned char *via;

#ifdef DEBUG_MACINTS
	printk ("%s: IRQ %d on VIA%d[%d] requested from %s\n",
	        __FUNCTION__, irq, srcidx+1, irqidx, devname);
#endif

	if (flags < IRQ_TYPE_SLOW || flags > IRQ_TYPE_PRIO) {
		printk ("%s: Bad irq type %ld requested from %s\n",
		        __FUNCTION__, flags, devname);
		return -EINVAL;
	}

	/* figure out what VIA is handling this irq */
        if (irq < IRQ_IDX(IRQ_VIA1_1) || irq >= IRQ_IDX(IRQ_NUBUS_1)) {
		/* non-via irqs unimplemented */
		printk ("%s: Bad irq source %d on VIA %d requested from %s\n",
		        __FUNCTION__, irq, srcidx, devname);
		return -EINVAL;
	} 

	/* figure out if SCC pseudo-irq (redundant ??) */
        if (irq >= IRQ_IDX(IRQ_SCC) && irq < IRQ_IDX(IRQ_PSC5_0)) {
		/* set specific SCC handler */
		scc_handler[irqidx].handler = handler;
		scc_handler[irqidx].dev_id  = dev_id;
		scc_param[irqidx].flags     = flags;
		scc_param[irqidx].devname   = devname;
		/* and done! */
		return 0;
	} 

	/* 
	 * code below: only for VIA irqs currently 
	 * add similar hack for Nubus pseudo-irq here - hide nubus_request_irq
	 */
	via         = (volatile unsigned char *) via_table[srcidx];
	if (!via) 
		return -EINVAL;

	via_handler = handler_table[srcidx];
	via_param   = param_table[srcidx];

	/* check for conflicts or possible replacement */

	/* set the handler - no chained irqs yet !! */
	via_handler[irqidx].handler = handler;
	via_handler[irqidx].dev_id  = dev_id;
	via_param[irqidx].flags     = flags;
	via_param[irqidx].devname   = devname;

	/* and turn it on ... careful, that's VIA only ... */
	if (srcidx == SRC_VIA2 && via2_is_rbv)
		via_write(via, rIER, via_read(via, rIER)|0x80|(1<<(irqidx)));
	else if (srcidx == SRC_VIA2 && via2_is_oss)
		via_write(oss_regp, oss_map[irqidx]+8, 2);
	else
		via_write(via, vIER, via_read(via, vIER)|0x80|(1<<(irqidx)));


	if (irq == IRQ_IDX(IRQ_MAC_SCSI)) {
		/*
		 * Set vPCR for SCSI interrupts. (what about RBV here?)
		 * 980429 MS: RBV is ok, OSS seems to be different
		 */
		if (!via2_is_oss)
			if (macintosh_config->scsi_type == MAC_SCSI_OLD) {
				/* CB2 (IRQ) indep. interrupt input, positive edge */
				/* CA2 (DRQ) indep. interrupt input, positive edge */
				via_write(via, vPCR, 0x66);
			} else {
				/* CB2 (IRQ) indep. interrupt input, negative edge */
				/* CA2 (DRQ) indep. interrupt input, negative edge */
				via_write(via, vPCR, 0x22);
			}
#if 0
		else
			/* CB2 (IRQ) indep. interrupt input, negative edge */
			/* CA2 (DRQ) indep. interrupt input, negative edge */
			via_write(via, vPCR, 0x22);
#endif
	}

	return 0;
}
                            
void mac_free_irq (unsigned int irq, void *dev_id)
{
	unsigned long flags;
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx =  (irq & IRQ_IDX_MASK);
	struct irqhandler *via_handler;
	struct irqparam   *via_param;
	volatile unsigned char *via;

#ifdef DEBUG_MACINTS
	printk ("%s: IRQ %d on VIA%d[%d] freed\n",
	        __FUNCTION__, irq, srcidx+1, irqidx);
#endif

	/* figure out what VIA is handling this irq */
	if (irq < IRQ_IDX(IRQ_VIA1_1) || irq >= IRQ_IDX(IRQ_NUBUS_1)) {
		/* non-via irqs unimplemented */
		return;
	} 

	save_flags(flags);
	cli();

	/* figure out if SCC pseudo-irq */
        if (irq >= IRQ_IDX(IRQ_SCC) && irq < IRQ_IDX(IRQ_PSC5_0)) {
		/* clear specific SCC handler */
		scc_handler[irqidx].handler = mac_default_handler;
		scc_handler[irqidx].dev_id  = NULL;
		scc_param[irqidx].flags     = IRQ_FLG_STD;
		scc_param[irqidx].devname   = NULL;
		/* and done! */
		restore_flags(flags);
		return;
	} 

	via         = (volatile unsigned char *) via_table[srcidx];
	via_handler = handler_table[srcidx];
	via_param   = param_table[srcidx];

	if ( !via || (via_handler[irqidx].dev_id != dev_id) ) {
		restore_flags(flags);
		goto not_found;
	}

	/* clear the handler - no chained irqs yet !! */
	via_handler[irqidx].handler = mac_default_handler;
	via_handler[irqidx].dev_id  = NULL;
	via_param[irqidx].flags     = IRQ_FLG_STD;
	via_param[irqidx].devname   = NULL;

	/* and turn it off */
	if (srcidx == SRC_VIA2 && via2_is_rbv)
		via_write(via, rIER, (via_read(via, rIER)&(1<<irqidx)));
	else if (srcidx == SRC_VIA2 && via2_is_oss)
		via_write(oss_regp, oss_map[irqidx]+8, 0);
	else
		via_write(via, vIER, (via_read(via, vIER)&(1<<irqidx)));

	restore_flags(flags);
	return;

not_found:
	printk("%s: tried to remove invalid irq\n", __FUNCTION__);
	return;

}

/*
 * {en,dis}able_irq have the usual semantics of temporary blocking the
 * interrupt, but not loosing requests that happen between disabling and
 * enabling. On Atari, this is done with the MFP mask registers.
 *
 * On the Mac, this isn't possible: there is no VIA mask register. 
 * Needs to be implemented in software, setting 'mask' bits in a separate
 * struct for each interrupt controller. These mask bits need to be checked
 * by the VIA interrupt routine which should ignore requests for masked IRQs 
 * (after possibly ack'ing them).
 *
 * On second thought: some of the IRQ sources _can_ be turned off via bits
 * in the VIA output registers. Need to check this ...
 *
 * TBI: According to the VIA docs, clearing a bit in the IER has the effect of 
 * blocking generation of the interrupt, but the internal interrupt condition 
 * is preserved. So the IER might be used as mask register here, and turnon_irq
 * would need to clear the interrupt bit in the IFR to prevent getting an 
 * interrupt at all.
 *
 * Implementation note: the irq no's here are the _machspec_ irqs, hence the 
 * hack with srcidx to figure out which VIA/RBV handles the interrupt. 
 * That's fundamentally different when it comes to the interrupt handlers 
 * proper: these get the interrupt level no. as argument, all information about
 * which source triggered the int. is buried in the VIA IFR ... The int. level 
 * points us to the proper handler, so we could do a sanity check there ...
 */

void mac_enable_irq (unsigned int irq)
{
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx = (irq & IRQ_IDX_MASK);

	irq_flags[srcidx].disabled &= ~(1<<irqidx);
	/*
	 * Call handler here if irq_flags[srcidx].pending & 1<<irqidx ?? 
	 * The structure of via_irq prevents this, sort of: it warns if
	 * no true events are pending. Maybe that's being changed ...
	 * Other problem: is it always possible to call an interrupt handler, 
	 * or should that depend on the current interrupt level?
	 */
}

void mac_disable_irq (unsigned int irq)
{
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx = (irq & IRQ_IDX_MASK);

	irq_flags[srcidx].disabled |= (1<<irqidx);
}

/*
 * In opposite to {en,dis}able_irq, requests between turn{off,on}_irq are not
 * "stored". This is done with the VIA interrupt enable register on VIAs.
 *
 * Note: Using these functions on non-VIA/OSS/PSC ints will panic, or at least 
 * have undesired side effects.
 */

void mac_turnon_irq( unsigned int irq )
{
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx = (irq & IRQ_IDX_MASK);
	volatile unsigned char *via;

	via         = (volatile unsigned char *) via_table[srcidx];
	if (!via) 
		return;

	if (srcidx == SRC_VIA2 && via2_is_rbv)		/* RBV as VIA2 */
	        via_write(via, rIER, via_read(via, rIER)|0x80|(1<<(irqidx)));
	else if (srcidx == SRC_VIA2 && via2_is_oss)	/* OSS */
		via_write(oss_regp, oss_map[irqidx]+8, 2);
	else if (srcidx > SRC_VIA2)			/* hope AVs have VIA2 */
	        via_write(via, (0x104 + 0x10*srcidx), 
	        	via_read(via, (0x104 + 0x10*srcidx))|0x80|(1<<(irqidx)));
	else						/* VIA1+2 */
	        via_write(via, vIER, via_read(via, vIER)|0x80|(1<<(irqidx)));

}

void mac_turnoff_irq( unsigned int irq )
{
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx = (irq & IRQ_IDX_MASK);
	volatile unsigned char *via;

	via         = (volatile unsigned char *) via_table[srcidx];
	if (!via) 
		return;

	if (srcidx == SRC_VIA2 && via2_is_rbv)		/* RBV as VIA2 */
		via_write(via, rIER, (via_read(via, rIER)&(1<<irqidx)));
	else if (srcidx == SRC_VIA2 && via2_is_oss)	/* OSS */
		via_write(oss_regp, oss_map[irqidx]+8, 0);
	/*
	 *	VIA2 is fixed. The stuff above VIA2 is for later
	 *	macintoshes only.
	 */
	else if (srcidx > SRC_VIA2)
	        via_write(via, (0x104 + 0x10*srcidx), 
	        	via_read(via, (0x104 + 0x10*srcidx))|(1<<(irqidx)));
	else						/* VIA1+2 */
		via_write(via, vIER, (via_read(via, vIER)&(1<<irqidx)));
}

/*
 * These functions currently only handle the software-maintained irq pending
 * list for disabled irqs - manipulating the actual irq flags in the via would
 * require clearing single bits in the via, such as (probably) 
 * via_write(via, vIFR, (via_read(via, vIFR)&(1<<irqidx))); - don't know if 
 * this has side-effects ...
 */

void mac_clear_pending_irq( unsigned int irq )
{
	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx = (irq & IRQ_IDX_MASK);

	irq_flags[srcidx].pending &= ~(1<<irqidx);
}

int  mac_irq_pending( unsigned int irq )
{
	int pending = 0;
	volatile unsigned char *via;

	int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
	int irqidx = (irq & IRQ_IDX_MASK);

	pending = irq_flags[srcidx].pending & (1<<irqidx);

	via         = (volatile unsigned char *) via_table[srcidx];
	if (!via) 
		return (pending);

	if (srcidx == SRC_VIA2 && via2_is_rbv)
		pending |= via_read(via, rIFR)&(1<<irqidx);
	else if (srcidx == SRC_VIA2 && via2_is_oss)
		pending |= via_read(via, oIFR)&0x03&(1<<oss_map[irqidx]);
	else if (srcidx > SRC_VIA2)
	        pending |= via_read(via, (0x100 + 0x10*srcidx))&(1<<irqidx);
	else
		pending |= via_read(via, vIFR)&(1<<irqidx);

	return (pending);
}

/*
 * for /proc/interrupts: log interrupt stats broken down by 
 * autovector int first, then by actual interrupt source.
 */

int mac_get_irq_list (char *buf)
{
	int i, len = 0;
	int srcidx, irqidx;

	for (i = VIA1_SOURCE_BASE; i < NUM_MAC_SOURCES+8; ++i) {
	        /* XXX fixme: IRQ_SRC_MASK should cover VIA1 - Nubus */
		srcidx = ((i & IRQ_SRC_MASK)>>3) - 1;
		irqidx = (i & IRQ_IDX_MASK);

		/*
		 * Not present: skip
		 */

		if (mac_irqs[srcidx] == NULL)
			continue;

		/* 
		 * never used by VIAs, unused by others so far, counts 
		 * the magic 'nothing pending' cases ...
		 */
		if (irqidx == 7 && mac_irqs[srcidx][irqidx]) {
			len += sprintf(buf+len, "Level %01d: %10lu (spurious) \n",
				       srcidx, 
				       mac_irqs[srcidx][irqidx]);
			continue;
		}

		/*
		 * Nothing registered for this IPL: skip
		 */

		if (handler_table[srcidx] == NULL)
			continue;

		/*
		 * No handler installed: skip
		 */ 

		if (handler_table[srcidx][irqidx].handler == mac_default_handler ||
		    handler_table[srcidx][irqidx].handler == nubus_wtf)
			continue;


		if (i < VIA2_SOURCE_BASE)
			len += sprintf(buf+len, "via1  %01d: %10lu ",
				       irqidx,
				       mac_irqs[srcidx][irqidx]);
		else if (i < RBV_SOURCE_BASE)
			len += sprintf(buf+len, "via2  %01d: %10lu ",
				       irqidx,
				       mac_irqs[srcidx][irqidx]);
		else if (i < MAC_SCC_SOURCE_BASE)
			len += sprintf(buf+len, "rbv   %01d: %10lu ",
				       irqidx,
				       mac_irqs[srcidx][irqidx]);
		else if (i < NUBUS_SOURCE_BASE)
			len += sprintf(buf+len, "scc   %01d: %10lu ",
				       irqidx,
				       mac_irqs[srcidx][irqidx]);
		else /* Nubus */
			len += sprintf(buf+len, "nubus %01d: %10lu ",
				       irqidx,
				       mac_irqs[srcidx][irqidx]);

			len += sprintf(buf+len, "%s\n", 
				       param_table[srcidx][irqidx].devname);

	}
	if (num_spurious)
		len += sprintf(buf+len, "spurio.: %10u\n", num_spurious);

	/* 
	 * XXX Fixme: Nubus sources are never logged above ...
	 */

	len += sprintf(buf+len, "Nubus interrupts:\n");

	for (i = 0; i < 7; i++) {
		if (nubus_handler[i].handler == nubus_wtf)
			continue;
		len += sprintf(buf+len, "nubus %01X: %10lu ",
			       i+9, 
			       nubus_irqs[i]);
		len += sprintf(buf+len, "%s\n", 
			       nubus_param[i].devname);

	}
	len += sprintf(buf+len, "nubus spurious ints: %10lu\n",
		       nubus_irqs[7]);
	len += sprintf(buf+len, "nubus stuck events : %10lu\n",
		       nubus_stuck_events);
#ifdef CONFIG_BLK_DEV_IDE
	len += sprintf(buf+len, "nubus/IDE interrupt: %10lu\n",
		       mac_ide_irqs);
#endif	

	return len;
}

void via_scsi_clear(void)
{
	volatile unsigned char deep_magic;
	if (via2_is_rbv) {
		via_write(rbv_regp, rIFR, (1<<3)|(1<<0)|0x80);
		deep_magic = via_read(rbv_regp, rBufB);
	} else if (via2_is_oss) {
		/* nothing */
		/* via_write(oss_regp, 9, 0) */;
	} else
		deep_magic = via_read(via2_regp, vBufB);
	mac_enable_irq( IRQ_IDX(IRQ_MAC_SCSI) );
}


void mac_default_handler(int irq, void *dev_id, struct pt_regs *regs)
{
#ifdef DEBUG_SPURIOUS
	if (console_loglevel > 6)
		printk("Unexpected IRQ %d on device %p\n", irq, dev_id);
#endif
}

static int num_debug[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

void mac_debug_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	if (num_debug[irq] < 10) {
		printk("DEBUG: Unexpected IRQ %d\n", irq);
		num_debug[irq]++;
	}
}

void scsi_mac_debug(void);
void scsi_mac_polled(void);

static int in_nmi = 0;
static volatile int nmi_hold = 0;

void mac_nmi_handler(int irq, void *dev_id, struct pt_regs *fp)
{
	int i;
	/* 
	 * generate debug output on NMI switch if 'debug' kernel option given
	 * (only works with Penguin!)
	 */

	in_nmi++;
#if 0
	scsi_mac_debug();
	printk("PC: %08lx\nSR: %04x  SP: %p\n", fp->pc, fp->sr, fp);
#endif
	for (i=0; i<100; i++)
		udelay(1000);

	if (in_nmi == 1) {
		nmi_hold = 1;
		printk("... pausing, press NMI to resume ...");
	} else {
		printk(" ok!\n");
		nmi_hold = 0;
	}

	barrier();

	while (nmi_hold == 1)
		udelay(1000);

#if 0
	scsi_mac_polled();
#endif

	if ( console_loglevel >= 8 ) {
#if 0
		show_state();
		printk("PC: %08lx\nSR: %04x  SP: %p\n", fp->pc, fp->sr, fp);
		printk("d0: %08lx    d1: %08lx    d2: %08lx    d3: %08lx\n",
		       fp->d0, fp->d1, fp->d2, fp->d3);
		printk("d4: %08lx    d5: %08lx    a0: %08lx    a1: %08lx\n",
		       fp->d4, fp->d5, fp->a0, fp->a1);
	
		if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page)
			printk("Corrupted stack page\n");
		printk("Process %s (pid: %d, stackpage=%08lx)\n",
			current->comm, current->pid, current->kernel_stack_page);
		if (intr_count == 1)
			dump_stack((struct frame *)fp);
#else
		/* printk("NMI "); */
#endif
	}
	in_nmi--;
}

/*
 *	Unexpected via interrupt
 */
 
void via_wtf(int slot, void *via, struct pt_regs *regs)
{
#ifdef DEBUG_SPURIOUS
	if (console_loglevel > 6)
		printk("Unexpected nubus event %d on via %p\n",slot,via);
#endif
}

/*
 * The generic VIA interrupt routines (shamelessly stolen from Alan Cox's
 * via6522.c :-), disable/pending masks added.
 * The int *viaidx etc. is just to keep the prototype happy ...
 */

static void via_irq(unsigned char *via, int *viaidx, struct pt_regs *regs)
{
	unsigned char events=(via_read(via, vIFR)&via_read(via,vIER))&0x7F;
	int i;
	int ct = 0;
	struct irqhandler *via_handler = handler_table[*viaidx];
	struct irqparam   *via_param   = param_table[*viaidx];
	unsigned long     *via_irqs    = mac_irqs[*viaidx];

	/* to be changed, possibly: for each non'masked', enabled IRQ, read 
	 * flag bit, ack and call handler ...
         * Currently: all pending irqs ack'ed en bloc.
	 * If ack for masked IRQ required: keep 'pending' info separate.
	 */

        /* shouldn't we disable interrupts here ?? */

	
	/*
	 *	Shouldnt happen
	 */
	 
	if(events==0)
	{
#ifdef DEBUG_VIA
		/* should go away; mostly missing timer ticks and ADB events */
		printk("via%d_irq: nothing pending, flags %x mask %x!\n",
			*viaidx + 1, via_read(via, vIFR), via_read(via,vIER));
#endif
		via_irqs[7]++;
		return;
	}

#ifdef DEBUG_VIA	
	/*
	 * limited verbosity for VIA interrupts
	 */
#if 0
	if ( (*viaidx == 0 && events != 1<<6) 		/* timer int */
	  || (*viaidx == 1 && events != 1<<3) )		/* SCSI IRQ */
#else
	if ( *viaidx == 0 && (events & 1<<2) ) 
#endif
		printk("via_irq: irq %d events %x !\n", (*viaidx)+1, events);
#endif

	do {
		/*
		 *	Clear the pending flag
		 */
		 
		via_write(via, vIFR, events);
		 
		/*
		 *	Now see what bits are raised
		 */
		 
		for(i=0;i<7;i++)
		{
			/* determine machspec. irq no. */
			int irq = ((*viaidx)+1)* 8 + i;
		        /* call corresponding handlers */
		        if (events&(1<<i)) {
			        if (irq_flags[*viaidx].disabled & (1<<i)) {
					if (!irq_flags[*viaidx].pending&(1<<i))
						via_irqs[i]++;
				        /* irq disabled -> mark pending */
				        irq_flags[*viaidx].pending |= (1<<i);
				} else {
					via_irqs[i]++;
				        /* irq enabled -> call handler */
				        (via_handler[i].handler)(irq, via, regs);
				}
			}
			/* and call handlers for pending irqs - first ?? */
			if (    (irq_flags[*viaidx].pending  & (1<<i))
			    && !(irq_flags[*viaidx].disabled & (1<<i)) ) {
				/* call handler for re-enabled irq */
			        (via_handler[i].handler)(irq, via, regs);
				/* and clear pending flag :-) */
				irq_flags[*viaidx].pending  &= ~(1<<i);
			}
		}
	
		/*
		 *	And done ... check for more punishment!
		 */

		events=(via_read(via, vIFR)&via_read(via,vIER))&0x7F;
		ct++;
		if(events && ct>8)
		{
#ifdef DEBUG_VIA
		        printk("via%d: stuck events %x\n", (*viaidx)+1, events);
#endif
		        break;
		}
	}
	while(events);
#if 0
	scsi_mac_polled();
#endif
}

/*
 * Caution: the following stuff is called from process_int as _autovector_ 
 * system interrupts. So irq is always in the range 0-7 :-( and the selection
 * of the appropriate VIA is up to the irq handler here based on the autovec 
 * irq number. There's no information whatsoever about which source on the VIA
 * triggered the int - and that's what the machspec irq no's are about. 
 * Broken design :-((((
 */

/*
 *	System interrupts
 */

void via1_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int srcidx = IRQ_IDX(irq) - 1;
	via_irq((unsigned char *)via1_regp, &srcidx, regs);
}


/*
 *	Nubus / SCSI interrupts, VIA style (could be wrapped into via1_irq or
 *	via_irq directly by selecting the regp based on the irq!)
 */
 
void via2_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int srcidx = IRQ_IDX(irq) - 1;
	via_irq((unsigned char *)via2_regp, &srcidx, regs);
}

/*
 *	Nubus / SCSI interrupts; RBV style
 *	The RBV is different. RBV appears to stand for randomly broken
 *	VIA (or even real broken VIA).
 */
 
void rbv_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int srcidx = IRQ_IDX(irq) - 1;	/* MUST be 1 !! */
	volatile unsigned char *via = rbv_regp;
	unsigned char events=(via_read(via, rIFR)&via_read(via,rIER))&0x7F;
	int i;
	int ct = 0;
	struct irqhandler *via_handler = handler_table[srcidx];
	struct irqparam   *via_param   = param_table[srcidx];

        /* shouldn't we disable interrupts here ?? */

	
	/*
	 *	Shouldnt happen
	 */
	 
	if(events==0)
	{
#ifdef DEBUG_VIA
		printk("rbv_irq: nothing pending, flags %x mask %x!\n",
			via_read(via, rIFR), via_read(via,rIER));
#endif
		rbv_irqs[7]++;
		return;
	}

#ifdef DEBUG_VIA	
	/*
	 * limited verbosity for RBV interrupts (add more if needed)
	 */
	if ( srcidx == 1 && events != 1<<3 )		/* SCSI IRQ */
		printk("rbv_irq: irq %d (%d) events %x !\n", irq, srcidx+1, events);
#endif

	/* to be changed, possibly: for each non'masked', enabled IRQ, read 
	 * flag bit, ack and call handler ...
         * Currently: all pending irqs ack'ed en bloc.
	 * If ack for masked IRQ required: keep 'pending' info separate.
	 */

	do {
		/*
		 *	Clear the pending flag
		 */
		 
		via_write(via, rIFR, events | rbv_clear);
		 
		/*
		 *	Now see what bits are raised
		 */
		 
		for(i=0;i<7;i++)
		{
			/* determine machspec. irq no. */
			int irq = (srcidx+1)* 8 + i;
		        /* call corresponding handlers */
		        if (events&(1<<i)) {
			        if (irq_flags[srcidx].disabled & (1<<i)) {
					if (!irq_flags[srcidx].pending&(1<<i))
						rbv_irqs[i]++;
				        /* irq disabled -> mark pending */
				        irq_flags[srcidx].pending |= (1<<i);
				} else {
					rbv_irqs[i]++;
				        /* irq enabled -> call handler */
				        (via_handler[i].handler)(irq, via, regs);
				}
			}
			/* and call handlers for pending irqs - first ?? */
			if (    (irq_flags[srcidx].pending  & (1<<i))
			    && !(irq_flags[srcidx].disabled & (1<<i)) ) {
				/* call handler for re-enabled irq */
			        (via_handler[i].handler)(irq, via, regs);
				/* and clear pending flag :-) */
				irq_flags[srcidx].pending  &= ~(1<<i);
			}
		}

		/*
		 *	And done ... check for more punishment!
		 */

		events=(via_read(via, rIFR)&via_read(via,rIER))&0x7F;
		ct++;
		if(events && ct>8)
		{
		        printk("rbv: stuck events %x\n",events);
			for(i=0;i<7;i++)
			{
				if(events&(1<<i))
				{
					printk("rbv - bashing source %d\n",
						i);
					via_write(via, rIER, 1<<i);
					via_write(via, rIFR, (1<<i) | rbv_clear);
				}
			}
		        break;
		}
	}
	while(events);
#if 0
	scsi_mac_polled();
#endif
}

/*
 *	Nubus / SCSI interrupts; OSS style
 *	The OSS is even more different than the RBV. OSS appears to stand for 
 *	Obscenely Screwed Silicon ... 
 *
 *	Latest NetBSD sources suggest the OSS should behave like a RBV, but 
 *	that's probably true for the 0x203 offset (Nubus/ADB-SWIM IOP) at best
 */
 
void oss_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int srcidx = IRQ_IDX(irq) - 1;	/* MUST be 1 !! */
	volatile unsigned char *via = oss_regp;
	unsigned char events=(via_read(via, oIFR))&0x03;
	unsigned char nub_ev=(via_read(via, nIFR))&0x4F;
	unsigned char adb_ev;
	int i;
	int ct = 0;
	struct irqhandler *via_handler = handler_table[srcidx];
	struct irqparam   *via_param   = param_table[srcidx];

        /* shouldn't we disable interrupts here ?? */

	adb_ev = nub_ev & 0x40;
	nub_ev &= 0x3F;

	/*
	 *	Shouldnt happen
	 */

	if (events==0 && adb_ev==0 && nub_ev==0)
	{
		printk("oss_irq: nothing pending, flags %x %x!\n",
			via_read(via, oIFR), via_read(via, nIFR));
		rbv_irqs[7]++;
		return;
	}

#ifdef DEBUG_VIA	
	/*
	 * limited verbosity for RBV interrupts (add more if needed)
	 */
	if ( events != 1<<3 )		/* SCSI IRQ */
		printk("oss_irq: irq %d srcidx+1 %d events %x %x %x !\n", irq, srcidx+1, 
			events, adb_ev, nub_ev);
#endif

	/* 
	 * OSS priorities: call ADB handler first if registered, other events,
	 * then Nubus 
	 * ADB: yet to be implemented!
	 */

	/*
	 * ADB: try to shutup the IOP 
	 */
	if (adb_ev) {
		printk("Hands off ! Don't press this button ever again !!!\n");
		via_write(via, 6, 0);
	}

	do {
		/*
		 *	Clear the pending flags
		 *	How exactly is that supposed to work ??
		 */

		/*
		 *	Now see what bits are raised
		 */
		 
		for(i=0;i<7;i++)
		{
			/* HACK HACK: map to bit number in OSS register */
			int irqidx = oss_map[i];
			/* determine machspec. irq no. */
			int irq = (srcidx+1)* 8 + i;
		        /* call corresponding handlers */
		        if ( (events&(1<<irqidx)) && 		/* bit set*/
		             (via_read(via, irqidx+8)&0x7) ) {	/* irq enabled */
			        if (irq_flags[srcidx].disabled & (1<<i)) {
					if (!irq_flags[srcidx].pending&(1<<i))
						rbv_irqs[i]++;
				        /* irq disabled -> mark pending */
				        irq_flags[srcidx].pending |= (1<<i);
				} else {
					rbv_irqs[i]++;
				        /* irq enabled -> call handler */
				        (via_handler[i].handler)(irq, via, regs);
				}
			}
			/* and call handlers for pending irqs - first ?? */
			if (    (irq_flags[srcidx].pending  & (1<<i))
			    && !(irq_flags[srcidx].disabled & (1<<i)) ) {
				/* call handler for re-enabled irq */
			        (via_handler[i].handler)(irq, via, regs);
				/* and clear pending flag :-) */
				irq_flags[srcidx].pending  &= ~(1<<i);
			}
		}
	
		/*
		 *	And done ... check for more punishment!
		 */

		events=(via_read(via, oIFR)/*&via_read(via,rIER)*/)&0x03;
		ct++;
		if(events && ct>8)
		{
		        printk("oss: stuck events %x\n",events);
			for(i=0;i<7;i++)
			{
				if(events&(1<<i))
				{
					printk("oss - bashing source %d\n",
						i);
					/* that should disable it */
					via_write(via, 8+i, 0);
				}
			}
		        break;
		}
	}
	while(events);
#if 0
	scsi_mac_polled();
#endif

	if (nub_ev)
		oss_do_nubus(irq, via, regs);

}

/*
 *	Unexpected slot interrupt
 */
 
void nubus_wtf(int slot, void *via, struct pt_regs *regs)
{
#ifdef DEBUG_NUBUS_SPURIOUS
	if (console_loglevel > 6)
		printk("Unexpected interrupt on nubus slot %d\n",slot);
#endif
}

/*
 *	SCC master interrupt handler; sole purpose: pass the registered 
 *	async struct to the SCC handler proper.
 */

void mac_SCC_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int i;
				/* 1+2: compatibility with PSC ! */
	for (i = 1; i < 3; i++) /* currently only these two used */
		if (scc_handler[i].handler != mac_default_handler) {
			(scc_handler[i].handler)(i, scc_handler[i].dev_id, regs);
			scc_irqs[i]++;
		}
}

/*
 *	PSC interrupt handler
 */

void psc_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	int srcidx = IRQ_IDX(irq) - 1;
	volatile unsigned char *via = psc_regp;
	unsigned int pIFR = 0x100 + 0x10*srcidx;
	unsigned int pIER = 0x104 + 0x10*srcidx;
	unsigned char events=(via_read(via, pIFR)&via_read(via,pIER))&0xF;
	int i;
	int ct = 0;
	struct irqhandler *via_handler = handler_table[srcidx];
	struct irqparam   *via_param   = param_table[srcidx];

        /* shouldn't we disable interrupts here ?? */

	
	/*
	 *	Shouldnt happen
	 */
	 
	if(events==0)
	{
#ifdef DEBUG_VIA
		printk("psc_irq: nothing pending, flags %x mask %x!\n",
			via_read(via, pIFR), via_read(via,pIER));
#endif
		mac_irqs[srcidx][7]++;
		return;
	}

#ifdef DEBUG_VIA	
	/*
	 * limited verbosity for PSC interrupts (add more if needed)
	 */
	if ( srcidx == 1 && events != 1<<3 && events != 1<<1 )		/* SCSI IRQ */
		printk("psc_irq: irq %d srcidx+1 %d events %x !\n", irq, srcidx+1, events);
#endif

	/* to be changed, possibly: for each non'masked', enabled IRQ, read 
	 * flag bit, ack and call handler ...
         * Currently: all pending irqs ack'ed en bloc.
	 * If ack for masked IRQ required: keep 'pending' info separate.
	 */

	do {
		/*
		 *	Clear the pending flag
		 */
		 
		/* via_write(via, pIFR, events); */
		 
		/*
		 *	Now see what bits are raised
		 */
		 
		for(i=0;i<7;i++)
		{
			/* determine machspec. irq no. */
			int irq = (srcidx+1)* 8 + i;
		        /* call corresponding handlers */
		        if (events&(1<<i)) {
			        if (irq_flags[srcidx].disabled & (1<<i)) {
					if (!irq_flags[srcidx].pending&(1<<i))
						mac_irqs[srcidx][i]++;
				        /* irq disabled -> mark pending */
				        irq_flags[srcidx].pending |= (1<<i);
				} else {
					mac_irqs[srcidx][i]++;
				        /* irq enabled -> call handler */
				        (via_handler[i].handler)(irq, via, regs);
				}
			}
			/* and call handlers for pending irqs - first ?? */
			if (    (irq_flags[srcidx].pending  & (1<<i))
			    && !(irq_flags[srcidx].disabled & (1<<i)) ) {
				/* call handler for re-enabled irq */
			        (via_handler[i].handler)(irq, via, regs);
				/* and clear pending flag :-) */
				irq_flags[srcidx].pending  &= ~(1<<i);
			}
		}

		/*
		 *	And done ... check for more punishment!
		 */

		events=(via_read(via,pIFR)&via_read(via,pIER))&0x7F;
		ct++;
		if(events && ct>8)
		{
		        printk("psc: stuck events %x\n",events);
			for(i=0;i<7;i++)
			{
				if(events&(1<<i))
				{
					printk("psc - bashing source %d\n",
						i);
					via_write(via, pIER, 1<<i);
					/* via_write(via, pIFR, (1<<i)); */
				}
			}
		        break;
		}
	}
	while(events);
}


/*
 *	Nubus handling
 *      Caution: slot numbers are currently 'hardcoded' to the range 9-15!
 *      In general, the same request_irq() functions as above can be used if
 *      the interrupt numbers specifed in macints.h are used.
 */
 
static int nubus_active=0;
 
int nubus_request_irq(int slot, void *dev_id, void (*handler)(int,void *,struct pt_regs *))
{
	slot-=9;
/*	printk("Nubus request irq for slot %d\n",slot);*/
	if(nubus_handler[slot].handler!=nubus_wtf)
		return -EBUSY;
	nubus_handler[slot].handler=handler;
	nubus_handler[slot].dev_id =dev_id;
	nubus_param[slot].flags    = IRQ_FLG_LOCK;
	nubus_param[slot].devname  = "nubus slot";

	/* 
	 * if no nubus int. was active previously: register the main nubus irq
	 * handler now!
	 */

	if (!nubus_active && !via2_is_oss) {
		request_irq(IRQ_MAC_NUBUS, via_do_nubus, IRQ_FLG_LOCK, 
			    "nubus dispatch", via_do_nubus);
		mac_turnon_irq(IRQ_MAC_NUBUS);
	}

	nubus_active|=1<<slot;
/*	printk("program slot %d\n",slot);*/
/*	printk("via2=%p\n",via2);*/
#if 0
	via_write(via2, vDirA, 
		via_read(via2, vDirA)|(1<<slot));
	via_write(via2, vBufA, 0);
#endif		
	if (via2_is_oss)
		via_write(oss_regp, slot, 2);
	else if (!via2_is_rbv) {
		/* Make sure the bit is an input */
		via_write(via2_regp, vDirA, 
			via_read(via2_regp, vDirA)&~(1<<slot));
	}
/*	printk("nubus irq on\n");*/
	return 0;
}

int nubus_free_irq(int slot)
{
	slot-=9;
	nubus_active&=~(1<<slot);
	nubus_handler[slot].handler=nubus_wtf;
	nubus_handler[slot].dev_id = NULL;
	nubus_param[slot].flags    = IRQ_FLG_STD;
	nubus_param[slot].devname  = NULL;

	if (via2_is_rbv)
		via_write(rbv_regp, rBufA, 1<<slot);
	else if (via2_is_oss)
		via_write(oss_regp, slot, 0);
	else {
		via_write(via2_regp, vDirA, 
			via_read(via2_regp, vDirA)|(1<<slot));
		via_write(via2_regp, vBufA, 1<<slot);
		via_write(via2_regp, vDirA, 
			via_read(via2_regp, vDirA)&~(1<<slot));
	}
	return 0;
}

#ifdef CONFIG_BLK_DEV_MAC_IDE
/*
 * IDE interrupt hook
 */
extern void (*mac_ide_intr_hook)(int, void *, struct pt_regs *);
extern int (*mac_ide_irq_p_hook)(void);
#endif

/*
 * Nubus dispatch handler - VIA/RBV style
 */
static void via_do_nubus(int slot, void *via, struct pt_regs *regs)
{
	unsigned char map, allints;
	int i;
	int ct=0;
	int ide_pending = 0;
		
	/* lock the nubus interrupt */
	/* That's just 'clear Nubus IRQ bit in VIA2' BTW. Pretty obsolete ? */
	if (via2_is_rbv) 
		via_write(rbv_regp, rIFR, 0x82);
	else
		via_write(via2_regp, vIFR, 0x82);
	
#ifdef CONFIG_BLK_DEV_MAC_IDE
	/* IDE hack */
	if (mac_ide_intr_hook) {
		/* 'slot' is lacking the machspec bit in 2.0 */
		/* need to pass proper dev_id = hwgroup here */
		mac_ide_intr_hook(IRQ_MAC_NUBUS, via, regs);
		mac_ide_irqs++;
	}
#endif

	while(1)
	{
		if (via2_is_rbv)
			allints = ~via_read(rbv_regp, rBufA);
		else
			allints = ~via_read(via2_regp, vBufA);
		
#ifdef CONFIG_BLK_DEV_MAC_IDE
		if (mac_ide_irq_p_hook)
			ide_pending = mac_ide_irq_p_hook();
#endif

		if ( (map = (allints&nubus_active)) == 0 
#ifdef CONFIG_BLK_DEV_MAC_IDE
		      && !ide_pending 
#endif
							) 
		{
			if (ct == 0) {
#ifdef DEBUG_VIA_NUBUS
				if (console_loglevel > 5)
					printk("nubus_irq: nothing pending, map %x mask %x active %x\n", 
						allints, nubus_active, map);
#endif
				nubus_irqs[7]++;
			}
			/* clear it */
			if (allints)
				if (via2_is_rbv)
					via_write(rbv_regp, rIFR, 0x02);
				else
					via_write(via2_regp, vIFR, 0x02);
			break;
		}

#ifdef DEBUG_VIA_NUBUS
		if (console_loglevel > 6)
			printk("nubus_irq: map %x mask %x active %x\n", 
				allints, nubus_active, map);
#endif

#ifdef CONFIG_BLK_DEV_MAC_IDE
		if (mac_ide_intr_hook && ide_pending) {
			mac_ide_intr_hook(IRQ_MAC_NUBUS, via, regs);
			mac_ide_irqs++;
		}
#endif

		if(ct++>2)
		{
			if (console_loglevel > 5)
				printk("nubus stuck events - %x/%x/%x ide %x\n", 
				allints, nubus_active, map, ide_pending);
			nubus_stuck_events++;

			return;
		}

		for(i=0;i<7;i++)
		{
			if(map&(1<<i))
			{
				nubus_irqs[i]++;
				(nubus_handler[i].handler)(i+9, nubus_handler[i].dev_id, regs);
			}
		}
		/* clear it */
		if (via2_is_rbv)
			via_write(rbv_regp, rIFR, 0x02);
		else
			via_write(via2_regp, vIFR, 0x02);

	}
	
	/* And done */
}

/*
 * Nubus dispatch handler - OSS style
 */
static void oss_do_nubus(int slot, void *via, struct pt_regs *regs)
{
	unsigned char map;
	int i;
	int ct=0;

/*	printk("nubus interrupt\n");*/
		
#if 0
	/* lock the nubus interrupt */
	if (via2_is_rbv) 
		via_write(rbv_regp, rIFR, 0x82);
	else
		via_write(via2_regp, vIFR, 0x82);
#endif

	/* IDE hack for Quadra: uses Nubus interrupt without any slot bit set */
#ifdef CONFIG_BLK_DEV_MAC_IDE
	if (mac_ide_intr_hook)
		mac_ide_intr_hook(IRQ_MAC_NUBUS, via, regs);
#endif
	
	while(1)
	{
		/* pending events */
		map=(via_read(via, nIFR))&0x3F;
		
#ifdef DEBUG_VIA_NUBUS
		printk("nubus_irq: map %x mask %x\n", map, nubus_active);
#endif
		if( (map = (map&nubus_active)) ==0 ) {
			if (ct == 0) {
#ifdef CONFIG_BLK_DEV_MAC_IDE
				if (!mac_ide_intr_hook)
					printk("nubus_irq: nothing pending, map %x mask %x\n", 
						map, nubus_active);
#endif
				nubus_irqs[7]++;
			}
			break;
		}

		if(ct++>2)
		{
#if 0
			printk("nubus stuck events - %d/%d\n", map, nubus_active);
#endif
			return;
		}

		for(i=0;i<7;i++)
		{
			if(map&(1<<i))
			{
				nubus_irqs[i]++;
				/* call handler */
				(nubus_handler[i].handler)((i+9), nubus_handler[i].dev_id, regs);
				/* clear interrupt ?? */
#if 0
				via_write(oss_regp, i, 0);
#endif
			}
		}
		/* clear it */ 
#if 0
		via_write(oss_regp, nIFR, map);
#endif
	}
	
	/* And done */
}
