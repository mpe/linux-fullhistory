#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }

#define spin_lock_init(x)	do { (x)->lock = 0; } while(0)
/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define spin_unlock_wait(x)	do { barrier(); } while(((volatile spinlock_t *)(x))->lock)

typedef struct { unsigned long a[100]; } __dummy_lock_t;
#define __dummy_lock(lock) (*(__dummy_lock_t *)(lock))

#define spin_lock_string \
	"\n1:\t" \
	"lock ; btsl $0,%0\n\t" \
	"jc 2f\n" \
	".section .text.lock,\"ax\"\n" \
	"2:\t" \
	"testb $1,%0\n\t" \
	"jne 2b\n\t" \
	"jmp 1b\n" \
	".previous"

#define spin_unlock_string \
	"movb $0,%0"

#define spin_lock(lock) \
__asm__ __volatile__( \
	spin_lock_string \
	:"=m" (__dummy_lock(lock)))

#define spin_unlock(lock) \
__asm__ __volatile__( \
	spin_unlock_string \
	:"=m" (__dummy_lock(lock)))

#define spin_trylock(lock) (!test_and_set_bit(0,(lock)))

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
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0 }

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
		     ".section .text.lock,\"ax\"\n" \
		     "2:\tlock ; decl %0\n" \
		     "3:\tcmpl $0,%0\n\t" \
		     "js 3b\n\t" \
		     "jmp 1b\n" \
		     ".previous" \
		     :"=m" (__dummy_lock(&(rw)->lock)))

#define read_unlock(rw) \
	asm volatile("lock ; decl %0" \
		:"=m" (__dummy_lock(&(rw)->lock)))

#define write_lock(rw) \
	asm volatile("\n1:\t" \
		     "lock ; btsl $31,%0\n\t" \
		     "jc 4f\n" \
		     "2:\ttestl $0x7fffffff,%0\n\t" \
		     "jne 3f\n" \
		     ".section .text.lock,\"ax\"\n" \
		     "3:\tlock ; btrl $31,%0\n" \
		     "4:\tcmp $0,%0\n\t" \
		     "jne 4b\n\t" \
		     "jmp 1b\n" \
		     ".previous" \
		     :"=m" (__dummy_lock(&(rw)->lock)))

#define write_unlock(rw) \
	asm volatile("lock ; btrl $31,%0":"=m" (__dummy_lock(&(rw)->lock)))

#endif /* __ASM_SPINLOCK_H */
