/*
 * amiints.c -- Amiga Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

/* isr node variables for amiga interrupt sources */
static isr_node_t *ami_lists[NUM_AMIGA_SOURCES];

static const ushort ami_intena_vals[NUM_AMIGA_SOURCES] = {
    IF_VERTB, IF_COPER, IF_AUD0, IF_AUD1, IF_AUD2, IF_AUD3, IF_BLIT,
    IF_DSKSYN, IF_DSKBLK, IF_RBF, IF_TBE, IF_PORTS, IF_PORTS, IF_PORTS,
    IF_PORTS, IF_PORTS, IF_EXTER, IF_EXTER, IF_EXTER, IF_EXTER, IF_EXTER,
    IF_SOFT, IF_PORTS, IF_EXTER
    };

struct ciadata
{
    volatile struct CIA *ciaptr;
    unsigned long	baseirq;
} ciadata[2];

/*
 * index into ami_lists for IRQs.  CIA IRQs are special, because
 * the same cia interrupt handler is used for both CIAs.
 */
#define IRQ_IDX(source) (source & ~IRQ_MACHSPEC)
#define CIA_IRQ_IDX(source) (IRQ_IDX(datap->baseirq) \
			     +(source-IRQ_AMIGA_CIAA_TA))

/*
 * void amiga_init_INTS (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the amiga IRQ handling routines.
 */

static void
    ami_int1(int irq, struct pt_regs *fp, void *data),
    ami_int2(int irq, struct pt_regs *fp, void *data),
    ami_int3(int irq, struct pt_regs *fp, void *data),
    ami_int4(int irq, struct pt_regs *fp, void *data),
    ami_int5(int irq, struct pt_regs *fp, void *data),
    ami_int6(int irq, struct pt_regs *fp, void *data),
    ami_int7(int irq, struct pt_regs *fp, void *data),
    ami_intcia(int irq, struct pt_regs *fp, void *data);

void amiga_init_INTS (void)
{
    int i;

    /* initialize handlers */
    for (i = 0; i < NUM_AMIGA_SOURCES; i++)
	ami_lists[i] = NULL;

    add_isr (IRQ1, ami_int1, 0, NULL, "int1 handler");
    add_isr (IRQ2, ami_int2, 0, NULL, "int2 handler");
    add_isr (IRQ3, ami_int3, 0, NULL, "int3 handler");
    add_isr (IRQ4, ami_int4, 0, NULL, "int4 handler");
    add_isr (IRQ5, ami_int5, 0, NULL, "int5 handler");
    add_isr (IRQ6, ami_int6, 0, NULL, "int6 handler");
    add_isr (IRQ7, ami_int7, 0, NULL, "int7 handler");

    /* hook in the CIA interrupts */
    ciadata[0].ciaptr = &ciaa;
    ciadata[0].baseirq = IRQ_AMIGA_CIAA_TA;
    add_isr (IRQ_AMIGA_PORTS, ami_intcia, 0, NULL, "Amiga CIAA");
    ciadata[1].ciaptr = &ciab;
    ciadata[1].baseirq = IRQ_AMIGA_CIAB_TA;
    add_isr (IRQ_AMIGA_EXTER, ami_intcia, 0, NULL, "Amiga CIAB");

    /* turn off all interrupts and enable the master interrupt bit */
    custom.intena = 0x7fff;
    custom.intreq = 0x7fff;
    custom.intena = 0xc000;

    /* turn off all CIA interrupts */
    ciaa.icr = 0x7f;
    ciab.icr = 0x7f;

    /* clear any pending CIA interrupts */
    i = ciaa.icr;
    i = ciab.icr;
}


/*
 * The builtin Amiga hardware interrupt handlers.
 */

