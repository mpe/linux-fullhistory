/*
 * arch/m68k/atari/ataints.c -- Atari Linux interrupt handling code
 *
 * 5/2/94 Roman Hodek:
 *  Added support for TT interrupts; setup for TT SCU (may someone has
 *  twiddled there and we won't get the right interrupts :-()
 *
 *  Major change: The device-independant code in m68k/ints.c didn't know
 *  about non-autovec ints yet. It hardcoded the number of possible ints to
 *  7 (IRQ1...IRQ7). But the Atari has lots of non-autovec ints! I made the
 *  number of possible ints a constant defined in interrupt.h, which is
 *  47 for the Atari. So we can call add_isr() for all Atari interrupts just
 *  the normal way. Additionally, all vectors >= 48 are initialized to call
 *  trap() instead of inthandler(). This must be changed here, too.
 *
 * 1995-07-16 Lars Brinkhoff <f93labr@dd.chalmers.se>:
 *  Corrected a bug in atari_add_isr() which rejected all SCC
 *  interrupt sources if there were no TT MFP!
 *
 * 12/13/95: New interface functions atari_level_triggered_int() and
 *  atari_register_vme_int() as support for level triggered VME interrupts.
 *
 * 02/12/96: (Roman)
 *  Total rewrite of Atari interrupt handling, for new scheme see comments
 *  below.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/kernel_stat.h>

#include <asm/system.h>
#include <asm/traps.h>

#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>


/*
 * Atari interrupt handling scheme:
 * --------------------------------
 * 
 * All interrupt source have an internal number (defined in
 * <asm/atariints.h>): Autovector interrupts are 1..7, then follow ST-MFP,
 * TT-MFP, SCC, and finally VME interrupts. Vector numbers for the latter can
 * be allocated by atari_register_vme_int(). Currently, all int source numbers
 * have the IRQ_MACHSPEC bit set, to keep the general int handling functions
 * in kernel/ints.c from them.
 *
 * Each interrupt can be of three types:
 * 
 *  - SLOW: The handler runs with all interrupts enabled, except the one it
 *    was called by (to avoid reentering). This should be the usual method.
 *    But it is currently possible only for MFP ints, since only the MFP
 *    offers an easy way to mask interrupts.
 *
 *  - FAST: The handler runs with all interrupts disabled. This should be used
 *    only for really fast handlers, that just do actions immediately
 *    necessary, and let the rest do a bottom half or task queue.
 *
 *  - PRIORITIZED: The handler can be interrupted by higher-level ints
 *    (greater IPL, no MFP priorities!). This is the method of choice for ints
 *    which should be slow, but are not from a MFP.
 *
 * The feature of more than one handler for one int source is still there, but
 * only applicable if all handers are of the same type. To not slow down
 * processing of ints with only one handler by the chaining feature, the list
 * calling function atari_call_isr_list() is only plugged in at the time the
 * second handler is registered.
 *
 * Implementation notes: For fast-as-possible int handling, there are separate
 * entry points for each type (slow/fast/prio). The assembler handler calls
 * the isr directly in the usual case, no C wrapper is involved. In case of
 * multiple handlers, atari_call_isr_list() is registered as handler and calls
 * in turn the real isr's. To ease access from assembler level to the isr
 * function pointer and accompanying data, these two are stored in a separate
 * array, irq_handler[]. The rest of data (type, name) are put into a second
 * array, irq_param, that is accessed from C only. For each slow interrupt (32
 * in all) there are separate handler functions, which makes it possible to
 * hard-code the MFP register address and value, are necessary to mask the
 * int. If there'd be only one generic function, lots of calculations would be
 * needed to determine MFP register and int mask from the vector number :-(
 *
 * Furthermore, slow ints may not lower the IPL below its previous value
 * (before the int happened). This is needed so that an int of class PRIO, on
 * that this int may be stacked, cannot be reentered. This feature is
 * implemented as follows: If the stack frame format is 1 (throwaway), the int
 * is not stacked, and the IPL is anded with 0xfbff, resulting in a new level
 * 2, which still blocks the HSYNC, but no interrupts of interest. If the
 * frame format is 0, the int is nested, and the old IPL value can be found in
 * the sr copy in the frame.
 */


#define	NUM_INT_SOURCES	(8 + NUM_ATARI_SOURCES)

typedef void (*asm_irq_handler)(void);

struct irqhandler {
	isrfunc	isr;
	void	*data;
};

struct irqparam {
	int type;
	char *name;
};

/*
 * Array with isr's and their parameter data. This array is accessed from low
 * level assembler code, so an element size of 8 allows usage of index scaling
 * addressing mode.
 */
static struct irqhandler irq_handler[NUM_INT_SOURCES];

/*
 * This array hold the rest of parameters of int handlers: type
 * (slow,fast,prio) and the name of the handler. These values are only
 * accessed from C
 */
static struct irqparam irq_param[NUM_INT_SOURCES];

/*
 * Counter for next free interrupt vector number
 * (new vectors starting from 0x70 can be allocated by
 * atari_register_vme_int())
 */
static int next_free_vme_vec = VME_SOURCE_BASE;

/* check for valid int number (complex, sigh...) */
#define	IS_VALID_INTNO(n)											\
	((n) > 0 &&														\
	 /* autovec and ST-MFP ok anyway */								\
	 (((n) < TTMFP_SOURCE_BASE) ||									\
	  /* TT-MFP ok if present */									\
	  ((n) >= TTMFP_SOURCE_BASE && (n) < SCC_SOURCE_BASE &&			\
	   ATARIHW_PRESENT(TT_MFP)) ||									\
	  /* SCC ok if present and number even */						\
	  ((n) >= SCC_SOURCE_BASE && (n) < VME_SOURCE_BASE &&			\
	   !((n) & 1) && ATARIHW_PRESENT(SCC)) ||						\
	  /* greater numbers ok if less than #registered VME vectors */	\
	  ((n) >= VME_SOURCE_BASE && (n) < next_free_vme_vec)))


/*
 * Here start the assembler entry points for interrupts
 */

#define IRQ_NAME(nr) atari_slow_irq_##nr##_handler(void)

#define	MFP_MK_BASE	"0xfa13"

/* This must agree with head.S.  */
#define ORIG_DO "0x20"
#define FORMATVEC "0x2E"
#define SR "0x28"
#define SAVE_ALL				\
	"clrl	%%sp@-;"    /* stk_adj */	\
	"clrl	%%sp@-;"			\
	"subql	#1,%%sp@;"  /* orig d0 = -1 */	\
	"movel	%%d0,%%sp@-;" /* d0 */		\
	"moveml	%%d1-%%d5/%%a0-%%a1,%%sp@-"

#define	BUILD_SLOW_IRQ(n)						   \
asmlinkage void IRQ_NAME(n);						   \
/* Dummy function to allow asm with operands.  */			   \
void atari_slow_irq_##n##_dummy (void) {				   \
__asm__ (ALIGN_STR "\n"							   \
SYMBOL_NAME_STR(atari_slow_irq_) #n "_handler:\t"			   \
	SAVE_ALL "\n"							   \
"	addql	#1,"SYMBOL_NAME_STR(intr_count)"\n"			   \
"	andb	#~(1<<(" #n "&7)),"	/* mask this interrupt */	   \
	"("MFP_MK_BASE"+(((" #n "&8)^8)>>2)+((" #n "&16)<<3)):w\n"	   \
"	movew	%%sp@("SR"),%%d0\n"	/* get old IPL from stack frame */ \
"	movew	%%sr,%%d1\n"						   \
"	andw	#0x0700,%%d0\n"						   \
"	andw	#0xf8ff,%%d1\n"						   \
"	orw 	%%d0,%%d1\n"						   \
"	movew	%%d1,%%sr\n"		/* set IPL = previous value */	   \
"	addql	#1,%a0\n"						   \
"	lea	"SYMBOL_NAME_STR(irq_handler)"+("#n"+8)*8,%%a0\n"	   \
"	movel	%%a0@(4),%%sp@-\n"	/* push handler data */		   \
"	pea 	%%sp@(4)\n"		/* push addr of frame */	   \
"	pea 	(" #n "+8):w\n"		/* push int number */		   \
"	movel	%%a0@,%%a0\n"						   \
"	jbsr	%%a0@\n"		/* call the handler */		   \
"	addql	#8,%%sp\n"						   \
"	addql	#4,%%sp\n"						   \
"	orw	#0x0600,%%sr\n"						   \
"	andw	#0xfeff,%%sr\n"		/* set IPL = 6 again */		   \
"	orb 	#(1<<(" #n "&7)),"	/* now unmask the int again */	   \
	    "("MFP_MK_BASE"+(((" #n "&8)^8)>>2)+((" #n "&16)<<3)):w\n"	   \
"	jbra	"SYMBOL_NAME_STR(ret_from_interrupt)"\n"		   \
	 : : "i" (&kstat.interrupts[n+8])				   \
);									   \
}

