/* $Id: spinlock.h,v 1.5 1999/06/17 13:30:39 ralf Exp $
 */
#ifndef __ASM_MIPS_SPINLOCK_H
#define __ASM_MIPS_SPINLOCK_H

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

/*
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
#define spin_lock(lock)		do { } while(0)
#define spin_trylock(lock)	(1)
#define spin_unlock_wait(lock)	do { } while(0)
#define spin_unlock(lock)	do { } while(0)

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
#define RW_LOCK_UNLOCKED (rwlock_t) { }

#define read_lock(lock)		do { } while(0)
#define read_unlock(lock)	do { } while(0)
#define write_lock(lock)	do { } while(0)
#define write_unlock(lock)	do { } while(0)

#else

#error "Nix SMP on MIPS"

#endif /* SMP */
#endif /* __ASM_MIPS_SPINLOCK_H */