static void ami_int1 (int irq, struct pt_regs *fp, void *data)
{
    ushort ints = custom.intreqr & custom.intenar;

    /* if serial transmit buffer empty, interrupt */
    if (ints & IF_TBE) {
	if (ami_lists[IRQ_IDX(IRQ_AMIGA_TBE)]) {
	    call_isr_list (IRQ_AMIGA_TBE,
			   ami_lists[IRQ_IDX(IRQ_AMIGA_TBE)], fp);
	    /* 
	     * don't acknowledge.... 
	     * allow serial code to turn off interrupts, but
	     * leave it pending so that when interrupts are
	     * turned on, transmission will resume
	     */
	} else
	    /* acknowledge the interrupt */
	    custom.intreq = IF_TBE;
    }

    /* if floppy disk transfer complete, interrupt */
    if (ints & IF_DSKBLK) {
	call_isr_list (IRQ_AMIGA_DSKBLK,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_DSKBLK)], fp);

	/* acknowledge */
	custom.intreq = IF_DSKBLK;
    }

    /* if software interrupt set, interrupt */
    if (ints & IF_SOFT) {
	call_isr_list (IRQ_AMIGA_SOFT,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_SOFT)], fp);

	/* acknowledge */
	custom.intreq = IF_SOFT;
    }
}

static void ami_int2 (int irq, struct pt_regs *fp, void *data)
{
    ushort ints = custom.intreqr & custom.intenar;

    if (ints & IF_PORTS) {
	/* call routines which have hooked into the PORTS interrupt */
	call_isr_list (IRQ_AMIGA_PORTS,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_PORTS)], fp);

	/* acknowledge */
	custom.intreq = IF_PORTS;
    }
}

static void ami_int3 (int irq, struct pt_regs *fp, void *data)
{
    ushort ints = custom.intreqr & custom.intenar;

    /* if a copper interrupt */
    if (ints & IF_COPER) {
	call_isr_list (IRQ_AMIGA_COPPER,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_COPPER)], fp);

	/* acknowledge */
	custom.intreq = IF_COPER;
    }

    /* if a vertical blank interrupt */
    if (ints & IF_VERTB) {
	call_isr_list (IRQ_AMIGA_VERTB,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_VERTB)], fp);

	/* acknowledge */
	custom.intreq = IF_VERTB;
    }

    /* if a blitter interrupt */
    if (ints & IF_BLIT) {
	call_isr_list (IRQ_AMIGA_BLIT,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_BLIT)], fp);

	/* acknowledge */
	custom.intreq = IF_BLIT;
    }
}

static void ami_int4 (int irq, struct pt_regs *fp, void *data)
{
    ushort ints = custom.intreqr & custom.intenar;

    /* if audio 0 interrupt */
    if (ints & IF_AUD0) {
	call_isr_list (IRQ_AMIGA_AUD0,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_AUD0)], fp);

	/* acknowledge */
	custom.intreq = IF_AUD0;
    }

    /* if audio 1 interrupt */
    if (ints & IF_AUD1) {
	call_isr_list (IRQ_AMIGA_AUD1,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_AUD1)], fp);

	/* acknowledge */
	custom.intreq = IF_AUD1;
    }

    /* if audio 2 interrupt */
    if (ints & IF_AUD2) {
	call_isr_list (IRQ_AMIGA_AUD2,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_AUD2)], fp);

	/* acknowledge */
	custom.intreq = IF_AUD2;
    }

    /* if audio 3 interrupt */
    if (ints & IF_AUD3) {
	call_isr_list (IRQ_AMIGA_AUD3,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_AUD3)], fp);

	/* acknowledge */
	custom.intreq = IF_AUD3;
    }
}

static void ami_int5 (int irq, struct pt_regs *fp, void *data)
{
    ushort ints = custom.intreqr & custom.intenar;

    /* if serial receive buffer full interrupt */
    if (ints & IF_RBF) {
	if (ami_lists[IRQ_IDX(IRQ_AMIGA_RBF)]) {
	    call_isr_list (IRQ_AMIGA_RBF,
			   ami_lists[IRQ_IDX(IRQ_AMIGA_RBF)], fp);
	    /* don't acknowledge ; leave that for the handler */
        } else
	    /* acknowledge the interrupt */
	    custom.intreq = IF_RBF;
    }

    /* if a disk sync interrupt */
    if (ints & IF_DSKSYN) {
	call_isr_list (IRQ_AMIGA_DSKSYN,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_DSKSYN)], fp);

	/* acknowledge */
	custom.intreq = IF_DSKSYN;
    }
}

static void ami_int6 (int irq, struct pt_regs *fp, void *data)
{
    ushort ints = custom.intreqr & custom.intenar;

    if (ints & IF_EXTER) {
	/* call routines which have hooked into the EXTER interrupt */
	call_isr_list (IRQ_AMIGA_EXTER,
		       ami_lists[IRQ_IDX(IRQ_AMIGA_EXTER)], fp);

	/* acknowledge */
	custom.intreq = IF_EXTER;
    }
}

static void ami_int7 (int irq, struct pt_regs *fp, void *data)
{
    panic ("level 7 interrupt received\n");
}

static void ami_intcia (int irq, struct pt_regs *fp, void *data)
{
    /* check CIA interrupts */
    struct ciadata *datap;
    u_char cia_ints;

    /* setup data correctly */
    if (irq == IRQ_AMIGA_PORTS)
	    datap = &ciadata[0];
    else
	    datap = &ciadata[1];

    cia_ints = datap->ciaptr->icr;

    /* if timer A interrupt */
    if (cia_ints & CIA_ICR_TA)
	call_isr_list (IRQ_AMIGA_CIAA_TA,
		       ami_lists[CIA_IRQ_IDX(IRQ_AMIGA_CIAA_TA)], fp);

    /* if timer B interrupt */
    if (cia_ints & CIA_ICR_TB)
	call_isr_list (IRQ_AMIGA_CIAA_TB,
		       ami_lists[CIA_IRQ_IDX(IRQ_AMIGA_CIAA_TB)], fp);

    /* if the alarm interrupt */
    if (cia_ints & CIA_ICR_ALRM)
	call_isr_list (IRQ_AMIGA_CIAA_ALRM,
		       ami_lists[CIA_IRQ_IDX(IRQ_AMIGA_CIAA_ALRM)], fp);

    /* if serial port interrupt (keyboard) */
    if (cia_ints & CIA_ICR_SP)
	call_isr_list (IRQ_AMIGA_CIAA_SP,
		       ami_lists[CIA_IRQ_IDX(IRQ_AMIGA_CIAA_SP)], fp);

    /* if flag interrupt (parallel port) */
    if (cia_ints & CIA_ICR_FLG)
	call_isr_list (IRQ_AMIGA_CIAA_FLG,
		       ami_lists[CIA_IRQ_IDX(IRQ_AMIGA_CIAA_FLG)], fp);
}

/*
 * amiga_add_isr : add an interrupt service routine for a particular
 *		   machine specific interrupt source.
 *		   If the addition was successful, it returns 1, otherwise
 *		   it returns 0.  It will fail if another routine is already
 *		   bound into the specified source.
 *   Note that the "pri" argument is currently unused.
 */

int amiga_add_isr (unsigned long source, isrfunc isr, int pri, void
		   *data, char *name)
{
    unsigned long amiga_source = source & ~IRQ_MACHSPEC;
    isr_node_t *p;

    if (amiga_source > NUM_AMIGA_SOURCES) {
	printk ("amiga_add_isr: Unknown interrupt source %ld\n", source);
	return 0;
    }

    p = new_isr_node();
    p->isr = isr;
    p->pri = pri;
    p->data = data;
    p->name = name;
    p->next = NULL;
    insert_isr (&ami_lists[amiga_source], p);

    /* enable the interrupt */
    custom.intena = IF_SETCLR | ami_intena_vals[amiga_source];

    /* if a CIAA interrupt, enable the appropriate CIA ICR bit */
    if (source >= IRQ_AMIGA_CIAA_TA && source <= IRQ_AMIGA_CIAA_FLG)
	ciaa.icr = 0x80 | (1 << (source - IRQ_AMIGA_CIAA_TA));

    /* if a CIAB interrupt, enable the appropriate CIA ICR bit */
    if (source >= IRQ_AMIGA_CIAB_TA && source <= IRQ_AMIGA_CIAB_FLG)
	ciab.icr = 0x80 | (1 << (source - IRQ_AMIGA_CIAB_TA));

    return 1;
}

int amiga_get_irq_list( char *buf, int len )
{	int			i;
    isr_node_t	*p;

	for( i = 0; i < NUM_AMIGA_SOURCES; ++i ) {
		if (!ami_lists[i])
			continue;
		len += sprintf( buf+len, "ami  %2d: ???????? ", i );
		for( p = ami_lists[i]; p; p = p->next ) {
			len += sprintf( buf+len, "%s\n", p->name );
			if (p->next)
				len += sprintf( buf+len, "                  " );
		}
	}
	
	return( len );
}
