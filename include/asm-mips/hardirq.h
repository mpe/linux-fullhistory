/* $Id: hardirq.h,v 1.5 1998/08/29 21:20:21 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998 by Ralf Baechle
 */
#ifndef __ASM_MIPS_HARDIRQ_H
#define __ASM_MIPS_HARDIRQ_H

#include <linux/tasks.h>

extern unsigned int local_irq_count[NR_CPUS];

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count[__cpu] + local_bh_count[__cpu] != 0); })

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	barrier();

#else

#error No habla MIPS SMP

#endif /* __SMP__ */
#endif /* __ASM_MIPS_HARDIRQ_H */