BUILD_SLOW_IRQ(0);
BUILD_SLOW_IRQ(1);
BUILD_SLOW_IRQ(2);
BUILD_SLOW_IRQ(3);
BUILD_SLOW_IRQ(4);
BUILD_SLOW_IRQ(5);
BUILD_SLOW_IRQ(6);
BUILD_SLOW_IRQ(7);
BUILD_SLOW_IRQ(8);
BUILD_SLOW_IRQ(9);
BUILD_SLOW_IRQ(10);
BUILD_SLOW_IRQ(11);
BUILD_SLOW_IRQ(12);
BUILD_SLOW_IRQ(13);
BUILD_SLOW_IRQ(14);
BUILD_SLOW_IRQ(15);
BUILD_SLOW_IRQ(16);
BUILD_SLOW_IRQ(17);
BUILD_SLOW_IRQ(18);
BUILD_SLOW_IRQ(19);
BUILD_SLOW_IRQ(20);
BUILD_SLOW_IRQ(21);
BUILD_SLOW_IRQ(22);
BUILD_SLOW_IRQ(23);
BUILD_SLOW_IRQ(24);
BUILD_SLOW_IRQ(25);
BUILD_SLOW_IRQ(26);
BUILD_SLOW_IRQ(27);
BUILD_SLOW_IRQ(28);
BUILD_SLOW_IRQ(29);
BUILD_SLOW_IRQ(30);
BUILD_SLOW_IRQ(31);

asm_irq_handler slow_handlers[32] = {
	atari_slow_irq_0_handler,
	atari_slow_irq_1_handler,
	atari_slow_irq_2_handler,
	atari_slow_irq_3_handler,
	atari_slow_irq_4_handler,
	atari_slow_irq_5_handler,
	atari_slow_irq_6_handler,
	atari_slow_irq_7_handler,
	atari_slow_irq_8_handler,
	atari_slow_irq_9_handler,
	atari_slow_irq_10_handler,
	atari_slow_irq_11_handler,
	atari_slow_irq_12_handler,
	atari_slow_irq_13_handler,
	atari_slow_irq_14_handler,
	atari_slow_irq_15_handler,
	atari_slow_irq_16_handler,
	atari_slow_irq_17_handler,
	atari_slow_irq_18_handler,
	atari_slow_irq_19_handler,
	atari_slow_irq_20_handler,
	atari_slow_irq_21_handler,
	atari_slow_irq_22_handler,
	atari_slow_irq_23_handler,
	atari_slow_irq_24_handler,
	atari_slow_irq_25_handler,
	atari_slow_irq_26_handler,
	atari_slow_irq_27_handler,
	atari_slow_irq_28_handler,
	atari_slow_irq_29_handler,
	atari_slow_irq_30_handler,
	atari_slow_irq_31_handler
};

asmlinkage void atari_fast_irq_handler( void );
asmlinkage void atari_prio_irq_handler( void );

