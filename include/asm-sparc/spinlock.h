/* spinlock.h: 32-bit Sparc spinlock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SPINLOCK_H
#define __SPARC_SPINLOCK_H

#ifndef __ASSEMBLY__

#ifndef __SMP__

#if (__GNUC__ > 2) || (__GNUC_MINOR__ >= 8)
  typedef struct { } spinlock_t;
  #define SPIN_LOCK_UNLOCKED { }
#else
  typedef unsigned char spinlock_t;
  #define SPIN_LOCK_UNLOCKED 0
#endif

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock_wait(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)
#define spin_lock_irq(lock)	cli()
#define spin_unlock_irq(lock)	sti()

#define spin_lock_irqsave(lock, flags)		save_and_cli(flags)
#define spin_unlock_irqrestore(lock, flags)	restore_flags(flags)

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
typedef struct { } rwlock_t;
#define RW_LOCK_UNLOCKED { }

#define read_lock(lock)		do { } while(0)
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	do { } while(0)
#define write_unlock(lock)	do { } while(0)
#define read_lock_irq(lock)	cli()
#define read_unlock_irq(lock)	sti()
#define write_lock_irq(lock)	cli()
#define write_unlock_irq(lock)	sti()

#define read_lock_irqsave(lock, flags)		save_and_cli(flags)
#define read_unlock_irqrestore(lock, flags)	restore_flags(flags)
#define write_lock_irqsave(lock, flags)		save_and_cli(flags)
#define write_unlock_irqrestore(lock, flags)	restore_flags(flags)

#else /* !(__SMP__) */

#include <asm/psr.h>

/* Define this to use the verbose/debugging versions in arch/sparc/lib/debuglocks.c */
/* #define SPIN_LOCK_DEBUG */

#ifdef SPIN_LOCK_DEBUG
struct _spinlock_debug {
	unsigned char lock;
	unsigned long owner_pc;
};
typedef struct _spinlock_debug spinlock_t;

#define SPIN_LOCK_UNLOCKED	{ 0, 0 }
#define spin_lock_init(lp)	do { (lp)->owner_pc = 0; (lp)->lock = 0; } while(0)
#define spin_unlock_wait(lp)	\
do { barrier();	\
} while(*(volatile unsigned char *)(&(lp)->lock))

extern void _spin_lock(spinlock_t *lock);
extern int _spin_trylock(spinlock_t *lock);
extern void _spin_unlock(spinlock_t *lock);
extern void _spin_lock_irq(spinlock_t *lock);
extern void _spin_unlock_irq(spinlock_t *lock);
extern void _spin_lock_irqsave(spinlock_t *lock);
extern void _spin_unlock_irqrestore(spinlock_t *lock);

#define spin_lock(lp)			_spin_lock(lp)
#define spin_trylock(lp)		_spin_trylock(lp)
#define spin_unlock(lp)			_spin_unlock(lp)
#define spin_lock_irq(lp)		_spin_lock_irq(lp)
#define spin_unlock_irq(lp)		_spin_unlock_irq(lp)
#define spin_lock_irqsave(lp, flags)	do { __save_and_cli(flags); \
					     _spin_lock_irqsave(lp); } while (0)
#define spin_unlock_irqrestore(lp, flags) do { _spin_unlock_irqrestore(lp); \
					       __restore_flags(flags); } while(0)

struct _rwlock_debug {
	volatile unsigned int lock;
	unsigned long owner_pc;
};
typedef struct _rwlock_debug rwlock_t;

#define RW_LOCK_UNLOCKED { 0, 0 }

extern void _read_lock(rwlock_t *rw);
extern void _read_unlock(rwlock_t *rw);
extern void _write_lock(rwlock_t *rw);
extern void _write_unlock(rwlock_t *rw);
extern void _read_lock_irq(rwlock_t *rw);
extern void _read_unlock_irq(rwlock_t *rw);
extern void _write_lock_irq(rwlock_t *rw);
extern void _write_unlock_irq(rwlock_t *rw);
extern void _read_lock_irqsave(rwlock_t *rw);
extern void _read_unlock_irqrestore(rwlock_t *rw);
extern void _write_lock_irqsave(rwlock_t *rw);
extern void _write_unlock_irqrestore(rwlock_t *rw);

#define read_lock(rw)		_read_lock(rw)
#define read_unlock(rw)		_read_unlock(rw)
#define write_lock(rw)		_write_lock(rw)
#define write_unlock(rw)	_write_unlock(rw)
#define read_lock_irq(rw)	_read_lock_irq(rw)
#define read_unlock_irq(rw)	_read_unlock_irq(rw)
#define write_lock_irq(rw)	_write_lock_irq(rw)
#define write_unlock_irq(rw)	_write_unlock_irq(rw)

#define read_lock_irqsave(rw, flags) \
do { __save_and_cli(flags); _read_lock_irqsave(rw); } while (0)

#define read_unlock_irqrestore(rw, flags) do { _read_unlock_irqrestore(rw); \
					       __restore_flags(flags); } while(0)

#define write_lock_irqsave(rw, flags) \
do { __save_and_cli(flags); _write_lock_irqsave(rw); } while(0)

#define write_unlock_irqrestore(rw, flags) do { _write_unlock_irqrestore(rw); \
					        __restore_flags(flags); } while(0)

#else /* !SPIN_LOCK_DEBUG */

typedef unsigned char spinlock_t;
#define SPIN_LOCK_UNLOCKED	0

#define spin_lock_init(lock)	(*(lock) = 0)
#define spin_unlock_wait(lock)	do { barrier(); } while(*(volatile spinlock_t *)lock)

