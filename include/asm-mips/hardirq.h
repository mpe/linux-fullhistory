/* $Id: hardirq.h,v 1.8 2000/03/02 02:37:13 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_HARDIRQ_H
#define _ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
} ____cacheline_aligned irq_cpustat_t;

extern irq_cpustat_t irq_stat [NR_CPUS];

/*
 * Simple wrappers reducing source bloat
 */
#define local_irq_count(cpu) (irq_stat[(cpu)].__local_irq_count)
#define local_bh_count(cpu) (irq_stat[(cpu)].__local_bh_count)
#define nmi_counter(cpu) (irq_stat[(cpu)].__nmi_counter)

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count(__cpu) + local_bh_count(__cpu) != 0); })
#define in_irq() (local_irq_count(smp_processor_id()) != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count(cpu) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu)		(local_irq_count(cpu)++)
#define irq_exit(cpu)		(local_irq_count(cpu)--)

#define synchronize_irq()	barrier();

#else

#error No habla MIPS SMP

#endif /* CONFIG_SMP */
#endif /* _ASM_HARDIRQ_H */
