/* $Id: debuglocks.c,v 1.1 1997/05/08 18:13:34 davem Exp $
 * debuglocks.c: Debugging versions of SMP locking primitives.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
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

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("spin_lock(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", lock, cpu, caller, lock->owner_pc & ~3, lock->owner_pc & 3); stuck = INIT_STUCK; }

void _spin_lock(spinlock_t *lock)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
again:
	__asm__ __volatile__("ldstub [%1], %0" : "=r" (val) : "r" (&(lock->lock)));
	if(val) {
		while(lock->lock) {
			STUCK;
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

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
	__asm__ __volatile__("ldstub [%1], %0" : "=r" (val) : "r" (&(lock->lock)));
	if(!val) {
		/* We got it, record our identity for debugging. */
		lock->owner_pc = (cpu & 3) | (caller & ~3);
	}
	return val == 0;
}

void _spin_unlock(spinlock_t *lock)
{
	lock->owner_pc = 0;
	__asm__ __volatile__("stb %%g0, [%0]" : : "r" (&(lock->lock)) : "memory");
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("spin_lock_irq(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", lock, cpu, caller, lock->owner_pc & ~3, lock->owner_pc & 3); stuck = INIT_STUCK; }

void _spin_lock_irq(spinlock_t *lock)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__cli();
	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
again:
	__asm__ __volatile__("ldstub [%1], %0" : "=r" (val) : "r" (&(lock->lock)));
	if(val) {
		while(lock->lock) {
			STUCK;
			barrier();
		}
		goto again;
	}
	lock->owner_pc = (cpu & 3) | (caller & ~3);
}

void _spin_unlock_irq(spinlock_t *lock)
{
	lock->owner_pc = 0;
	__asm__ __volatile__("stb %%g0, [%0]" : : "r" (&(lock->lock)) : "memory");
	__sti();
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("spin_lock_irq(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", lock, cpu, caller, lock->owner_pc & ~3, lock->owner_pc & 3); stuck = INIT_STUCK; }

/* Caller macro does __save_and_cli(flags) for us. */
void _spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
again:
	__asm__ __volatile__("ldstub [%1], %0" : "=r" (val) : "r" (&(lock->lock)));
	if(val) {
		while(lock->lock) {
			STUCK;
			barrier();
		}
		goto again;
	}
	lock->owner_pc = (cpu & 3) | (caller & ~3);
}

void _spin_unlock_irqrestore(spinlock_t *lock)
{
	lock->owner_pc = 0;
	__asm__ __volatile__("stb %%g0, [%0]" : : "r" (&(lock->lock)) : "memory");
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("read_lock(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _read_lock(rwlock_t *rw)
{
	unsigned long flags;
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
	__save_and_cli(flags);
wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			STUCK;
			barrier();
		}
		goto wlock_again;
	}
clock_again:
	__asm__ __volatile__("ldstub [%1 + 2], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff00) {
			STUCK;
			barrier();
		}
		goto clock_again;
	}
	(*((unsigned short *)&rw->lock))++;
	barrier();
	(*(((unsigned short *)&rw->lock)+1)) = 0;
	__restore_flags(flags);
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("read_unlock(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _read_unlock(rwlock_t *rw)
{
	unsigned long flags, val, caller;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
	__save_and_cli(flags);
clock_again:
	__asm__ __volatile__("ldstub [%1 + 2], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff00) {
			STUCK;
			barrier();
		}
		goto clock_again;
	}
	(*((unsigned short *)&rw->lock))--;
	barrier();
	(*(((unsigned char *)&rw->lock)+2))=0;
	__restore_flags(flags);
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("write_lock(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _write_lock(rwlock_t *rw)
{
	unsigned long flags, val, caller;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
	__save_and_cli(flags);
wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			STUCK;
			barrier();
		}
		goto wlock_again;
	}
	rw->owner_pc = (cpu & 3) | (caller & ~3);
	while(rw->lock & ~0xff) {
		STUCK;
		barrier();
	}
}

