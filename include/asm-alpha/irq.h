#ifndef _ALPHA_IRQ_H
#define _ALPHA_IRQ_H

/*
 *	linux/include/alpha/irq.h
 *
 *	(C) 1994 Linus Torvalds
 */

#include <linux/linkage.h>
#include <linux/config.h>

#if   defined(CONFIG_ALPHA_CABRIOLET) || \
      defined(CONFIG_ALPHA_EB66P)     || \
      defined(CONFIG_ALPHA_EB164)     || \
      defined(CONFIG_ALPHA_PC164)     || \
	defined(CONFIG_ALPHA_LX164)

# define NR_IRQS	35

#elif defined(CONFIG_ALPHA_EB66)      || \
      defined(CONFIG_ALPHA_EB64P)     || \
	defined(CONFIG_ALPHA_MIKASA)

# define NR_IRQS	32

#elif defined(CONFIG_ALPHA_ALCOR)     || \
      defined(CONFIG_ALPHA_XLT)       || \
      defined(CONFIG_ALPHA_MIATA)     || \
      defined(CONFIG_ALPHA_RUFFIAN)   || \
      defined(CONFIG_ALPHA_NORITAKE)

# define NR_IRQS	48

#elif defined(CONFIG_ALPHA_SABLE)     || \
      defined(CONFIG_ALPHA_SX164)

# define NR_IRQS	40

#elif defined(CONFIG_ALPHA_DP264) || \
      defined(CONFIG_ALPHA_RAWHIDE)

# define NR_IRQS	64

#elif defined(CONFIG_ALPHA_TAKARA)

# define NR_IRQS	20

#else /* everyone else */

# define NR_IRQS	16

#endif

static __inline__ int irq_cannonicalize(int irq)
{
	/*
	 * XXX is this true for all Alpha's?  The old serial driver
	 * did it this way for years without any complaints, so....
	 */
	return ((irq == 2) ? 9 : irq);
}

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif
