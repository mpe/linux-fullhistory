#ifndef _ALPHA_SPINLOCK_H
#define _ALPHA_SPINLOCK_H

#ifndef __SMP__

typedef struct { } spinlock_t;
#define SPIN_LOCK_UNLOCKED { }

#define spin_lock_init(lock)			do { } while(0)
#define spin_lock(lock)				do { } while(0)
#define spin_trylock(lock)			do { } while(0)
#define spin_unlock_wait(lock)			do { } while(0)
#define spin_unlock(lock)			do { } while(0)
#define spin_lock_irq(lock)			setipl(7)
#define spin_unlock_irq(lock)			setipl(0)

#define spin_lock_irqsave(lock, flags)		swpipl(flags,7)
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

#else

/* Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned long lock;
	unsigned long previous;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0, 0 }

#define spin_lock_init(lock)	do { (lock)->lock = 0; (lock)->previous = 0; } while(0)
#define spin_unlock_wait(lock)	do { barrier(); } while(((volatile spinlock_t *)lock)->lock)

typedef struct { unsigned long a[100]; } __dummy_lock_t;
#define __dummy_lock(lock) (*(__dummy_lock_t *)(lock))

static inline void spin_unlock(spinlock_t * lock)
{
	__asm__ __volatile__(
	"mb; stq $31,%0"
	:"=m" (__dummy_lock(lock)));
}

static inline void spin_lock(spinlock_t * lock)
{
	__label__ l1;
	long tmp;
	long stuck = 0x100000000;
l1:
	/* Use sub-sections to put the actual loop at the end
	   of this object file's text section so as to perfect
	   branch prediction.  */
	__asm__ __volatile__(
	"1:	ldq_l	%0,%1\n"
	"	subq	%2,1,%2\n"
	"	blbs	%0,2f\n"
	"	or	%0,1,%0\n"
	"	stq_c	%0,%1\n"
	"	beq	%0,3f\n"
	"4:	mb\n"
	".text 2\n"
	"2:	ldq	%0,%1\n"
	"	subq	%2,1,%2\n"
	"3:	blt	%2,4b\n"
	"	blbs	%0,2b\n"
	"	br	1b\n"
	".text"
	: "=r" (tmp),
	  "=m" (__dummy_lock(lock)),
	  "=r" (stuck)
	: "2" (stuck));

	if (stuck < 0)
		printk("spinlock stuck at %p (%lx)\n",&&l1,lock->previous);
	else
		lock->previous = (unsigned long) &&l1;
}

#define spin_trylock(lock) (!set_bit(0,(lock)))

#define spin_lock_irq(lock) \
	do { __cli(); spin_lock(lock); } while (0)

#define spin_unlock_irq(lock) \
	do { spin_unlock(lock); __sti(); } while (0)

#define spin_lock_irqsave(lock, flags) \
	do { swpipl(flags,7); spin_lock(lock); } while (0)

#define spin_unlock_irqrestore(lock, flags) \
	do { spin_unlock(lock); setipl(flags); } while (0)

#endif /* SMP */
#endif /* _ALPHA_SPINLOCK_H */