extern __inline__ void spin_lock(spinlock_t *lock)
{
	__asm__ __volatile__("
1:	ldstub	[%0], %%g2
	orcc	%%g2, 0x0, %%g0
	bne,a	2f
	 ldub	[%0], %%g2
	.text	2
2:	orcc	%%g2, 0x0, %%g0
	bne,a	2b
	 ldub	[%0], %%g2
	b,a	1b
	.previous
"	: /* no outputs */
	: "r" (lock)
	: "g2", "memory", "cc");
}

extern __inline__ int spin_trylock(spinlock_t *lock)
{
	unsigned int result;
	__asm__ __volatile__("ldstub [%1], %0"
			     : "=r" (result)
			     : "r" (lock)
			     : "memory");
	return (result == 0);
}

extern __inline__ void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("stb %%g0, [%0]" : : "r" (lock) : "memory");
}

extern __inline__ void spin_lock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	rd	%%psr, %%g2
	or	%%g2, %0, %%g2
	wr	%%g2, 0x0, %%psr
	nop; nop; nop;
1:	ldstub	[%1], %%g2
	orcc	%%g2, 0x0, %%g0
	bne,a	2f
	 ldub	[%1], %%g2
	.text	2
2:	orcc	%%g2, 0x0, %%g0
	bne,a	2b
	 ldub	[%1], %%g2
	b,a	1b
	.previous
"	: /* No outputs */
	: "i" (PSR_PIL), "r" (lock)
	: "g2", "memory", "cc");
}

extern __inline__ void spin_unlock_irq(spinlock_t *lock)
{
	__asm__ __volatile__("
	rd	%%psr, %%g2
	andn	%%g2, %1, %%g2
	stb	%%g0, [%0]
	wr	%%g2, 0x0, %%psr
	nop; nop; nop;
"	: /* No outputs. */
	: "r" (lock), "i" (PSR_PIL)
	: "g2", "memory");
}

#define spin_lock_irqsave(lock, flags)		\
do {						\
	register spinlock_t *lp asm("g1");	\
	lp = lock;				\
	__asm__ __volatile__(			\
	"rd	%%psr, %0\n\t"			\
	"or	%0, %1, %%g2\n\t"		\
	"wr	%%g2, 0x0, %%psr\n\t"		\
	"nop; nop; nop;\n"			\
	"1:\n\t"				\
	"ldstub	[%2], %%g2\n\t"			\
	"orcc	%%g2, 0x0, %%g0\n\t"		\
	"bne,a	2f\n\t"				\
	" ldub	[%2], %%g2\n\t"			\
	".text	2\n"				\
	"2:\n\t"				\
	"orcc	%%g2, 0x0, %%g0\n\t"		\
	"bne,a	2b\n\t"				\
	" ldub	[%2], %%g2\n\t"			\
	"b,a	1b\n\t"				\
	".previous\n"				\
	: "=r" (flags)				\
	: "i" (PSR_PIL), "r" (lp)		\
	: "g2", "memory", "cc");		\
} while(0)

extern __inline__ void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__("
	stb	%%g0, [%0]
	wr	%1, 0x0, %%psr
	nop; nop; nop;
"	: /* No outputs. */
	: "r" (lock), "r" (flags)
	: "memory", "cc");
}

/* Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 *
 * XXX This might create some problems with my dual spinlock
 * XXX scheme, deadlocks etc. -DaveM
 */
typedef struct { volatile unsigned int lock; } rwlock_t;

#define RW_LOCK_UNLOCKED { 0 }

/* Sort of like atomic_t's on Sparc, but even more clever.
 *
 *	------------------------------------
 *	| 16-bit counter   | clock | wlock |  rwlock_t
 *	------------------------------------
 *	 31              16 15    8 7     0
 *
 * wlock signifies the one writer is in, the clock protects
 * counter bumping, however a reader must acquire wlock
 * before he can bump the counter on a read_lock().
 * Similarly a writer, once he has the wlock, must await
 * for the top 24 bits to all clear before he can finish
 * going in (this includes the clock of course).
 *
 * Unfortunately this scheme limits us to ~65,000 cpus.
 */
extern __inline__ void _read_lock(rwlock_t *rw)
{
	register rwlock_t *lp asm("g1");
	lp = rw;
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___rw_read_enter
	 ldstub	[%%g1 + 3], %%g2
"	: /* no outputs */
	: "r" (lp)
	: "g2", "g4", "g7", "memory", "cc");
}

#define read_lock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_read_lock(lock); \
	__restore_flags(flags); \
} while(0)

extern __inline__ void _read_unlock(rwlock_t *rw)
{
	register rwlock_t *lp asm("g1");
	lp = rw;
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___rw_read_exit
	 ldstub	[%%g1 + 2], %%g2
"	: /* no outputs */
	: "r" (lp)
	: "g2", "g4", "g7", "memory", "cc");
}

#define read_unlock(lock) \
do {	unsigned long flags; \
	__save_and_cli(flags); \
	_read_unlock(lock); \
	__restore_flags(flags); \
} while(0)

extern __inline__ void write_lock(rwlock_t *rw)
{
	register rwlock_t *lp asm("g1");
	lp = rw;
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___rw_write_enter
	 ldstub	[%%g1 + 3], %%g2
"	: /* no outputs */
	: "r" (lp)
	: "g2", "g4", "g7", "memory", "cc");
}

#define write_unlock(rw)	do { (rw)->lock = 0; } while(0)
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

#endif /* SPIN_LOCK_DEBUG */

#endif /* __SMP__ */

#endif /* !(__ASSEMBLY__) */

#endif /* __SPARC_SPINLOCK_H */
