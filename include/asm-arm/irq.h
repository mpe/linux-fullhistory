#ifndef __ASM_ARM_IRQ_H
#define __ASM_ARM_IRQ_H

#include <asm/arch/irqs.h>

#define irq_cannonicalize(i)	(i)

#ifndef NR_IRQS
#define NR_IRQS	128
#endif

/*
 * Use this value to indicate lack of interrupt
 * capability
 */
#ifndef NO_IRQ
#define NO_IRQ	255
#endif

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define __STR(x) #x
#define STR(x) __STR(x)

#endif

