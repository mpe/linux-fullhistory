#ifndef _ALPHA_SEMAPHORE_H
#define _ALPHA_SEMAPHORE_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1996, 2000 Richard Henderson
 */

#include <asm/current.h>
#include <asm/system.h>
#include <asm/atomic.h>

struct semaphore {
	/* Careful, inline assembly knows about the position of these two.  */
	atomic_t count;
	atomic_t waking;		/* biased by -1 */
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
};

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name)		, (long)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

#define __SEMAPHORE_INITIALIZER(name,count)		\
	{ ATOMIC_INIT(count), ATOMIC_INIT(-1),		\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
	  __SEM_DEBUG_INIT(name) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

extern inline void sema_init(struct semaphore *sem, int val)
{
	/*
	 * Logically, 
	 *   *sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
	 * except that gcc produces better initializing by parts yet.
	 */

	atomic_set(&sem->count, val);
	atomic_set(&sem->waking, -1);
	init_waitqueue_head(&sem->wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
#endif
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}


extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern int  __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/* All have custom assembly linkages.  */
extern void __down_failed(struct semaphore * sem);
extern void __down_failed_interruptible(struct semaphore * sem);
extern void __down_failed_trylock(struct semaphore * sem);
extern void __up_wakeup(struct semaphore * sem);

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

	/* This little bit of silliness is to get the GP loaded for
	   a function that ordinarily wouldn't.  Otherwise we could
	   have it done by the macro directly, which can be optimized
	   the linker.  */
	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	
	pv = __down_failed;
	__asm__ __volatile__ (
		"/* semaphore down operation */\n"
		"1:	ldl_l	$24,%1\n"
		"	subl	$24,1,$28\n"
		"	subl	$24,1,$24\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	blt	$24,3f\n"
		"4:	mb\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%1\n"
		"	jsr	$28,($27),__down_failed\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(pv)
		: "m"(sem->count), "r"(pv)
		: "$24", "$28", "memory");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	/* __down_failed_interruptible takes the semaphore address in $24,
	   and it's return address in $28.  The pv is loaded as usual.
	   The gp is clobbered (in the module case) as usual.  The return
	   value is in $24.  */

	register int ret __asm__("$24");
	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	
	pv = __down_failed_interruptible;
	__asm__ __volatile__ (
		"/* semaphore down interruptible operation */\n"
		"1:	ldl_l	$24,%2\n"
		"	subl	$24,1,$28\n"
		"	subl	$24,1,$24\n"
		"	stl_c	$28,%2\n"
		"	beq	$28,2f\n"
		"	blt	$24,3f\n"
		"	mov	$31,%0\n"
		"4:	mb\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%2\n"
		"	jsr	$28,($27),__down_failed_interruptible\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(ret), "=r"(pv)
		: "m"(sem->count), "r"(pv)
		: "$28", "memory");

	return ret;
}

/*
 * down_trylock returns 0 on success, 1 if we failed to get the lock.
 *
 * We must manipulate count and waking simultaneously and atomically.
 * Do this by using ll/sc on the pair of 32-bit words.
 */

extern inline int down_trylock(struct semaphore * sem)
{
	long ret, tmp, tmp2, sub;

	/* "Equivalent" C.  Note that we have to do this all without
	   (taken) branches in order to be a valid ll/sc sequence.

	   do {
	       tmp = ldq_l;
	       sub = 0x0000000100000000;
	       ret = ((int)tmp <= 0);		// count =< 0 ?
	       if ((int)tmp >= 0) sub = 0;	// count >= 0 ?
			// note that if count=0 subq overflows to the high
			// longword (i.e waking)
	       ret &= ((long)tmp < 0);		// waking < 0 ?
	       sub += 1;
	       if (ret) 
			break;	
	       tmp -= sub;
	       tmp = stq_c = tmp;
	   } while (tmp == 0);
	*/

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	
	__asm__ __volatile__(
		"1:	ldq_l	%1,%4\n"
		"	lda	%3,1\n"
		"	addl	%1,0,%2\n"
		"	sll	%3,32,%3\n"
		"	cmple	%2,0,%0\n"
		"	cmovge	%2,0,%3\n"
		"	cmplt	%1,0,%2\n"
		"	addq	%3,1,%3\n"
		"	and	%0,%2,%0\n"
		"	bne	%0,2f\n"
		"	subq	%1,%3,%1\n"
		"	stq_c	%1,%4\n"
		"	beq	%1,3f\n"
		"2:\n"
		".section .text2,\"ax\"\n"
		"3:	br	1b\n"
		".previous"
		: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(sub)
		: "m"(*sem)
		: "memory");

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

	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	
	pv = __up_wakeup;
	__asm__ __volatile__ (
		"/* semaphore up operation */\n"
		"	mb\n"
		"1:	ldl_l	$24,%1\n"
		"	addl	$24,1,$28\n"
		"	addl	$24,1,$24\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	ble	$24,3f\n"
		"4:\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%1\n"
		"	jsr	$28,($27),__up_wakeup\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(pv)
		: "m"(sem->count), "r"(pv)
		: "$24", "$28", "memory");
}


/* rw mutexes (should that be mutices? =) -- throw rw
 * spinlocks and semaphores together, and this is what we
 * end up with...
 *
 * The lock is initialized to BIAS.  This way, a writer
 * subtracts BIAS ands gets 0 for the case of an uncontended
 * lock.  Readers decrement by 1 and see a positive value
 * when uncontended, negative if there are writers waiting
 * (in which case it goes to sleep).
 *
 * The value 0x01000000 supports up to 128 processors and
 * lots of processes.  BIAS must be chosen such that subtracting
 * BIAS once per CPU will result in the int remaining
 * negative.
 * In terms of fairness, this should result in the lock
 * flopping back and forth between readers and writers
 * under heavy use.
 *
 *	      -ben
 *
 * Once we start supporting machines with more than 128 CPUs,
 * we should go for using a 64bit atomic type instead of 32bit
 * as counter. We shall probably go for bias 0x80000000 then,
 * so that single sethi can set it.
 *
 *	      -jj
 */

#define RW_LOCK_BIAS		0x01000000

struct rw_semaphore {
	int			count;
	/* bit 0 means read bias granted;
	   bit 1 means write bias granted.  */
	unsigned		granted;
	wait_queue_head_t	wait;
	wait_queue_head_t	write_bias_wait;
#if WAITQUEUE_DEBUG
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

#if WAITQUEUE_DEBUG
#define __RWSEM_DEBUG_INIT	, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif

#define __RWSEM_INITIALIZER(name,count)					\
	{ (count), 0, __WAIT_QUEUE_HEAD_INITIALIZER((name).wait),	\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).write_bias_wait)		\
	  __SEM_DEBUG_INIT(name) __RWSEM_DEBUG_INIT }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) \
	__DECLARE_RWSEM_GENERIC(name, RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) \
	__DECLARE_RWSEM_GENERIC(name, RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) \
	__DECLARE_RWSEM_GENERIC(name, 0)

