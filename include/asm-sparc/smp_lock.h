/* smp_lock.h: SMP locking primitives on Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SMPLOCK_H
#define __SPARC_SMPLOCK_H

#include <asm/smp.h>
#include <asm/ptrace.h>

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

/* The following acquire and release the master kernel global lock,
 * the idea is that the usage of this mechanmism becomes less and less
 * as time goes on, to the point where they are no longer needed at all
 * and can thus disappear.
 */

/* Do not fuck with this without consulting arch/sparc/lib/locks.S first! */
extern __inline__ void lock_kernel(void)
{
	register struct klock_info *klip asm("g1");
	register int proc asm("g5");
	klip = &klock_info; proc = smp_processor_id();
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___lock_kernel
	 ld	[%%g6 + %0], %%g2
"	: : "i" (TASK_LOCK_DEPTH), "r" (klip), "r" (proc)
	: "g2", "g3", "g4", "g7", "memory");
}

/* Release kernel global lock. */
extern __inline__ void unlock_kernel(void)
{
	register struct klock_info *klip asm("g1");
	klip = &klock_info;
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___unlock_kernel
	 ld	[%%g6 + %0], %%g2
"	: : "i" (TASK_LOCK_DEPTH), "r" (klip)
	: "g2", "g3", "g4", "memory");
}

/* Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 */

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED	0

extern __inline__ void spin_lock_init(spinlock_t *lock)
{
	*lock = 0;
}

extern __inline__ void spin_lock(spinlock_t *lock)
{
	register spinlock_t *lp asm("g1");
	lp = lock;
	__asm__ __volatile__("
	ldstub	[%%g1], %%g2
	orcc	%%g2, 0x0, %%g0
	be	1f
	 mov	%%o7, %%g4
	call	___spinlock_waitfor
	 ldub	[%%g1], %%g2
1:"	: /* no outputs */
	: "r" (lp)
	: "g2", "g4", "memory");
}

extern __inline__ int spin_trylock(spinlock_t *lock)
{
	unsigned int result;

	__asm__ __volatile__("
	ldstub	[%1], %0
"	: "=r" (result) : "r" (lock) : "memory");

	return (result == 0);
}

extern __inline__ void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("stb	%%g0, [%0]" : : "r" (lock) : "memory");
}

/* These variants clear interrupts and return save_flags() style flags
 * to the caller when acquiring a lock.  To release the lock you must
 * pass the lock pointer as well as the flags returned from the acquisition
 * routine when releasing the lock.
 */
extern __inline__ unsigned long spin_lock_cli(spinlock_t *lock)
{
	register spinlock_t *lp asm("g1");
	register unsigned long flags asm("g3");
	lp = lock;
	__asm__ __volatile__("
	rd	%%psr, %%g3
	or	%%g3, %1, %%g4
	wr	%%g4, 0, %%psr
	nop; nop; nop;
	ldstub	[%%g1], %%g2
	orcc	%%g2, 0x0, %%g0
	be	1f
	 mov	%%o7, %%g4
	call	___spinlock_waitfor
	 ldub	[%%g1], %%g2
1:"	: "=r" (flags)
	: "i" (PSR_PIL), "r" (lp)
	: "g2", "g4", "memory");
	return flags;
}

extern __inline__ void spin_unlock_restore(spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__("
	stb	%%g0, [%0]
	wr	%1, 0, %%psr
	nop; nop; nop;
"	: /* no outputs */
	: "r" (lock), "r" (flags)
	: "memory");
}

#endif /* !(__SPARC_SMPLOCK_H) */

#endif /* (__SMP__) */
