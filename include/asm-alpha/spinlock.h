#ifndef _ALPHA_SPINLOCK_H
#define _ALPHA_SPINLOCK_H

#ifndef __SMP__

/* gcc 2.7.2 can crash initializing an empty structure.  */
typedef struct { int dummy; } spinlock_t;
#define SPIN_LOCK_UNLOCKED { 0 }

#define spin_lock_init(lock)			do { } while(0)
#define spin_lock(lock)				do { } while(0)
#define spin_trylock(lock)			do { } while(0)
#define spin_unlock_wait(lock)			do { } while(0)
#define spin_unlock(lock)			do { } while(0)
#define spin_lock_irq(lock)			setipl(7)
#define spin_unlock_irq(lock)			setipl(0)

#define spin_lock_irqsave(lock, flags)		do { (flags) = swpipl(7); } while (0)
#define spin_unlock_irqrestore(lock, flags)	setipl(flags)

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */
typedef struct { int dummy; } rwlock_t;
#define RW_LOCK_UNLOCKED { 0 }

#define read_lock(lock)		do { } while(0)
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	do { } while(0)
#define write_unlock(lock)	do { } while(0)
#define read_lock_irq(lock)	cli()
#define read_unlock_irq(lock)	sti()
#define write_lock_irq(lock)	cli()
#define write_unlock_irq(lock)	sti()

#define read_lock_irqsave(lock, flags)		do { (flags) = swpipl(7); } while (0)
#define read_unlock_irqrestore(lock, flags)	setipl(flags)
#define write_lock_irqsave(lock, flags)		do { (flags) = swpipl(7); } while (0)
#define write_unlock_irqrestore(lock, flags)	setipl(flags)

#else /* __SMP__ */

#include <linux/kernel.h>
#include <asm/current.h>

/* Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned long lock;
	unsigned long previous;
	unsigned long task;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0, 0 }

#define spin_lock_init(x) \
	do { (x)->lock = 0; (x)->previous = 0; } while(0)
#define spin_unlock_wait(x) \
	do { barrier(); } while(((volatile spinlock_t *)x)->lock)

typedef struct { unsigned long a[100]; } __dummy_lock_t;
#define __dummy_lock(lock) (*(__dummy_lock_t *)(lock))

static inline void spin_unlock(spinlock_t * lock)
{
	__asm__ __volatile__(
	"mb; stq $31,%0"
	:"=m" (__dummy_lock(lock)));
}

#if 1
#define DEBUG_SPINLOCK
#else
#undef DEBUG_SPINLOCK
#endif

#ifdef DEBUG_SPINLOCK
extern void spin_lock(spinlock_t * lock);
#else
static inline void spin_lock(spinlock_t * lock)
{
	long tmp;

	/* Use sub-sections to put the actual loop at the end
	   of this object file's text section so as to perfect
	   branch prediction.  */
	__asm__ __volatile__(
	"1:	ldq_l	%0,%1\n"
	"	blbs	%0,2f\n"
	"	or	%0,1,%0\n"
	"	stq_c	%0,%1\n"
	"	beq	%0,2f\n"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"2:	ldq	%0,%1\n"
	"	blbs	%0,2b\n"
	"	br	1b\n"
	".previous"
	: "=r" (tmp),
	  "=m" (__dummy_lock(lock)));
}
#endif /* DEBUG_SPINLOCK */

#define spin_trylock(lock) (!test_and_set_bit(0,(lock)))

#define spin_lock_irq(lock) \
	do { __cli(); spin_lock(lock); } while (0)

#define spin_unlock_irq(lock) \
	do { spin_unlock(lock); __sti(); } while (0)

#define spin_lock_irqsave(lock, flags) \
	do { __save_and_cli(flags); spin_lock(lock); } while (0)

#define spin_unlock_irqrestore(lock, flags) \
	do { spin_unlock(lock); __restore_flags(flags); } while (0)

/***********************************************************/

#if 1
#define DEBUG_RWLOCK
#else
#undef DEBUG_RWLOCK
#endif

typedef struct { volatile int write_lock:1, read_counter:31; } rwlock_t;

#define RW_LOCK_UNLOCKED { 0, 0 }

#ifdef DEBUG_RWLOCK
extern void write_lock(rwlock_t * lock);
#else
static inline void write_lock(rwlock_t * lock)
{
	long regx, regy;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	or	%1,1,%2;"
	"	stl_c	%2,%0;"
	"	beq	%2,6f;"
	"	blt	%1,8f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	ldl	%1,%0;"
	"	blbs	%1,6b;"
	"	br	1b;"
	"8:	ldl	%1,%0;"
	"	blt	%1,8b;"
	"9:	br	4b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx), "=&r" (regy)
	: "0" (__dummy_lock(lock))
	);
}
#endif /* DEBUG_RWLOCK */

static inline void write_unlock(rwlock_t * lock)
{
	__asm__ __volatile__("mb; stl $31,%0" : "=m" (__dummy_lock(lock)));
}

#ifdef DEBUG_RWLOCK
extern void _read_lock(rwlock_t * lock);
#else
static inline void _read_lock(rwlock_t * lock)
{
	long regx;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	subl	%1,2,%1;"
	"	stl_c	%1,%0;"
	"	beq	%1,6f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	ldl	%1,%0;"
	"	blbs	%1,6b;"
	"	br	1b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx)
	: "0" (__dummy_lock(lock))
	);
}
#endif /* DEBUG_RWLOCK */

#define read_lock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_read_lock(lock); \
	__restore_flags(flags); \
} while(0)

static inline void _read_unlock(rwlock_t * lock)
{
	long regx;
	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	addl	%1,2,%1;"
	"	stl_c	%1,%0;"
	"	beq	%1,6f;"
	".section .text2,\"ax\"\n"
	"6:	br	1b\n"
	".previous"
	: "=m" (__dummy_lock(lock)), "=&r" (regx)
	: "0" (__dummy_lock(lock)));
}

#define read_unlock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_read_unlock(lock); \
	__restore_flags(flags); \
} while(0)

#define read_lock_irq(lock)	do { __cli(); _read_lock(lock); } while (0)
#define read_unlock_irq(lock)	do { _read_unlock(lock); __sti(); } while (0)
#define write_lock_irq(lock)	do { __cli(); write_lock(lock); } while (0)
#define write_unlock_irq(lock)	do { write_unlock(lock); __sti(); } while (0)

#define read_lock_irqsave(lock, flags)	\
	do { __save_and_cli(flags); _read_lock(lock); } while (0)
#define read_unlock_irqrestore(lock, flags) \
	do { _read_unlock(lock); __restore_flags(flags); } while (0)
#define write_lock_irqsave(lock, flags)	\
	do { __save_and_cli(flags); write_lock(lock); } while (0)
#define write_unlock_irqrestore(lock, flags) \
	do { write_unlock(lock); __restore_flags(flags); } while (0)

#endif /* SMP */
#endif /* _ALPHA_SPINLOCK_H */
