#ifndef __M68K_SOFTIRQ_H
#define __M68K_SOFTIRQ_H

/*
 * Software interrupts.. no SMP here either.
 */

#include <asm/atomic.h>

#define local_bh_disable()	(local_bh_count(smp_processor_id())++)
#define local_bh_enable()	(local_bh_count(smp_processor_id())--)

#define in_softirq() (local_bh_count != 0)

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)  (local_bh_count(cpu) ? 0 : (local_bh_count(cpu)=1))
#define softirq_endlock(cpu)  (local_bh_count(cpu) = 0)
#define synchronize_bh()	barrier()

#endif