/* Dummy function to allow asm with operands.  */
void atari_fast_prio_irq_dummy (void) {
__asm__ (ALIGN_STR "\n"
SYMBOL_NAME_STR(atari_fast_irq_handler) ":
	orw 	#0x700,%%sr		/* disable all interrupts */
"SYMBOL_NAME_STR(atari_prio_irq_handler) ":\t"
	SAVE_ALL "
	addql	#1,"SYMBOL_NAME_STR(intr_count)"
	movew	%%sp@(" FORMATVEC "),%%d0	/* get vector number from stack frame */
	andil	#0xfff,%%d0		/* mask off format nibble */
	lsrl	#2,%%d0			/* convert vector to source */
	subl	#(0x40-8),%%d0
	jpl 	1f
	addl	#(0x40-8-0x18),%%d0
1:	lea	%a0,%%a0
	addql	#1,%%a0@(%%d0:l:4)
	lea	"SYMBOL_NAME_STR(irq_handler)",%%a0
	lea	%%a0@(%%d0:l:8),%%a0
	movel	%%a0@(4),%%sp@-		/* push handler data */
	pea 	%%sp@(4)		/* push frame address */
	movel	%%d0,%%sp@-		/* push int number */
	movel	%%a0@,%%a0
	jsr	%%a0@			/* and call the handler */
	addql	#8,%%sp
	addql	#4,%%sp
	jbra	"SYMBOL_NAME_STR(ret_from_interrupt)
	 : : "i" (&kstat.interrupts)
);
}

/* GK:
 * HBL IRQ handler for Falcon. Nobody needs it :-)
 * ++andreas: raise ipl to disable further HBLANK interrupts.
 */