extern inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RW_LOCK_BIAS;
	sem->granted = 0;
	init_waitqueue_head(&sem->wait);
	init_waitqueue_head(&sem->write_bias_wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

/* All have custom assembly linkages.  */
extern void __down_read_failed(struct rw_semaphore *sem);
extern void __down_write_failed(struct rw_semaphore *sem);
extern void __rwsem_wake(struct rw_semaphore *sem, unsigned long readers);

extern inline void down_read(struct rw_semaphore *sem)
{
	/* Given that we have to use particular hard registers to 
	   communicate with __down_read_failed anyway, reuse them in 
	   the atomic operation as well. 

	   __down_read_failed takes the semaphore address in $24, the count
	   we read in $25, and it's return address in $28. The pv is loaded
	   as usual. The gp is clobbered (in the module case) as usual.  */

	/* This little bit of silliness is to get the GP loaded for
	   a function that ordinarily wouldn't.  Otherwise we could
	   have it done by the macro directly, which can be optimized
	   the linker.  */
	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	pv = __down_read_failed;
	__asm__ __volatile__(
		"/* semaphore down_read operation */\n"
		"1:	ldl_l	$24,%1\n"
		"	subl	$24,1,$28\n"
		"	subl	$24,1,$25\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	blt	$25,3f\n"
		"4:	mb\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%1\n"
		"	jsr	$28,($27),__down_read_failed\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(pv)
		: "m"(sem->count), "r"(pv)
		: "$24", "$25", "$28", "memory");

#if WAITQUEUE_DEBUG
	if (sem->granted & 2)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_inc(&sem->readers);
#endif
}

extern inline void down_write(struct rw_semaphore *sem)
{
	/* Given that we have to use particular hard registers to 
	   communicate with __down_write_failed anyway, reuse them in 
	   the atomic operation as well. 

	   __down_write_failed takes the semaphore address in $24, the count
	   we read in $25, and it's return address in $28. The pv is loaded
	   as usual. The gp is clobbered (in the module case) as usual.  */

	/* This little bit of silliness is to get the GP loaded for
	   a function that ordinarily wouldn't.  Otherwise we could
	   have it done by the macro directly, which can be optimized
	   the linker.  */
	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	pv = __down_write_failed;
	__asm__ __volatile__(
		"/* semaphore down_write operation */\n"
		"1:	ldl_l	$24,%1\n"
		"	ldah	$28,%3($24)\n"
		"	ldah	$25,%3($24)\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	bne	$25,3f\n"
		"4:	mb\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%1\n"
		"	jsr	$28,($27),__down_write_failed\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(pv)
		: "m"(sem->count), "r"(pv), "i"(-(RW_LOCK_BIAS >> 16))
		: "$24", "$25", "$28", "memory");

#if WAITQUEUE_DEBUG
	if (atomic_read(&sem->writers))
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (sem->granted & 3)
		BUG();
	atomic_inc(&sem->writers);
#endif
}

/* When a reader does a release, the only significant case is when
  there was a writer waiting, and we've * bumped the count to 0: we must
wake the writer up.  */

extern inline void up_read(struct rw_semaphore *sem)
{
	/* Given that we have to use particular hard registers to 
	   communicate with __rwsem_wake anyway, reuse them in 
	   the atomic operation as well. 

	   __rwsem_wake takes the semaphore address in $24, the
	   number of waiting readers in $25, and it's return address
	   in $28.  The pv is loaded as usual. The gp is clobbered
	   (in the module case) as usual.  */

	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
	if (sem->granted & 2)
		BUG();
	if (atomic_read(&sem->writers))
		BUG();
	atomic_dec(&sem->readers);
#endif

	pv = __rwsem_wake;
	__asm__ __volatile__(
		"/* semaphore up_read operation */\n"
		"	mb\n"
		"1:	ldl_l	$24,%1\n"
		"	addl	$24,1,$28\n"
		"	addl	$24,1,$24\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	beq	$24,3f\n"
		"4:\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	lda	$24,%1\n"
		"	mov	0,$25\n"
		"	jsr	$28,($27),__rwsem_wake\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(pv)
		: "m"(sem->count), "r"(pv)
		: "$24", "$25", "$28", "memory");
}

/* releasing the writer is easy -- just release it and
 * wake up any sleepers.
 */
extern inline void up_write(struct rw_semaphore *sem)
{
	/* Given that we have to use particular hard registers to 
	   communicate with __rwsem_wake anyway, reuse them in 
	   the atomic operation as well. 

	   __rwsem_wake takes the semaphore address in $24, the
	   number of waiting readers in $25, and it's return address
	   in $28.  The pv is loaded as usual. The gp is clobbered
	   (in the module case) as usual.  */

	register void *pv __asm__("$27");

#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
	if (sem->granted & 3)
		BUG();
	if (atomic_read(&sem->readers))
		BUG();
	if (atomic_read(&sem->writers) != 1)
		BUG();
	atomic_dec(&sem->writers);
#endif

	pv = __rwsem_wake;
	__asm__ __volatile__(
		"/* semaphore up_write operation */\n"
		"	mb\n"
		"1:	ldl_l	$24,%1\n"
		"	ldah	$28,%3($24)\n"
		"	stl_c	$28,%1\n"
		"	beq	$28,2f\n"
		"	blt	$24,3f\n"
		"4:\n"
		".section .text2,\"ax\"\n"
		"2:	br	1b\n"
		"3:	ldah	$25,%3($24)\n"
		/* Only do the wake if we're no longer negative.  */
		"	blt	$25,4b\n"
		"	lda	$24,%1\n"
		"	jsr	$28,($27),__rwsem_wake\n"
		"	ldgp	$29,0($28)\n"
		"	br	4b\n"
		".previous"
		: "=r"(pv)
		: "m"(sem->count), "r"(pv), "i"(RW_LOCK_BIAS >> 16)
		: "$24", "$25", "$28", "memory");
}

#endif
