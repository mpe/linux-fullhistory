#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#ifndef __SMP__

typedef struct { int blah; } spinlock_t;
#define SPIN_LOCK_UNLOCKED { }

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)
#define spin_lock_irq(lock)	cli()
#define spin_unlock_irq(lock)	sti()

#define spin_lock_irqsave(lock, flags) \
	do { save_flags(flags); cli(); } while (0)
#define spin_unlock_irqrestore(lock, flags) \
	restore_flags(flags)

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
typedef struct { int fred; } rwlock_t;
#define RW_LOCK_UNLOCKED { }

#define read_lock(lock)		do { } while(0)
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	do { } while(0)
#define write_unlock(lock)	do { } while(0)
#define read_lock_irq(lock)	cli()
#define read_unlock_irq(lock)	sti()
#define write_lock_irq(lock)	cli()
#define write_unlock_irq(lock)	sti()

#define read_lock_irqsave(lock, flags)	\
	do { save_flags(flags); cli(); } while (0)
#define read_unlock_irqrestore(lock, flags) \
	restore_flags(flags)
#define write_lock_irqsave(lock, flags)	\
	do { save_flags(flags); cli(); } while (0)
#define write_unlock_irqrestore(lock, flags) \
	restore_flags(flags)

#else

/* Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned int lock;
	unsigned long previous;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0, 0 }

#define spin_unlock(lock)	((lock)->lock = 0)

static inline void spin_lock(spinlock_t * lock)
{
	int stuck = 10000000;
	int tmp, val;
	__label__ l1;

l1:	__asm__ __volatile__(
		"	mtctr %2\n"
		"1:	lwarx %0,0,%3\n"
		"	andi. %1,%0,1\n\t"
		"	ori %0,%0,1\n\t"
		"	bne- 2f\n\t"
		"	stwcx. %0,0,%3\n\t"
		"2:	bdnzf- 2,1b"
		: "=r" (tmp), "=r" (val)
		: "r" (stuck), "r" (lock)
		: "ctr");
	if (!val) {
		printk("spinlock stuck at %p (%lx)\n", &&l1, lock->previous);
	} else
		lock->previous = (unsigned long) &&l1;
}

#define spin_trylock(lock) (!set_bit(0,(lock)))

#define spin_lock_irq(lock) \
	do { __cli(); spin_lock(lock); } while (0)

#define spin_unlock_irq(lock) \
	do { spin_unlock(lock); __sti(); } while (0)

#define spin_lock_irqsave(lock, flags) \
	do { __save_flags(flags); __cli(); spin_lock(lock); } while (0)

#define spin_unlock_irqrestore(lock, flags) \
	do { spin_unlock(lock); __restore_flags(flags); } while (0)

#endif /* SMP */
#endif /* __ASM_SPINLOCK_H */
