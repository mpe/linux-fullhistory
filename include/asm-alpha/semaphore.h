#ifndef _ALPHA_SEMAPHORE_H
#define _ALPHA_SEMAPHORE_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 */

#include <asm/current.h>
#include <asm/system.h>
#include <asm/atomic.h>

/*
 * Semaphores are recursive: we allow the holder process to recursively do
 * down() operations on a semaphore that the process already owns. In order
 * to do that, we need to keep a semaphore-local copy of the owner and the
 * "depth of ownership".
 *
 * NOTE! Nasty memory ordering rules:
 *  - "owner" and "owner_count" may only be modified once you hold the lock.
 *  - "owner_count" must be written _after_ modifying owner, and must be
 *  read _before_ reading owner. There must be appropriate write and read
 *  barriers to enforce this.
 */

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct task_struct *owner;
	long owner_depth;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) \
 { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL, 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) \
 { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL, 1, NULL })

#define semaphore_owner(sem)	((sem)->owner)
#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/* All three have custom assembly linkages.  */
extern void __down_failed(struct semaphore * sem);
extern void __down_failed_interruptible(struct semaphore * sem);
extern void __up_wakeup(struct semaphore * sem);


/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * which we have.  Let the rest of the losers suck eggs.
 *
 * Tricky bits --
 *
 * (1) One task does two downs, no other contention
 *	initial state:
 *		count = 1, waking = 0, depth = undef;
 *	down(&sem)
 *		count = 0, waking = 0, depth = 1;
 *	down(&sem)
 *		atomic dec and test sends us to waking_non_zero via __down
 *			count = -1, waking = 0;
 *		conditional atomic dec on waking discovers no free slots
 *			count = -1, waking = 0;
 *		test for owner succeeeds and we return ok.
 *			count = -1, waking = 0, depth = 2;
 *	up(&sem)
 *		dec depth
 *			count = -1, waking = 0, depth = 1;
 *		atomic inc and test sends us to slow path
 *			count = 0, waking = 0, depth = 1;
 *		notice !(depth < 0) and don't call __up.
 *	up(&sem)
 *		dec depth
 *			count = 0, waking = 0, depth = 0;
 *		atomic inc and test succeeds.
 *			count = 1, waking = 0, depth = 0;
 */

static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int waking_non_zero(struct semaphore *sem,
				  struct task_struct *tsk)
{
	long owner_depth;
	int ret, tmp;

	owner_depth = sem->owner_depth;
	
	/* Atomic decrement, iff the value is > 0.  */
	__asm__ __volatile__(
	"1:	ldl_l	%1,%2\n"
	"	ble	%1,2f\n"
	"	subl	%1,1,%0\n"
	"	stl_c	%0,%2\n"
	"	beq	%0,3f\n"
	"2:	mb\n"
	".section .text2,\"ax\"\n"
	"3:	br	1b\n"
	".previous"
	: "=r"(ret), "=r"(tmp), "=m"(__atomic_fool_gcc(&sem->waking))
	: "0"(0));

	ret |= ((owner_depth != 0) & (sem->owner == tsk));
	if (ret) {
		sem->owner = tsk;
		wmb();
		/* Don't use the old value, which is stale in the
		   !owner case.  */
		sem->owner_depth++;
	}

	return ret;
}

/*
 * Whee.  Hidden out of line code is fun.  The contention cases are
 * handled out of line in kernel/sched.c; arch/alpha/lib/semaphore.S
 * takes care of making sure we can call it without clobbering regs.
 */

extern inline void down(struct semaphore * sem)
{
	/* Given that we have to use particular hard registers to 
	   communicate with __down_failed anyway, reuse them in 
	   the atomic operation as well. 

	   __down_failed takes the semaphore address in $24, and
	   it's return address in $28.  The pv is loaded as usual.
	   The gp is clobbered (in the module case) as usual.  */

	__asm__ __volatile__ (
		"/* semaphore down operation */\n"
		"1:	ldl_l	$27,%3\n"
		"	subl	$27,1,$27\n"
		"	mov	$27,$28\n"
		"	stl_c	$28,%0\n"
		"	beq	$28,2f\n"
		"	blt	$27,3f\n"
		/* Got the semaphore no contention.  Set owner and depth.  */
		"	stq	$8,%1\n"
		"	lda	$28,1\n"
		"	wmb\n"
		"	stq	$28,%2\n"
		"4:	mb\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%3\n"
		"	jsr	$28,__down_failed\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=m"(sem->count), "=m"(sem->owner), "=m"(sem->owner_depth)
		: "m"(sem->count)
		: "$24", "$27", "$28", "memory");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	/* __down_failed_interruptible takes the semaphore address in $24,
	   and it's return address in $28.  The pv is loaded as usual.
	   The gp is clobbered (in the module case) as usual.  The return
	   value is in $24.  */

	register int ret __asm__("$24");

	__asm__ __volatile__ (
		"/* semaphore down interruptible operation */\n"
		"1:	ldl_l	$27,%4\n"
		"	subl	$27,1,$27\n"
		"	mov	$27,$28\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	blt	$27,3f\n"
		/* Got the semaphore no contention.  Set owner and depth.  */
		"	stq	$8,%2\n"
		"	lda	$28,1\n"
		"	wmb\n"
		"	stq	$28,%3\n"
		"	mov	$31,$24\n"
		"4:	mb\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%4\n"
		"	jsr	$28,__down_failed_interruptible\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(ret), "=m"(sem->count), "=m"(sem->owner),
		  "=m"(sem->owner_depth)
		: "m"(sem->count)
		: "$27", "$28", "memory");

	return ret;
}

extern inline void up(struct semaphore * sem)
{
	/* Given that we have to use particular hard registers to 
	   communicate with __up_wakeup anyway, reuse them in 
	   the atomic operation as well. 

	   __up_wakeup takes the semaphore address in $24, and
	   it's return address in $28.  The pv is loaded as usual.
	   The gp is clobbered (in the module case) as usual.  */

	__asm__ __volatile__ (
		"/* semaphore up operation */\n"
		"	mb\n"
		"1:	ldl_l	$27,%1\n"
		"	addl	$27,1,$27\n"
		"	mov	$27,$28\n"
		"	stl_c	$28,%0\n"
		"	beq	$28,2f\n"
		"	mb\n"
		"	ble	$27,3f\n"
		"4:\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%1\n"
		"	bgt	%2,4b\n"
		"	jsr	$28,__up_wakeup\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=m"(sem->count)
		: "m"(sem->count), "r"(--sem->owner_depth)
		: "$24", "$27", "$28", "memory");
}

#endif
