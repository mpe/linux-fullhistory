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
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h> /* for intr_count */

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
static struct irqhandler   scc_handler[8];
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
static struct irqparam   scc_param[8];
static struct irqparam nubus_param[8];

static struct irqparam *param_table[8];

/*
 * This array holds the 'disabled' and 'pending' software flags maintained
 * by mac_{enable,disable}_irq and the generic via_irq function.
 */

static struct irqflags irq_flags[8];

/*
 * This array holds the pointers to the various VIA or other interrupt 
 * controllers
 */

static volatile unsigned char *via_table[8];

#ifdef VIABASE_WEIRDNESS
/*
 * VIA2 / RBV default base address 
 */

volatile unsigned char *via2_regp = ((volatile unsigned char *)VIA2_BAS);
volatile unsigned char *rbv_regp  = ((volatile unsigned char *)VIA2_BAS_IIci);
#endif

/*
 * Flags to control via2 / rbv behaviour
 */ 

static int via2_is_rbv = 0;
static int rbv_clear = 0;

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

/*#define DEBUG_VIA*/

void mac_init_IRQ(void)
{
        int i;

	mac_debugging_penguin(6);

#ifdef DEBUG_MACINTS
	printk("Mac interrupt stuff initializing ...\n");
#endif
        /* initialize the hardwired (primary, autovector) IRQs */

	/* level 1 IRQ: VIA1, always present */
	sys_request_irq(1, via1_irq, IRQ_FLG_LOCK, "via1", via1_irq);

	/* via2 or rbv?? */
	if (macintosh_config->via_type == MAC_VIA_IIci) {
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
	} else
		/* level 2 IRQ: VIA2 */
		sys_request_irq(2, via2_irq, IRQ_FLG_LOCK, "via2", via2_irq);

	/* 
	 * level 4 IRQ: SCC - use 'master' interrupt routine that calls the 
	 *		registered channel-specific interrupts in turn.
	 *		Currently, one interrupt per channel is used, solely
	 *		to pass the correct async_info as parameter!
	 */
#if 0	/* doesn't seem to work yet */
	sys_request_irq(4, mac_SCC_handler, IRQ_FLG_STD, "INT4", mac_SCC_handler);
#else
	sys_request_irq(4, mac_debug_handler, IRQ_FLG_STD, "INT4", mac_debug_handler);
#endif
	/* Alan uses IRQ 5 for SCC ?? */
	sys_request_irq(5, mac_debug_handler, IRQ_FLG_STD, "INT5", mac_debug_handler);

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

	if (via2_is_rbv) {
		via_table[1]     =  rbv_regp;
		handler_table[1] =  &rbv_handler[0];
		param_table[1]   =  &rbv_param[0];
	} else {
		via_table[1]     =  via2_regp;
		handler_table[1] =  &via2_handler[0];
		param_table[1]   =  &via2_param[0];
	}
	via_table[2]     =  NULL;
	via_table[3]     =  NULL;
	
	handler_table[2] =   &rbv_handler[0];
	handler_table[3] = &nubus_handler[0];

	param_table[2]   =   &rbv_param[0];
	param_table[3]   = &nubus_param[0];

	mac_debugging_penguin(7);
#ifdef DEBUG_MACINTS
	printk("Mac interrupt init done!\n");
#endif
}

