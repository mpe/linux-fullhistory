#ifndef _ASM_IA64_SPINLOCK_H
#define _ASM_IA64_SPINLOCK_H

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * This file is used for SMP configurations only.
 */

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

typedef struct { 
	volatile unsigned int lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED			(spinlock_t) { 0 }
#define spin_lock_init(x)			((x)->lock = 0) 

/*
 * Streamlined test_and_set_bit(0, (x)).  We use test-and-test-and-set
 * rather than a simple xchg to avoid writing the cache-line when
 * there is contention.
 */
#if 1 /* Bad code generation? */
#define spin_lock(x) __asm__ __volatile__ ( \
       "mov ar.ccv = r0\n" \
       "mov r29 = 1\n" \
       ";;\n" \
       "1:\n" \
       "ld4 r2 = %0\n" \
       ";;\n" \
       "cmp4.eq p0,p7 = r0,r2\n" \
       "(p7) br.cond.spnt.few 1b \n" \
       "cmpxchg4.acq r2 = %0, r29, ar.ccv\n" \
       ";;\n" \
       "cmp4.eq p0,p7 = r0, r2\n" \
       "(p7) br.cond.spnt.few 1b\n" \
       ";;\n" \
       :: "m" __atomic_fool_gcc((x)) : "r2", "r29")
 
#else 
#define spin_lock(x)					\
{							\
	spinlock_t *__x = (x);				\
							\
	do {						\
		while (__x->lock);			\
	} while (cmpxchg_acq(&__x->lock, 0, 1));	\
}
#endif

#define spin_is_locked(x)	((x)->lock != 0)

#define spin_unlock(x)		(((spinlock_t *) x)->lock = 0)

/* Streamlined !test_and_set_bit(0, (x)) */
#define spin_trylock(x)						\
({								\
	spinlock_t *__x = (x);					\
	__u32 old;						\
								\
	do {							\
		old = __x->lock;				\
	} while (cmpxchg_acq(&__x->lock, old, 1) != old);	\
	old == 0;						\
})

#define spin_unlock_wait(x) \
	({ do { barrier(); } while(((volatile spinlock_t *)x)->lock); })

typedef struct {
	volatile int read_counter:31;
	volatile int write_lock:1;
} rwlock_t;
#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }

#define read_lock(rw)									\
do {											\
	int tmp = 0;									\
	__asm__ __volatile__ ("1:\tfetchadd4.acq %0 = %1, 1\n"				\
			      ";;\n"							\
			      "tbit.nz p6,p0 = %0, 31\n"				\
			      "(p6) br.cond.sptk.few 2f\n"				\
			      ".section .text.lock,\"ax\"\n"				\
			      "2:\tfetchadd4.rel %0 = %1, -1\n"				\
			      ";;\n"							\
			      "3:\tld4.acq %0 = %1\n"					\
			      ";;\n"							\
			      "tbit.nz p6,p0 = %0, 31\n"				\
			      "(p6) br.cond.sptk.few 3b\n"				\
			      "br.cond.sptk.few 1b\n"					\
			      ";;\n"							\
			      ".previous\n": "=r" (tmp), "=m" (__atomic_fool_gcc(rw)));	\
} while(0)

#define read_unlock(rw)								\
do {										\
	int tmp = 0;								\
	__asm__ __volatile__ ("fetchadd4.rel %0 = %1, -1\n"			\
			      : "=r" (tmp) : "m" (__atomic_fool_gcc(rw)));	\
} while(0)

#define write_lock(rw)				\
while(1) {					\
	do {					\
	} while (!test_and_set_bit(31, (rw)));	\
	if ((rw)->read_counter) {		\
		clear_bit(31, (rw));		\
		while ((rw)->read_counter)	\
			;			\
	} else {				\
		break;				\
	}					\
}

#define write_unlock(x)				(clear_bit(31, (x)))

#endif /*  _ASM_IA64_SPINLOCK_H */