asmlinkage void falcon_hblhandler(void);
asm(".text\n"
ALIGN_STR "\n"
SYMBOL_NAME_STR(falcon_hblhandler) ":
	movel	%d0,%sp@-
	movew	%sp@(4),%d0
	andw	#0xf8ff,%d0
	orw	#0x0200,%d0	/* set saved ipl to 2 */
	movew	%d0,%sp@(4)
	movel	%sp@+,%d0
	rte");

/* Defined in entry.S; only increments 'num_suprious' */
asmlinkage void bad_interrupt(void);

extern void atari_microwire_cmd( int cmd );

/*
 * void atari_init_INTS (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the atari IRQ handling routines.
 */

void atari_init_INTS(void)
{
	int i;

	/* initialize the vector table */
	for (i = 0; i < NUM_INT_SOURCES; ++i) {
		vectors[IRQ_SOURCE_TO_VECTOR(i)] = bad_interrupt;
	}

	/* Initialize the MFP(s) */

#ifdef ATARI_USE_SOFTWARE_EOI
	mfp.vec_adr  = 0x48;	/* Software EOI-Mode */
#else
	mfp.vec_adr  = 0x40;	/* Automatic EOI-Mode */
#endif
	mfp.int_en_a =		    /* turn off MFP-Ints */
	mfp.int_en_b = 0x00;
	mfp.int_mk_a =			/* no Masking */
	mfp.int_mk_b = 0xff;

	if (ATARIHW_PRESENT(TT_MFP)) {
#ifdef ATARI_USE_SOFTWARE_EOI
		tt_mfp.vec_adr  = 0x58;		/* Software EOI-Mode */
#else
		tt_mfp.vec_adr  = 0x50;		/* Automatic EOI-Mode */
#endif
		tt_mfp.int_en_a =			/* turn off MFP-Ints */
		tt_mfp.int_en_b = 0x00;
		tt_mfp.int_mk_a =			/* no Masking */
		tt_mfp.int_mk_b = 0xff;
	}

	if (ATARIHW_PRESENT(SCC)) {
		scc.cha_a_ctrl = 9;
		MFPDELAY();
		scc.cha_a_ctrl = (char) 0xc0; /* hardware reset */
	}

	if (ATARIHW_PRESENT(SCU)) {
		/* init the SCU if present */
		tt_scu.sys_mask = 0x10;		/* enable VBL (for the cursor) and
									 * disable HSYNC interrupts (who
									 * needs them?)  MFP and SCC are
									 * enabled in VME mask
									 */
		tt_scu.vme_mask = 0x60;		/* enable MFP and SCC ints */
	}
	else {
		/* If no SCU, the HSYNC interrupt needs to be disabled this
		 * way. (Else _inthandler in kernel/sys_call.S gets overruns)
		 */
		vectors[VEC_INT2] = falcon_hblhandler;
	}

	if (ATARIHW_PRESENT(PCM_8BIT) && ATARIHW_PRESENT(MICROWIRE)) {
		/* Initialize the LM1992 Sound Controller to enable
		   the PSG sound.  This is misplaced here, it should
		   be in a atasound_init(), that doesn't exist yet. */
		atari_microwire_cmd(MW_LM1992_PSG_HIGH);
	}
	
	stdma_init();

	/* Initialize the PSG: all sounds off, both ports output */
	sound_ym.rd_data_reg_sel = 7;
	sound_ym.wd_data = 0xff;
}


static void atari_call_isr_list( int irq, struct pt_regs *fp, void *_p )
{
  isr_node_t *p;
	
  for( p = (isr_node_t *)_p; p; p = p->next )
    p->isr( irq, fp, p->data );
}


/*
 * atari_add_isr : add an interrupt service routine for a particular
 *		   machine specific interrupt source.
 *		   If the addition was successful, it returns 1, otherwise
 *		   it returns 0.  It will fail if the interrupt is already
 *                 occupied of another handler with different type
 */

int atari_add_isr(unsigned long source, isrfunc isr, int type, void
		  *data, char *name)
{
	int vector;
	
	source &= ~IRQ_MACHSPEC;
	if (type < IRQ_TYPE_SLOW || type > IRQ_TYPE_PRIO) {
		printk ("atari_add_isr: Bad irq type %d requested from %s\n",
				type, name );
		return( 0 );
	}
	if (!IS_VALID_INTNO(source)) {
		printk ("atari_add_isr: Unknown irq %ld requested from %s\n",
				source, name );
		return( 0 );
	}
	vector = IRQ_SOURCE_TO_VECTOR(source);

	/*
	 * Check type/source combination: slow ints are (currently)
	 * only possible for MFP-interrupts.
	 */
	if (type == IRQ_TYPE_SLOW &&
		(source < STMFP_SOURCE_BASE || source >= SCC_SOURCE_BASE)) {
		printk ("atari_add_isr: Slow irq requested for non-MFP source %ld from %s\n",
				source, name );
		return( 0 );
	}
		
	if (vectors[vector] == bad_interrupt) {
		/* int has no handler yet */
		irq_handler[source].isr = isr;
		irq_handler[source].data = data;
		irq_param[source].type = type;
		irq_param[source].name = name;
		vectors[vector] =
			(type == IRQ_TYPE_SLOW) ? slow_handlers[source-STMFP_SOURCE_BASE] :
			(type == IRQ_TYPE_FAST) ? atari_fast_irq_handler :
									  atari_prio_irq_handler;
		/* If MFP int, also enable and umask it */
		atari_turnon_irq(source);
		atari_enable_irq(source);

		return 1;
	}
	else if (irq_param[source].type == type) {
		/* old handler is of same type -> handlers can be chained */
		isr_node_t *p;
		unsigned long flags;

		save_flags(flags);
		cli();

		if (irq_handler[source].isr != atari_call_isr_list) {
			/* Only one handler yet, make a node for this first one */
			p = new_isr_node();
			if (p == NULL) return 0;
			p->isr = irq_handler[source].isr;
			p->data = irq_handler[source].data;
			p->name = irq_param[source].name;
			p->next = NULL;

			irq_handler[source].isr = atari_call_isr_list;
			irq_handler[source].data = p;
			irq_param[source].name = "chained";
		}

		p = new_isr_node();
		if (p == NULL) return 0;
		p->isr = isr;
		p->data = data;
		p->name = name;
		/* new handlers are put in front of the queue */
		p->next = irq_handler[source].data;
		irq_handler[source].data = p;

		restore_flags(flags);
		return 1;
	}
	else {
		printk ("atari_add_isr: Irq %ld allocated by other type int (call from %s)\n",
				source, name );
		return( 0 );
	}
}


int atari_remove_isr(unsigned long source, isrfunc isr)
{
	unsigned long flags;
	int vector;
	isr_node_t **p, *q;

	source &= ~IRQ_MACHSPEC;

	if (!IS_VALID_INTNO(source)) {
		printk("atari_remove_isr: Unknown irq %ld\n", source);
		return 0;
	}

	vector = IRQ_SOURCE_TO_VECTOR(source);
	if (vectors[vector] == bad_interrupt)
		goto not_found;

	save_flags(flags);
	cli();

	if (irq_handler[source].isr != atari_call_isr_list) {
		/* It's the only handler for the interrupt */
		if (irq_handler[source].isr != isr) {
			restore_flags(flags);
			goto not_found;
		}
		irq_handler[source].isr = NULL;
		irq_handler[source].data = NULL;
		irq_param[source].name = NULL;
		vectors[vector] = bad_interrupt;
		/* If MFP int, also disable it */
		atari_disable_irq(source);
		atari_turnoff_irq(source);

		restore_flags(flags);
		return 1;
	}

	/* The interrupt is chained, find the isr on the list */
	for( p = (isr_node_t **)&irq_handler[source].data; *p; p = &(*p)->next ) {
		if ((*p)->isr == isr) break;
	}
	if (!*p) {
		restore_flags(flags);
		goto not_found;
	}

	(*p)->isr = NULL; /* Mark it as free for reallocation */
	*p = (*p)->next;

	/* If there's now only one handler, unchain the interrupt, i.e. plug in
	 * the handler directly again and omit atari_call_isr_list */
	q = (isr_node_t *)irq_handler[source].data;
	if (q && !q->next) {
		irq_handler[source].isr = q->isr;
		irq_handler[source].data = q->data;
		irq_param[source].name = q->name;
		q->isr = NULL; /* Mark it as free for reallocation */
	}

	restore_flags(flags);
	return 1;

  not_found:
	printk("atari_remove_isr: isr %p not found on list!\n", isr);
	return 0;
}


/*
 * atari_register_vme_int() returns the number of a free interrupt vector for
 * hardware with a programmable int vector (probably a VME board).
 */

unsigned long atari_register_vme_int(void)
{
	unsigned long source;
	
	if (next_free_vme_vec == NUM_ATARI_SOURCES)
		return 0;

	source = next_free_vme_vec | IRQ_MACHSPEC;
	next_free_vme_vec++;
	return source;
}


int atari_get_irq_list(char *buf, int len)
{
	int i;

	for (i = 0; i < NUM_INT_SOURCES; ++i) {
		if (vectors[IRQ_SOURCE_TO_VECTOR(i)] == bad_interrupt)
			continue;
		if (i < STMFP_SOURCE_BASE)
			len += sprintf(buf+len, "auto %2d: %8d ",
				       i, kstat.interrupts[i]);
		else
			len += sprintf(buf+len, "vec $%02x: %8d ",
				       IRQ_SOURCE_TO_VECTOR(i),
				       kstat.interrupts[i]);

		if (irq_handler[i].isr != atari_call_isr_list) {
			len += sprintf(buf+len, "%s\n", irq_param[i].name);
		}
		else {
			isr_node_t *p;
			for( p = (isr_node_t *)irq_handler[i].data; p; p = p->next ) {
				len += sprintf(buf+len, "%s\n", p->name);
				if (p->next)
					len += sprintf( buf+len, "                  " );
			}
		}
	}
	if (num_spurious)
		len += sprintf(buf+len, "spurio.: %8ld\n", num_spurious);
	
	return len;
}