/*
 *	We have no machine specific interrupts on a macintoy
 *      Yet, we need to register/unregister interrupts ... :-)
 *      Currently unimplemented: Test for valid irq number, chained irqs,
 *      Nubus interrupts.
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

	/* figure out if SCC pseudo-irq */
        if (irq >= IRQ_IDX(IRQ_SCC) && irq < IRQ_IDX(IRQ_NUBUS_1)) {
		/* set specific SCC handler */
		scc_handler[irqidx].handler = handler;
		scc_handler[irqidx].dev_id  = dev_id;
		scc_param[irqidx].flags     = flags;
		scc_param[irqidx].devname   = devname;
		/* and done! */
		return 0;
	} 

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

	/* and turn it on ... */
	if (srcidx == SRC_VIA2 && via2_is_rbv)
		via_write(via, rIER, via_read(via, rIER)|0x80|(1<<(irqidx)));
	else
		via_write(via, vIER, via_read(via, vIER)|0x80|(1<<(irqidx)));


	if (irq == IRQ_IDX(IRQ_MAC_SCSI)) {
		/*
		 * Set vPCR for SCSI interrupts. (what about RBV here?)
		 */
		via_write(via, vPCR, 0x66);
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
        if (irq >= IRQ_IDX(IRQ_SCC) && irq < IRQ_IDX(IRQ_NUBUS_1)) {
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
 * "stored". This is done with the VIA interrupt enable register
 */

void mac_turnon_irq( unsigned int irq )
{
        int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
        int irqidx = (irq & IRQ_IDX_MASK);
	volatile unsigned char *via;

	via         = (volatile unsigned char *) via_table[srcidx];
	if (!via) 
		return;

	if (srcidx == SRC_VIA2 && via2_is_rbv)
	        via_write(via, rIER, via_read(via, rIER)|0x80|(1<<(irqidx)));
	else
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

	if (srcidx == SRC_VIA2 && via2_is_rbv)
		via_write(via, rIER, (via_read(via, rIER)&(1<<irqidx)));
	else
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
        int srcidx = ((irq & IRQ_SRC_MASK)>>3) - 1;
        int irqidx = (irq & IRQ_IDX_MASK);

	return (irq_flags[srcidx].pending & (1<<irqidx));
}

int mac_get_irq_list (char *buf)
{
	return 0;
}

void via_scsi_clear(void)
{
	volatile unsigned char deep_magic;
	if (via2_is_rbv) {
		via_write(rbv_regp, rIFR, (1<<3)|(1<<0)|0x80);
		deep_magic = via_read(rbv_regp, rBufB);
	} else
		deep_magic = via_read(via2_regp, vBufB);
	mac_enable_irq( IRQ_IDX(IRQ_MAC_SCSI) );
}


void mac_default_handler(int irq, void *dev_id, struct pt_regs *regs)
{
#ifdef DEBUG_VIA
	printk("Unexpected IRQ %d\n", irq);
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

void mac_nmi_handler(int irq, void *dev_id, struct pt_regs *fp)
{
	/* 
	 * generate debug output on NMI switch if 'debug' kernel option given
	 * (only works with Penguin!)
	 */
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
		printk("via_irq: nothing pending!\n");
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
				        /* irq disabled -> mark pending */
				        irq_flags[*viaidx].pending |= (1<<i);
				} else {
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
		        printk("via%d: stuck events %x\n", (*viaidx)+1, events);
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

        /* shouldn't we disable interrupts here ?? */

	
	/*
	 *	Shouldnt happen
	 */
	 
	if(events==0)
	{
		printk("rbv_irq: nothing pending!\n");
		return;
	}
	
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
			        if (irq_flags[srcidx].disabled & (1<<i))
				        /* irq disabled -> mark pending */
				        irq_flags[srcidx].pending |= (1<<i);
				else
				        /* irq enabled -> call handler */
				        (via_handler[i].handler)(irq, via, regs);
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
 *	Unexpected via interrupt
 */
 
void via_wtf(int slot, void *via, struct pt_regs *regs)
{
#ifdef DEBUG_VIA
	printk("Unexpected event %d on via %p\n",slot,via);
#endif
}

void nubus_wtf(int slot, void *via, struct pt_regs *regs)
{
#ifdef DEBUG_VIA
	printk("Unexpected interrupt on nubus slot %d\n",slot);
#endif
}

void mac_SCC_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int i;

	for (i = 0; i < 8; i++) 
		if (scc_handler[i].handler != mac_default_handler)
			(scc_handler[i].handler)(i, scc_handler[i].dev_id, regs);

}

/*
 *	Nubus handling
 *      Caution: slot numbers are currently 'hardcoded' to the range 9-15!
 *      In general, the same request_irq() functions as above can be used if
 *      the interrupt numbers specifed in macints.h are used.
 */
 
static int nubus_active=0;
 
int nubus_request_irq(int slot, void (*handler)(int,void *,struct pt_regs *))
{
	slot-=9;
/*	printk("Nubus request irq for slot %d\n",slot);*/
	if(nubus_handler[slot].handler!=nubus_wtf)
		return -EBUSY;
	nubus_handler[slot].handler=handler;
	nubus_handler[slot].dev_id =handler;
	nubus_param[slot].flags    = IRQ_FLG_LOCK;
	nubus_param[slot].devname  = "nubus";

	/* 
	 * if no nubus int. was active previously: register the main nubus irq
	 * handler now!
	 */

	if (!nubus_active)
	  request_irq(IRQ_MAC_NUBUS, via_do_nubus, IRQ_FLG_LOCK, 
		      "nubus dispatch", via_do_nubus);

	nubus_active|=1<<slot;
/*	printk("program slot %d\n",slot);*/
/*	printk("via2=%p\n",via2);*/
#if 0
	via_write(via2, vDirA, 
		via_read(via2, vDirA)|(1<<slot));
	via_write(via2, vBufA, 0);
#endif		
	if (!via2_is_rbv) {
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
	else {
		via_write(via2_regp, vDirA, 
			via_read(via2_regp, vDirA)|(1<<slot));
		via_write(via2_regp, vBufA, 1<<slot);
		via_write(via2_regp, vDirA, 
			via_read(via2_regp, vDirA)&~(1<<slot));
	}
	return 0;
}

static void via_do_nubus(int slot, void *via, struct pt_regs *regs)
{
	unsigned char map;
	int i;
	int ct=0;

/*	printk("nubus interrupt\n");*/
		
	/* lock the nubus interrupt */
	if (via2_is_rbv) 
		via_write(rbv_regp, rIFR, 0x82);
	else
		via_write(via2_regp, vIFR, 0x82);
	
	while(1)
	{
		if (via2_is_rbv)
			map = ~via_read(rbv_regp, rBufA);
		else
			map = ~via_read(via2_regp, vBufA);
		
#ifdef DEBUG_VIA
		printk("nubus_irq: map %x mask %x\n", map, nubus_active);
#endif
		if( (map = (map&nubus_active)) ==0 )
			break;

		if(ct++>2)
		{
			printk("nubus stuck events - %d/%d\n", map, nubus_active);
			return;
		}

		for(i=0;i<7;i++)
		{
			if(map&(1<<i))
			{
				(nubus_handler[i].handler)(i+9, via, regs);
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
