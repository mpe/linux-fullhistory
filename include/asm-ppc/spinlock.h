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

#else /* __SMP__ */

/* Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

struct _spinlock_debug {
	volatile unsigned long lock;
	volatile unsigned long owner_pc;
};

typedef struct _spinlock_debug spinlock_t;

#define SPIN_LOCK_UNLOCKED { 0, 0 }

#define SPIN_LOCK_UNLOCKED	{ 0, 0 }
#define spin_lock_init(lp)	do { (lp)->owner_pc = 0; (lp)->lock = 0; } while(0)
#define spin_unlock_wait(lp)	do { barrier(); } while((lp)->lock)

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
#if 0
extern __inline__ void spin_unlock(spinlock_t *lock)
{
	__asm__ __volatile__("stw 0,%0" : : "m" (lock) : "memory");
}

static inline void spin_lock(spinlock_t * lock)
{
	int stuck = 10000000;
	int tmp, val;

	__asm__ __volatile__(
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
	if (!val)
	{
		unsigned long __nip;
		asm("mfnip %0\n": "=r" (__nip));
		printk("spinlock stuck at %08lx\n", __nip);
	}
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
#endif

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


#endif /* SMP */
#endif /* __ASM_SPINLOCK_H */









