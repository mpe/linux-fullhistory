#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#ifndef __SMP__

/*
 * To be safe, we assume the only compiler that can cope with
 * empty initialisers is EGCS.
 */
#if (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 90))
#define EMPTY_INIT_OK
#endif

/*
 * Your basic spinlocks, allowing only a single CPU anywhere
 */
#ifdef EMPTY_INIT_OK
  typedef struct { } spinlock_t;
# define SPIN_LOCK_UNLOCKED (spinlock_t) { }
#else
  typedef unsigned char spinlock_t;
# define SPIN_LOCK_UNLOCKED 0
#endif

#define spin_lock_init(lock)	do { } while(0)
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	do { } while(0)
#define spin_unlock_wait(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)
#define spin_lock_irq(lock)	cli()
#define spin_unlock_irq(lock)	sti()

#define spin_lock_irqsave(lock, flags) \
	do { __save_flags_cli(flags); } while (0)
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
#ifdef EMPTY_INIT_OK
  typedef struct { } rwlock_t;
# define RW_LOCK_UNLOCKED (rwlock_t) { }
#else
  typedef unsigned char rwlock_t;
# define RW_LOCK_UNLOCKED 0
#endif

#define read_lock(lock)		do { } while(0)
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	do { } while(0)
#define write_unlock(lock)	do { } while(0)
#define read_lock_irq(lock)	cli()
#define read_unlock_irq(lock)	sti()
#define write_lock_irq(lock)	cli()
#define write_unlock_irq(lock)	sti()

#define read_lock_irqsave(lock, flags)	\
	do { __save_flags_cli(flags); } while (0)
#define read_unlock_irqrestore(lock, flags) \
	restore_flags(flags)
#define write_lock_irqsave(lock, flags)	\
	do { __save_flags_cli(flags); } while (0)
#define write_unlock_irqrestore(lock, flags) \
	restore_flags(flags)

#else
#error ARM architecture does not support spin locks
#endif /* SMP */
#endif /* __ASM_SPINLOCK_H */
