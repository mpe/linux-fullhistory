#ifndef _ALPHA_IRQ_H
#define _ALPHA_IRQ_H

/*
 *	linux/include/alpha/irq.h
 *
 *	(C) 1994 Linus Torvalds
 */

#include <linux/linkage.h>
#include <linux/config.h>

#if defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB66P) || defined(CONFIG_ALPHA_EB164) || defined(CONFIG_ALPHA_PC164)
# define NR_IRQS	33
#elif defined(CONFIG_ALPHA_EB66) || defined(CONFIG_ALPHA_EB64P) || defined(CONFIG_ALPHA_MIKASA)
# define NR_IRQS	32
#elif defined(CONFIG_ALPHA_ALCOR)
# define NR_IRQS	48
#else
# define NR_IRQS	16
#endif


extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define __STR(x) #x
#define STR(x) __STR(x)
 
#define SAVE_ALL "xx"

/*
 * SAVE_MOST/RESTORE_MOST is used for the faster version of IRQ handlers,
 * installed by using the SA_INTERRUPT flag. These kinds of IRQ's don't
 * call the routines that do signal handling etc on return, and can have
 * more relaxed register-saving etc. They are also atomic, and are thus
 * suited for small, fast interrupts like the serial lines or the harddisk
 * drivers, which don't actually need signal handling etc.
 *
 * Also note that we actually save only those registers that are used in
 * C subroutines, so if you do something weird, you're on your own. 
 */
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
