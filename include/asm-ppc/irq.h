#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#define NR_IRQS	32

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif
