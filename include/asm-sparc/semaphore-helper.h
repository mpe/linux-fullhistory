#ifndef _SPARC_SEMAPHORE_HELPER_H
#define _SPARC_SEMAPHORE_HELPER_H

/*
 * (barely) SMP- and interrupt-safe semaphore helper functions, sparc version.
 *
 * (C) Copyright 1999 David S. Miller (davem@redhat.com)
 * (C) Copyright 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */
#define wake_one_more(sem)	atomic_inc(&(sem)->waking)
static __inline__ int waking_non_zero(struct semaphore *sem)
{
	int ret;

#ifdef __SMP__
	int tmp;

	__asm__ __volatile__("
		rd	%%psr, %%g1
		or	%%g1, %3, %0
		wr	%0, 0x0, %%psr
		nop; nop; nop;
1:		ldstub	[%2 + 3], %0
		tst	%0
		bne	1b
		ld	[%2], %0
		andn	%0, 0xff, %1
		subcc	%0, 0x1ff, %0
		bl,a	1f
		 mov	0, %0
		mov	%0, %1
		mov	1, %0
1:		st	%1, [%2]
		wr	%%g1, 0x0, %%psr
		nop; nop; nop\n" 
	: "=&r" (ret), "=&r" (tmp)
	: "r" (&sem->waking), "i" (PSR_PIL)
	: "g1", "memory", "cc");
#else
	__asm__ __volatile__("
		rd	%%psr, %%g1
		or	%%g1, %2, %0
		wr	%0, 0x0, %%psr
		nop; nop; nop;
		ld	[%1], %0
		subcc	%0, 1, %0
		bl,a	1f
		 mov	0, %0
		st	%0, [%1]
		mov	1, %0
1:		wr	%%g1, 0x0, %%psr
		nop; nop; nop\n" 
	: "=&r" (ret)
	: "r" (&sem->waking), "i" (PSR_PIL)
	: "g1", "memory", "cc");
#endif
	return ret;
}

static __inline__ int waking_non_zero_interruptible(struct semaphore *sem,
						    struct task_struct *tsk)
{
	int ret;

#ifdef __SMP__
	int tmp;

	__asm__ __volatile__("
		rd	%%psr, %%g1
		or	%%g1, %3, %0
		wr	%0, 0x0, %%psr
		nop; nop; nop;
1:		ldstub	[%2 + 3], %0
		tst	%0
		bne	1b
		ld	[%2], %0
		andn	%0, 0xff, %1
		subcc	%0, 0x1ff, %0
		bl,a	1f
		 mov	0, %0
		mov	%0, %1
		mov	1, %0
1:		st	%1, [%2]
		wr	%%g1, 0x0, %%psr
		nop; nop; nop\n" 
	: "=&r" (ret), "=&r" (tmp)
	: "r" (&sem->waking), "i" (PSR_PIL)
	: "g1", "memory", "cc");
#else
	__asm__ __volatile__("
		rd	%%psr, %%g1
		or	%%g1, %2, %0
		wr	%0, 0x0, %%psr
		nop; nop; nop;
		ld	[%1], %0
		subcc	%0, 1, %0
		bl,a	1f
		 mov	0, %0
		st	%0, [%1]
		mov	1, %0
1:		wr	%%g1, 0x0, %%psr
		nop; nop; nop\n" 
	: "=&r" (ret)
	: "r" (&sem->waking), "i" (PSR_PIL)
	: "g1", "memory", "cc");
#endif
	if(ret == 0 && signal_pending(tsk)) {
		atomic_inc(&sem->count);
		ret = -EINTR;
	}
	return ret;
}

static __inline__ int waking_non_zero_trylock(struct semaphore *sem)
{
	int ret;

#ifdef __SMP__
	int tmp;

	__asm__ __volatile__("
		rd	%%psr, %%g1
		or	%%g1, %3, %0
		wr	%0, 0x0, %%psr
		nop; nop; nop;
1:		ldstub	[%2 + 3], %0
		tst	%0
		bne	1b
		ld	[%2], %0
		andn	%0, 0xff, %1
		subcc	%0, 0x1ff, %0
		bl,a	1f
		 mov	0, %0
		mov	%0, %1
		mov	1, %0
1:		st	%1, [%2]
		wr	%%g1, 0x0, %%psr
		nop; nop; nop\n" 
	: "=&r" (ret), "=&r" (tmp)
	: "r" (&sem->waking), "i" (PSR_PIL)
	: "g1", "memory", "cc");
#else
	__asm__ __volatile__("
		rd	%%psr, %%g1
		or	%%g1, %2, %0
		wr	%0, 0x0, %%psr
		nop; nop; nop;
		ld	[%1], %0
		subcc	%0, 1, %0
		bl,a	1f
		 mov	0, %0
		st	%0, [%1]
		mov	1, %0
1:		wr	%%g1, 0x0, %%psr
		nop; nop; nop\n" 
	: "=&r" (ret)
	: "r" (&sem->waking), "i" (PSR_PIL)
	: "g1", "memory", "cc");
#endif
	ret = !ret;
	if(ret == 1)
		atomic_inc(&sem->count);
	return ret;
}

#endif /* !(_SPARC_SEMAPHORE_HELPER_H) */

