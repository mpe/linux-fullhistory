#ifndef _ALPHA_SEMAPHORE_HELPER_H
#define _ALPHA_SEMAPHORE_HELPER_H

/*
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1999 Richard Henderson
 */

/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * which we have.  Let the rest of the losers suck eggs.
 */

static inline void
wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int
waking_non_zero(struct semaphore *sem)
{
	long ret, tmp;

	/* An atomic conditional decrement.  */
	__asm__ __volatile__(
		"1:	ldl_l	%1,%2\n"
		"	blt	%1,2f\n"
		"	subl	%1,1,%0\n"
		"	stl_c	%0,%2\n"
		"	beq	%0,3f\n"
		"2:\n"
		".subsection 2\n"
		"3:	br	1b\n"
		".previous"
		: "=r"(ret), "=r"(tmp), "=m"(sem->waking.counter)
		: "0"(0));

	return ret > 0;
}


/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 *
 * We must undo the sem->count down_interruptible decrement
 * simultaneously and atomicly with the sem->waking adjustment,
 * otherwise we can race with wake_one_more.
 *
 * This is accomplished by doing a 64-bit ll/sc on the 2 32-bit words.
 */

static inline int
waking_non_zero_interruptible(struct semaphore *sem, struct task_struct *tsk)
{
	long ret, tmp, tmp2, tmp3;

	/* "Equivalent" C.  Note that we have to do this all without
	   (taken) branches in order to be a valid ll/sc sequence.

	   do {
	       tmp = ldq_l;
	       ret = 0;
	       if (tmp >= 0) {
	           tmp += 0xffffffff00000000;
	           ret = 1;
	       }
	       else if (pending) {
		   // Since -1 + 1 carries into the high word, we have
		   // to be more careful adding 1 here.
		   tmp = (tmp & 0xffffffff00000000)
			 | ((tmp + 1) & 0x00000000ffffffff;
	           ret = -EINTR;
	       }
	       else {
		   break;	// ideally.  we don't actually break 
		   		// since this is a predicate we don't
				// have, and is more trouble to build
				// than to elide the noop stq_c.
	       }
	       tmp = stq_c = tmp;
	   } while (tmp == 0);
	*/

	__asm__ __volatile__(
		"1:	ldq_l	%1,%4\n"
		"	lda	%0,0\n"
		"	cmovne	%5,%6,%0\n"
		"	addq	%1,1,%2\n"
		"	and	%1,%7,%3\n"
		"	andnot	%2,%7,%2\n"
		"	cmovge	%1,1,%0\n"
		"	or	%3,%2,%2\n"
		"	addq	%1,%7,%3\n"
		"	cmovne	%5,%2,%1\n"
		"	cmovge	%2,%3,%1\n"
		"	stq_c	%1,%4\n"
		"	beq	%1,3f\n"
		"2:\n"
		".subsection 2\n"
		"3:	br	1b\n"
		".previous"
		: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(tmp3), "=m"(*sem)
		: "r"(signal_pending(tsk)), "r"(-EINTR),
		  "r"(0xffffffff00000000));

	return ret;
}

/*
 * waking_non_zero_trylock is unused.  we do everything in 
 * down_trylock and let non-ll/sc hosts bounce around.
 */

static inline int
waking_non_zero_trylock(struct semaphore *sem)
{
	return 0;
}

#endif
