#ifndef __I386_SMPLOCK_H
#define __I386_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()		do { } while(0)
#define unlock_kernel()		do { } while(0)

typedef struct { } spinlock_t;
#define SPIN_LOCK_UNLOCKED

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)

#define spin_lock_cli(lock)		\
({	unsigned long flags;		\
	save_flags(flags); cli();	\
	return flags;			\
})

#define spin_unlock_restore(lock, flags)	restore_flags(flags)

#else
#include <asm/pgtable.h>

/* Locking the kernel */
extern __inline__ void lock_kernel(void)
{
	int cpu = smp_processor_id();

	__asm__ __volatile__("
	pushfl
	cli
	cmpl	$0, %0
	jne	0f
	movl	$0f, %%eax
	jmp	__lock_kernel
0:
	incl	%0
	popfl
"	:
	: "m" (current_set[cpu]->lock_depth), "d" (cpu)
	: "ax", "memory");
}

extern __inline__ void unlock_kernel(void)
{
	__asm__ __volatile__("
	pushfl
	cli
	decl	%0
	jnz	1f
	movb	%1, active_kernel_processor
	lock
	btrl	$0, kernel_flag
1:
	popfl
"	: /* no outputs */
	: "m" (current->lock_depth), "i" (NO_PROC_ID)
	: "ax", "memory");
}

/* Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 *
 *	NOT YET TESTED!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */

typedef unsigned char spinlock_t;

/* Arse backwards is faster for us on Intel (trylock is a clock faster) */

#define SPIN_LOCK_UNLOCKED	1

extern __inline__ void __spinlock_waitfor(spinlock_t *lock)
{
	int cpu=smp_processor_id();
	do
	{
		/* Spin reading and let the MESI cache do the right stuff
		   without us thrashing the bus */
		while(lock)
		{
			/*
			 *	Not a race, the interrupt will pick up
			 *	the exiting case that looks suspicious.
			 *	(The test_bit is not locked so won't
			 *	 thrash the bus either).
			 */
			if(test_bit(cpu,&smp_invalidate_needed))
			{
				local_flush_tlb();
				clear_bit(cpu,&smp_invalidate_needed);
			}
		}
	}
	while(clear_bit(0,lock));
}

extern __inline__ void spin_lock_init(spinlock_t *lock)
{
	*lock = 1;	/* We assume init does not need to be itself SMP safe */
}

extern __inline__ void spin_lock(spinlock_t *lock)
{
	/* Returns the old value. If we get 1 then we got the lock */
	if(clear_bit(0,lock))
	{
		__spinlock_waitfor(lock);
	}
}

extern __inline__ int spin_trylock(spinlock_t *lock)
{
	return clear_bit(0,lock);
}

extern __inline__ void spin_unlock(spinlock_t *lock)
{
	set_bit(0,lock);
}

/* These variants clear interrupts and return save_flags() style flags
 * to the caller when acquiring a lock.  To release the lock you must
 * pass the lock pointer as well as the flags returned from the acquisition
 * routine when releasing the lock.
 */
extern __inline__ unsigned long spin_lock_cli(spinlock_t *lock)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	if(clear_bit(0,lock))
		__spinlock_waitfor(lock);
	return flags;
}

extern __inline__ void spin_unlock_restore(spinlock_t *lock, unsigned long flags)
{
	set_bit(0,lock);	/* Locked operation to keep it serialized with
				   the popfl */
	restore_flags(flags);
}


#endif /* __SMP__ */

#endif /* __I386_SMPLOCK_H */
