/* $Id: semaphore-helper.h,v 1.3 1999/06/11 14:30:15 ralf Exp $
 *
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1999 Andrea Arcangeli
 * (C) Copyright 1999 Ralf Baechle
 */
#ifndef __ASM_MIPS_SEMAPHORE_HELPER_H
#define __ASM_MIPS_SEMAPHORE_HELPER_H

/*
 * These two _must_ execute atomically wrt each other.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int
waking_non_zero(struct semaphore *sem)
{
	int ret, tmp;

	__asm__ __volatile__(
	"1:\tll\t%1,%2\n\t"
	"blez\t%1,2f\n\t"
	"subu\t%0,%1,1\n\t"
	"sc\t%0,%2\n\t"
	"beqz\t%0,1b\n\t"
	"2:"
	".text"
	: "=r"(ret), "=r"(tmp), "=m"(__atomic_fool_gcc(&sem->waking))
	: "0"(0));

	return ret;
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
 *
 * This is crazy.  Normally it stricly forbidden to use 64-bit operation
 * in the 32-bit MIPS kernel.  In this case it's however ok because if an
 * interrupt has destroyed the upper half of registers sc will fail.
 */
static inline int
waking_non_zero_interruptible(struct semaphore *sem, struct task_struct *tsk)
{
	long ret, tmp;

#ifdef __MIPSEB__
        __asm__ __volatile__("
	.set	mips3
	.set	push
	.set	noat
0:	lld	%1,%2
	li	%0,0

	bltz	%1, 1f
	dli	$1, 0xffffffff00000000
	daddu	%1, $1
	li	%0, 1
	b	2f
1:

	beqz	%3, 1f
	addiu	$1, %1, 1
	dsll32	$1, $1, 0
	dsrl32	$1, $1, 0
	dsrl32	%1, %1, 0
	dsll32	%1, %1, 0
	or	%1, $1
	li	%0, %4
	b	2f
1:
	scd	%1, %2
2:
	beqz	%1,0b
	.set	pop
	.set	mips0"
	: "=&r"(ret), "=&r"(tmp), "=m"(*sem)
	: "r"(signal_pending(tsk)), "i"(-EINTR));
#endif

#ifdef __MIPSEL__
#error "FIXME: waking_non_zero_interruptible doesn't support little endian machines yet."
#endif

	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 *
 * XXX SMP ALERT
 */
#ifdef __SMP__
#error FIXME, waking_non_zero_trylock is broken for SMP.
#endif
static inline int waking_non_zero_trylock(struct semaphore *sem)
{
        int ret = 1;

	if (atomic_read(&sem->waking) <= 0)
		atomic_inc(&sem->count);
	else {
		atomic_dec(&sem->waking);
		ret = 0;
	}

	return ret;
}

#endif /* __ASM_MIPS_SEMAPHORE_HELPER_H */
