/*
 * BK Id: %F% %I% %G% %U% %#%
 */
#ifdef __KERNEL__
#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define local_bh_disable()			\
do {						\
	preempt_disable();			\
	local_bh_count(smp_processor_id())++;	\
	barrier();				\
} while (0)

#define __local_bh_enable()			\
do {						\
	barrier();				\
	local_bh_count(smp_processor_id())--;	\
	preempt_enable();			\
} while (0)

#define local_bh_enable()				\
do {							\
	barrier();					\
	if (!--local_bh_count(smp_processor_id())	\
	    && softirq_pending(smp_processor_id())) {	\
		do_softirq();				\
	}						\
	preempt_enable();				\
} while (0)

#define in_softirq() (local_bh_count(smp_processor_id()) != 0)

#endif	/* __ASM_SOFTIRQ_H */
#endif /* __KERNEL__ */
