#ifndef _ALPHA_IRQ_H
#define _ALPHA_IRQ_H

/*
 *	linux/include/asm-sparc/irq.h
 *
 *	Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/linkage.h>

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define __STR(x) #x
#define STR(x) __STR(x)
 
#define SAVE_ALL "xx"

#define SAVE_MOST "yy"

#define RESTORE_MOST "zz"

#define ACK_FIRST(mask) "aa"

#define ACK_SECOND(mask) "dummy"

#define UNBLK_FIRST(mask) "dummy"

#define UNBLK_SECOND(mask) "dummy"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)
#define FAST_IRQ_NAME(nr) IRQ_NAME2(fast_IRQ##nr)
#define BAD_IRQ_NAME(nr) IRQ_NAME2(bad_IRQ##nr)
	
#define BUILD_IRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
asmlinkage void FAST_IRQ_NAME(nr); \
asmlinkage void BAD_IRQ_NAME(nr); \
asm code comes here

#endif
