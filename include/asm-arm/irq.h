#ifndef __ASM_ARM_IRQ_H
#define __ASM_ARM_IRQ_H

#include <asm/irq-no.h>

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define __STR(x) #x
#define STR(x) __STR(x)

#endif

