/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <linux/config.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/system.h>		/* for membar() */

#ifndef CONFIG_SMP
extern unsigned int local_bh_count;
#else
#define local_bh_count		(cpu_data[smp_processor_id()].bh_count)
#endif

#define local_bh_disable()	(local_bh_count++)
#define local_bh_enable()	(local_bh_count--)

#define in_softirq() (local_bh_count != 0)

#endif /* !(__SPARC64_SOFTIRQ_H) */
