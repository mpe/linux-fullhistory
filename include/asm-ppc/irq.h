#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H


/*
 * wild guess here.  someone should go look and put
 * the right number in.
 *                      -- Cort
 */
# define NR_IRQS	16

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif
