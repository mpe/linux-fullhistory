/*
 *  include/asm-s390/hardirq.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  Derived from "include/asm-i386/hardirq.h"
 */

#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/lowcore.h>
#include <linux/sched.h>
/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ((atomic_read(&S390_lowcore.local_irq_count) + atomic_read(&S390_lowcore.local_bh_count)) != 0)

#define in_irq() (atomic_read(&S390_lowcore.local_irq_count) != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(atomic_read(&S390_lowcore.local_irq_count) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(atomic_inc(&S390_lowcore.local_irq_count))
#define hardirq_exit(cpu)	(atomic_dec(&S390_lowcore.local_irq_count))

#define synchronize_irq()	do { } while (0)

#else

#include <asm/atomic.h>
#include <asm/smp.h>

extern atomic_t global_irq_holder;
extern atomic_t global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (atomic_read(&global_irq_holder) ==  cpu) {
		atomic_set(&global_irq_holder,NO_PROC_ID);
		clear_bit(0,&global_irq_lock);
	}
}

static inline void hardirq_enter(int cpu)
{
        atomic_inc(&safe_get_cpu_lowcore(cpu).local_irq_count);
	atomic_inc(&global_irq_count);
}

static inline void hardirq_exit(int cpu)
{
	atomic_dec(&global_irq_count);
        atomic_dec(&safe_get_cpu_lowcore(cpu).local_irq_count);
}

static inline int hardirq_trylock(int cpu)
{
	return !atomic_read(&global_irq_count) && !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
