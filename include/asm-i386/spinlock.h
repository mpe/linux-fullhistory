#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#ifndef __SMP__

typedef struct { } spinlock_t;
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

typedef struct { unsigned long a[100]; } __dummy_lock_t;
#define __dummy_lock(lock) (*(__dummy_lock_t *)(lock))

#define spin_lock(lock) \
__asm__ __volatile__( \
	"jmp 2f\n" \
	"1:\t" \
	"testb $1,%0\n\t" \
	"jne 1b\n" \
	"2:\t" \
	"lock ; btsl $0,%0\n\t" \
	"jc 1b" \
	:"=m" (__dummy_lock(lock)))

#define spin_unlock(lock) \
__asm__ __volatile__( \
	"lock ; btrl $0,%0" \
	:"=m" (__dummy_lock(lock)))

#undef spin_lock
static inline void spin_lock(spinlock_t * lock)
{
	__label__ l1;
	int stuck = 10000000;
l1:
	__asm__ __volatile__(
		"jmp 2f\n"
		"1:\t"
		"decl %1\n\t"
		"je 3f\n\t"
		"testb $1,%0\n\t"
		"jne 1b\n"
		"2:\t"
		"lock ; btsl $0,%0\n\t"
		"jc 1b\n"
		"3:"
		:"=m" (__dummy_lock(lock)),
		 "=r" (stuck)
		:"1" (stuck));
	if (!stuck) {
		printk("spinlock stuck at %p (%lx)\n",&&l1,lock->previous);
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
