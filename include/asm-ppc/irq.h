#include <linux/config.h>

#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <asm/machdep.h>		/* ppc_md */

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

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
#define IRQ_8259_CASCADE	16
#define openpic_to_irq(n)	((n)+NUM_8259_INTERRUPTS)
#define irq_to_openpic(n)	((n)-NUM_8259_INTERRUPTS)

#ifndef CONFIG_APUS
/*
 * This gets called from serial.c, which is now used on
 * powermacs as well as prep/chrp boxes.
 * Prep and chrp both have cascaded 8259 PICs.
 */
static __inline__ int irq_cannonicalize(int irq)
{
	if (ppc_md.irq_cannonicalize)
	{
		return ppc_md.irq_cannonicalize(irq);
	}
	else
	{
		return irq;
	}
}
#endif

#else /* CONFIG_8xx */

/* The MPC8xx cores have 16 possible interrupts.  There are eight
 * possible level sensitive interrupts assigned and generated internally
 * from such devices as CPM, PCMCIA, RTC, PIT, TimeBase and Decrementer.
 * There are eight external interrupts (IRQs) that can be configured
 * as either level or edge sensitive. 
 *
 * The 82xx can have up to 64 interrupts on the internal controller.
 *
 * On some implementations, there is also the possibility of an 8259
 * through the PCI and PCI-ISA bridges.
 */
#ifdef CONFIG_82xx
#define NR_SIU_INTS	64
#else
#define NR_SIU_INTS	16
#endif

#define NR_IRQS	(NR_SIU_INTS + NR_8259_INTS)

/* These values must be zero-based and map 1:1 with the SIU configuration.
 * They are used throughout the 8xx/82xx I/O subsystem to generate
 * interrupt masks, flags, and other control patterns.  This is why the
 * current kernel assumption of the 8259 as the base controller is such
 * a pain in the butt.
 */
#define	SIU_IRQ0	(0)	/* Highest priority */
#define	SIU_LEVEL0	(1)
#define	SIU_IRQ1	(2)
#define	SIU_LEVEL1	(3)
#define	SIU_IRQ2	(4)
#define	SIU_LEVEL2	(5)
#define	SIU_IRQ3	(6)
#define	SIU_LEVEL3	(7)
#define	SIU_IRQ4	(8)
#define	SIU_LEVEL4	(9)
#define	SIU_IRQ5	(10)
#define	SIU_LEVEL5	(11)
#define	SIU_IRQ6	(12)
#define	SIU_LEVEL6	(13)
#define	SIU_IRQ7	(14)
#define	SIU_LEVEL7	(15)

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

/* Now include the board configuration specific associations.
*/
#include <asm/mpc8xx.h>

/* always the same on 8xx -- Cort */
static __inline__ int irq_cannonicalize(int irq)
{
	return irq;
}

#endif /* CONFIG_8xx */

#endif