void _write_unlock(rwlock_t *rw)
{
	rw->owner_pc = 0;
	barrier();
	rw->lock = 0;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("read_lock_irq(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _read_lock_irq(rwlock_t *rw)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
	__cli();
wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			STUCK;
			barrier();
		}
		goto wlock_again;
	}
clock_again:
	__asm__ __volatile__("ldstub [%1 + 2], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff00) {
			STUCK;
			barrier();
		}
		goto clock_again;
	}
	(*((unsigned short *)&rw->lock))++;
	barrier();
	(*(((unsigned short *)&rw->lock)+1)) = 0;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("read_unlock_irq(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _read_unlock_irq(rwlock_t *rw)
{
	unsigned long val, caller;
	int stuck = INIT_STUCK;
	int cpu = smp_processor_id();

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
clock_again:
	__asm__ __volatile__("ldstub [%1 + 2], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff00) {
			STUCK;
			barrier();
		}
		goto clock_again;
	}
	(*((unsigned short *)&rw->lock))--;
	barrier();
	(*(((unsigned char *)&rw->lock)+2))=0;
	__sti();
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("write_lock_irq(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _write_lock_irq(rwlock_t *rw)
{
	unsigned long val, caller;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
	__cli();
wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			STUCK;
			barrier();
		}
		goto wlock_again;
	}
	rw->owner_pc = (cpu & 3) | (caller & ~3);
	while(rw->lock & ~0xff) {
		STUCK;
		barrier();
	}
}

void _write_unlock_irq(rwlock_t *rw)
{
	rw->owner_pc = 0;
	barrier();
	rw->lock = 0;
	__sti();
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("read_lock_irqsave(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

/* Caller does __save_and_cli(flags) for us. */
void _read_lock_irqsave(rwlock_t *rw)
{
	unsigned long caller;
	unsigned long val;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			STUCK;
			barrier();
		}
		goto wlock_again;
	}
clock_again:
	__asm__ __volatile__("ldstub [%1 + 2], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff00) {
			STUCK;
			barrier();
		}
		goto clock_again;
	}
	(*((unsigned short *)&rw->lock))++;
	barrier();
	(*(((unsigned short *)&rw->lock)+1)) = 0;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("read_unlock_irqrestore(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

void _read_unlock_irqrestore(rwlock_t *rw)
{
	unsigned long val, caller;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
clock_again:
	__asm__ __volatile__("ldstub [%1 + 2], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff00) {
			STUCK;
			barrier();
		}
		goto clock_again;
	}
	(*((unsigned short *)&rw->lock))--;
	barrier();
	(*(((unsigned char *)&rw->lock)+2))=0;
}

#undef INIT_STUCK
#define INIT_STUCK 100000000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("write_lock_irqsave(%p) CPU#%d stuck at %08lx, owner PC(%08lx):CPU(%lx)\n", rw, cpu, caller, rw->owner_pc & ~3, rw->owner_pc & 3); stuck = INIT_STUCK; }

/* Caller does __save_and_cli(flags) for us. */
void _write_lock_irqsave(rwlock_t *rw)
{
	unsigned long val, caller;
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;

	__asm__ __volatile__("mov %%i7, %0" : "=r" (caller));
wlock_again:
	__asm__ __volatile__("ldstub [%1 + 3], %0" : "=r" (val) : "r" (&(rw->lock)));
	if(val) {
		while(rw->lock & 0xff) {
			STUCK;
			barrier();
		}
		goto wlock_again;
	}
	rw->owner_pc = (cpu & 3) | (caller & ~3);
	while(rw->lock & ~0xff) {
		STUCK;
		barrier();
	}
}

void _write_unlock_irqrestore(rwlock_t *rw)
{
	rw->owner_pc = 0;
	barrier();
	rw->lock = 0;
}

#endif /* SPIN_LOCK_DEBUG */
