#include <linux/config.h>

#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <asm/processor.h>		/* for is_prep() */

#ifndef CONFIG_8xx

#ifdef CONFIG_APUS
#define enable_irq m68k_enable_irq
#define disable_irq m68k_disable_irq
#include <asm-m68k/irq.h>
#undef enable_irq
#undef disable_irq
#else /* CONFIG_APUS */

/*
 * this is the # irq's for all ppc arch's (pmac/chrp/prep)
 * so it is the max of them all - which happens to be powermac
 * at present (G3 powermacs have 64).
 */
#define NR_IRQS			128

#endif /* CONFIG_APUS */

#define NUM_8259_INTERRUPTS	16
#define NUM_OPENPIC_INTERRUPTS	20
#define is_8259_irq(n)		((n) < NUM_8259_INTERRUPTS)
#define openpic_to_irq(n)	((n)+NUM_8259_INTERRUPTS)
#define irq_to_openpic(n)	((n)-NUM_8259_INTERRUPTS)
#define IRQ_8259_CASCADE	NUM_8259_INTERRUPTS

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#ifndef CONFIG_APUS
/*
 * This gets called from serial.c, which is now used on
 * powermacs as well as prep/chrp boxes.
 * Prep and chrp both have cascaded 8259 PICs.
 */
static __inline__ int irq_cannonicalize(int irq)
{
	return (((is_prep || is_chrp) && irq == 2) ? 9 : irq);
}
#endif

#else /* CONFIG_8xx */

/* The MPC8xx cores have 16 possible interrupts.  There are eight
 * possible level sensitive interrupts assigned and generated internally
 * from such devices as CPM, PCMCIA, RTC, PIT, TimeBase and Decrementer.
 * There are eight external interrupts (IRQs) that can be configured
 * as either level or edge sensitive. 
 * On the MBX implementation, there is also the possibility of an 8259
 * through the PCI and PCI-ISA bridges.  All 8259 interrupts appear
 * on the 8xx as IRQ3, but I may eventually add some of the 8259 code
 * back into this port to handle that controller.
 */
#define NR_IRQS	16

#define	SIU_IRQ0	0	/* Highest priority */
#define	SIU_LEVEL0	1
#define	SIU_IRQ1	2
#define	SIU_LEVEL1	3
#define	SIU_IRQ2	4
#define	SIU_LEVEL2	5
#define	SIU_IRQ3	6
#define	SIU_LEVEL3	7
#define	SIU_IRQ4	8
#define	SIU_LEVEL4	9
#define	SIU_IRQ5	10
#define	SIU_LEVEL5	11
#define	SIU_IRQ6	12
#define	SIU_LEVEL6	13
#define	SIU_IRQ7	14
#define	SIU_LEVEL7	15

/* The internal interrupts we can configure as we see fit.
 * My personal preference is CPM at level 2, which puts it above the
 * MBX PCI/ISA/IDE interrupts.
 */
#define PIT_INTERRUPT		SIU_LEVEL0
#define CPM_INTERRUPT		SIU_LEVEL2
#define PCMCIA_INTERRUPT	SIU_LEVEL6
#define DEC_INTERRUPT		SIU_LEVEL7

/* Some internal interrupt registers use an 8-bit mask for the interrupt
 * level instead of a number.
 */
#define	mk_int_int_mask(IL) (1 << (7 - (IL/2)))

#ifdef CONFIG_MBX
/* These are defined (and fixed) by the MBX hardware implementation.*/
#define POWER_FAIL_INT	SIU_IRQ0	/* Power fail */
#define TEMP_HILO_INT	SIU_IRQ1	/* Temperature sensor */
#define QSPAN_INT	SIU_IRQ2	/* PCI Bridge (DMA CTLR?) */
#define ISA_BRIDGE_INT	SIU_IRQ3	/* All those PC things */
#define COMM_L_INT	SIU_IRQ6	/* MBX Comm expansion connector pin */
#define STOP_ABRT_INT	SIU_IRQ7	/* Stop/Abort header pin */
#endif /* CONFIG_MBX */

#ifdef CONFIG_FADS
#define FEC_INTERRUPT	SIU_LEVEL1	/* FEC interrupt */
#endif

/* always the same on MBX -- Cort */
static __inline__ int irq_cannonicalize(int irq)
{
	return irq;
}

#endif /* CONFIG_8xx */

#endif
