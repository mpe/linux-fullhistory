#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/config.h>

#ifdef CONFIG_PMAC
#define NR_IRQS	32
#else
#define NR_IRQS	16
#endif

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif
