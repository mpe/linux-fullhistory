/* $Id: debuglocks.c,v 1.5 1998/10/14 09:19:04 jj Exp $
 * debuglocks.c: Debugging versions of SMP locking primitives.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/psr.h>
#include <asm/system.h>
#include <asm/spinlock.h>

/* To enable this code, just define SPIN_LOCK_DEBUG in asm/spinlock.h */
#ifdef SPIN_LOCK_DEBUG

/* Some notes on how these debugging routines work.  When a lock is acquired
 * an extra debugging member lock->owner_pc is set to the caller of the lock
 * acquisition routine.  Right before releasing a lock, the debugging program
 * counter is cleared to zero.
 *
 * Furthermore, since PC's are 4 byte aligned on Sparc, we stuff the CPU
 * number of the owner in the lowest two bits.
 */

#define STORE_CALLER(A) __asm__ __volatile__("mov %%i7, %0" : "=r" (A));

static inline void show(char *str, spinlock_t *lock, unsigned long caller)
{
	int cpu = smp_processor_id();

	printk("%s(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n",str,
		lock, cpu, caller, lock->owner_pc & ~3, lock->owner_pc & 3);
}

static inline void show_read(char *str, rwlock_t *lock, unsigned long caller)
{
	int cpu = smp_processor_id();

	printk("%s(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n",str,
		lock, cpu, caller, lock->owner_pc & ~3, lock->owner_pc & 3);
}

static inline void show_write(char *str, rwlock_t *lock, unsigned long caller)
{
	int cpu = smp_processor_id();

	printk("%s(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx) reader[0]=%08lx reader[1]=%08lx reader[2]=%08lx reader[3]=%08lx\n",
		str, lock, cpu, caller, lock->owner_pc & ~3, lock->owner_pc & 3,
		lock->reader_pc[0],
		lock->reader_pc[1],
		lock->reader_pc[2],
		lock->reader_pc[3]);
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

void _do_spin_lock(spinlock_t *lock, char *str)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	STORE_CALLER(caller);

again:
	__asm__ __volatile__("ldstub [%1], %0" : "=r" (val) : "r" (&(lock->lock)));
	if(val) {
		while(lock->lock) {
			if (!--stuck) {
				show(str, lock, caller);
				stuck = INIT_STUCK;
			}
			barrier();
		}
		goto again;
	}
	lock->owner_pc = (cpu & 3) | (caller & ~3);
}

int _spin_trylock(spinlock_t *lock)
{
	unsigned long val;
	unsigned long caller;
	int cpu = smp_processor_id();

	STORE_CALLER(caller);

	__asm__ __volatile__("ldstub [%1], %0" : "=r" (val) : "r" (&(lock->lock)));
	if(!val) {
		/* We got it, record our identity for debugging. */
		lock->owner_pc = (cpu & 3) | (caller & ~3);
	}
	return val == 0;
}

void _do_spin_unlock(spinlock_t *lock)
{
	lock->owner_pc = 0;
	barrier();
	lock->lock = 0;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

void _do_read_lock(rwlock_t *rw, char *str)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	STORE_CALLER(caller);

wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			if (!--stuck) {
				show_read(str, rw, caller);
				stuck = INIT_STUCK;
			}
			barrier();
		}
		goto wlock_again;
	}

	rw->reader_pc[cpu] = caller;
	barrier();
	rw->lock++;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

void _do_read_unlock(rwlock_t *rw, char *str)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	STORE_CALLER(caller);

wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			if (!--stuck) {
				show_read(str, rw, caller);
				stuck = INIT_STUCK;
			}
			barrier();
		}
		goto wlock_again;
	}

	rw->reader_pc[cpu] = 0;
	barrier();
	rw->lock -= 0x1ff;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

void _do_write_lock(rwlock_t *rw, char *str)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	STORE_CALLER(caller);

wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
wlock_wait:
		while(rw->lock) {
			if (!--stuck) {
				show_write(str, rw, caller);
				stuck = INIT_STUCK;
			}
			barrier();
		}
		goto wlock_again;
	}

	if (rw->lock & ~0xff) {
		*(((unsigned char *)&rw->lock)+3) = 0;
		barrier();
		goto wlock_wait;
	}

	barrier();
	rw->owner_pc = (cpu & 3) | (caller & ~3);
}

void _do_write_unlock(rwlock_t *rw)
{
	rw->owner_pc = 0;
	barrier();
	rw->lock = 0;
}

#endif /* SPIN_LOCK_DEBUG */
