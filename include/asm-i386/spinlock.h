#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * These are the generic versions of the spinlocks
 * and read-write locks.. We should actually do a
 * <linux/spinlock.h> with all of this. Oh, well.
 */
#define spin_lock_irqsave(lock, flags)		do { local_irq_save(flags);       spin_lock(lock); } while (0)
#define spin_lock_irq(lock)			do { local_irq_disable();         spin_lock(lock); } while (0)
#define spin_lock_bh(lock)			do { local_bh_disable();          spin_lock(lock); } while (0)

#define read_lock_irqsave(lock, flags)		do { local_irq_save(flags);       read_lock(lock); } while (0)
#define read_lock_irq(lock)			do { local_irq_disable();         read_lock(lock); } while (0)
#define read_lock_bh(lock)			do { local_bh_disable();          read_lock(lock); } while (0)

#define write_lock_irqsave(lock, flags)		do { local_irq_save(flags);      write_lock(lock); } while (0)
#define write_lock_irq(lock)			do { local_irq_disable();        write_lock(lock); } while (0)
#define write_lock_bh(lock)			do { local_bh_disable();         write_lock(lock); } while (0)

#define spin_unlock_irqrestore(lock, flags)	do { spin_unlock(lock);  local_irq_restore(flags); } while (0)
#define spin_unlock_irq(lock)			do { spin_unlock(lock);  local_irq_enable();       } while (0)
#define spin_unlock_bh(lock)			do { spin_unlock(lock);  local_bh_enable();        } while (0)

#define read_unlock_irqrestore(lock, flags)	do { read_unlock(lock);  local_irq_restore(flags); } while (0)
#define read_unlock_irq(lock)			do { read_unlock(lock);  local_irq_enable();       } while (0)
#define read_unlock_bh(lock)			do { read_unlock(lock);  local_bh_enable();        } while (0)

#define write_unlock_irqrestore(lock, flags)	do { write_unlock(lock); local_irq_restore(flags); } while (0)
#define write_unlock_irq(lock)			do { write_unlock(lock); local_irq_enable();       } while (0)
#define write_unlock_bh(lock)			do { write_unlock(lock); local_bh_enable();        } while (0)

#ifndef __SMP__

#define DEBUG_SPINLOCKS	0	/* 0 == no debugging, 1 == maintain lock state, 2 == full debug */

#if (DEBUG_SPINLOCKS < 1)

/*
 * Your basic spinlocks, allowing only a single CPU anywhere
 *
 * Gcc-2.7.x has a nasty bug with empty initializers.
 */
#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8)
  typedef struct { } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
#endif

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		(void)(lock) /* Not "unused variable". */
#define spin_trylock(lock)	(1)
#define spin_unlock_wait(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)

#elif (DEBUG_SPINLOCKS < 2)

typedef struct {
	volatile unsigned int lock;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }

#define spin_lock_init(x)	do { (x)->lock = 0; } while (0)
#define spin_trylock(lock)	(!test_and_set_bit(0,(lock)))

#define spin_lock(x)		do { (x)->lock = 1; } while (0)
#define spin_unlock_wait(x)	do { } while (0)
#define spin_unlock(x)		do { (x)->lock = 0; } while (0)

#else /* (DEBUG_SPINLOCKS >= 2) */

typedef struct {
	volatile unsigned int lock;
	volatile unsigned int babble;
	const char *module;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0, 25, __BASE_FILE__ }

#include <linux/kernel.h>

#define spin_lock_init(x)	do { (x)->lock = 0; } while (0)
#define spin_trylock(lock)	(!test_and_set_bit(0,(lock)))

#define spin_lock(x)		do {unsigned long __spinflags; save_flags(__spinflags); cli(); if ((x)->lock&&(x)->babble) {printk("%s:%d: spin_lock(%s:%p) already locked\n", __BASE_FILE__,__LINE__, (x)->module, (x));(x)->babble--;} (x)->lock = 1; restore_flags(__spinflags);} while (0)
#define spin_unlock_wait(x)	do {unsigned long __spinflags; save_flags(__spinflags); cli(); if ((x)->lock&&(x)->babble) {printk("%s:%d: spin_unlock_wait(%s:%p) deadlock\n", __BASE_FILE__,__LINE__, (x)->module, (x));(x)->babble--;} restore_flags(__spinflags);} while (0)
#define spin_unlock(x)		do {unsigned long __spinflags; save_flags(__spinflags); cli(); if (!(x)->lock&&(x)->babble) {printk("%s:%d: spin_unlock(%s:%p) not locked\n", __BASE_FILE__,__LINE__, (x)->module, (x));(x)->babble--;} (x)->lock = 0; restore_flags(__spinflags);} while (0)

#endif	/* DEBUG_SPINLOCKS */

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 *
 * Gcc-2.7.x has a nasty bug with empty initializers.
 */
#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8)
  typedef struct { } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { 0 }
#endif

#define read_lock(lock)		(void)(lock) /* Not "unused variable". */
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	(void)(lock) /* Not "unused variable". */
#define write_unlock(lock)	do { } while(0)

#else	/* __SMP__ */

/*
 * Your basic spinlocks, allowing only a single CPU anywhere
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
	"lock ; btrl $0,%0"

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
	unsigned long previous;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }

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

#endif /* __SMP__ */
#endif /* __ASM_SPINLOCK_H */
