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
#elif defined(CONFIG_ALPHA_ALCOR) || defined(CONFIG_ALPHA_XLT) || defined(CONFIG_ALPHA_MIATA) || defined(CONFIG_ALPHA_NORITAKE)
# define NR_IRQS	48
#elif defined(CONFIG_ALPHA_SABLE)
# define NR_IRQS	40
#else
# define NR_IRQS	16
#endif


extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif
