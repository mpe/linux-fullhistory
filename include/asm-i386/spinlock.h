#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#ifndef __SMP__

/*
 * Your basic spinlocks, allowing only a single CPU anywhere
 */
typedef struct { } spinlock_t;
#define SPIN_LOCK_UNLOCKED { }

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock_wait(lock)	do { } while(0)
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

#define read_lock_irqsave(lock, flags)	\
	do { save_flags(flags); cli(); } while (0)
#define read_unlock_irqrestore(lock, flags) \
	restore_flags(flags)
#define write_lock_irqsave(lock, flags)	\
	do { save_flags(flags); cli(); } while (0)
#define write_unlock_irqrestore(lock, flags) \
	restore_flags(flags)

#else

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned int lock;
	unsigned long previous;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0, 0 }

#define spin_lock_init(x)	do { (x)->lock = 0; (x)->previous = 0; } while(0)
#define spin_unlock_wait(x)	do { barrier(); } while(((volatile spinlock_t *)(x))->lock)

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
typedef struct {
	volatile unsigned int lock;
	unsigned long previous;
} rwlock_t;

#define RW_LOCK_UNLOCKED { 0, 0 }

/*
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "write" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 */
#define read_lock(rw)	\
	asm volatile("\n1:\t" \
		     "lock ; incl %0\n\t" \
		     "js 2f\n" \
		     ".text 2\n" \
		     "2:\tlock ; decl %0\n" \
		     "3:\tcmpl $0,%0\n\t" \
		     "js 3b\n\t" \
		     "jmp 1b\n" \
		     ".text" \
		     :"=m" (__dummy_lock(&(rw)->lock)))

#define read_unlock(rw) \
	asm volatile("lock ; decl %0" \
		:"=m" (__dummy_lock(&(rw)->lock)))

#define write_lock(rw) \
	asm volatile("\n1:\t" \
		     "lock ; btsl $31,%0\n\t" \
		     "jc 3f\n\t" \
		     "testl $0x7fffffff,%0\n\t" \
		     "jne 4f\n" \
		     "2:\n" \
		     ".text 2\n" \
		     "3:\ttestl $-1,%0\n\t" \
		     "js 3b\n\t" \
		     "lock ; btsl $31,%0\n\t" \
		     "jc 3b\n" \
		     "4:\ttestl $0x7fffffff,%0\n\t" \
		     "jne 4b\n\t" \
		     "jmp 2b\n" \
		     ".text" \
		     :"=m" (__dummy_lock(&(rw)->lock)))

#define write_unlock(rw) \
	asm volatile("lock ; btrl $31,%0":"=m" (__dummy_lock(&(rw)->lock)))

#define read_lock_irq(lock)	do { __cli(); read_lock(lock); } while (0)
#define read_unlock_irq(lock)	do { read_unlock(lock); __sti(); } while (0)
#define write_lock_irq(lock)	do { __cli(); write_lock(lock); } while (0)
#define write_unlock_irq(lock)	do { write_unlock(lock); __sti(); } while (0)

#define read_lock_irqsave(lock, flags)	\
	do { __save_flags(flags); __cli(); read_lock(lock); } while (0)
#define read_unlock_irqrestore(lock, flags) \
	do { read_unlock(lock); __restore_flags(flags); } while (0)
#define write_lock_irqsave(lock, flags)	\
	do { __save_flags(flags); __cli(); write_lock(lock); } while (0)
#define write_unlock_irqrestore(lock, flags) \
	do { write_unlock(lock); __restore_flags(flags); } while (0)

#endif /* SMP */
#endif /* __ASM_SPINLOCK_H */
