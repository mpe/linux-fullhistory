/*
 * $Id: locks.c,v 1.7 1998/01/06 06:44:59 cort Exp $
 *
 * Locks for smp ppc 
 * 
 * Written by Cort Dougan (cort@cs.nmt.edu)
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/spinlock.h>

#define DEBUG_LOCKS 1

#undef INIT_STUCK
#define INIT_STUCK 10000

#undef STUCK
#define STUCK \
if(!--stuck) { printk("spin_lock(%p) CPU#%d nip %08lx\n", lock, cpu, nip); stuck = INIT_STUCK; }

void _spin_lock(spinlock_t *lock)
{
	unsigned long val, nip = (unsigned long)__builtin_return_address(0);
	int cpu = smp_processor_id();
	int stuck = INIT_STUCK;
	
again:	
	/* try expensive atomic load/store to get lock */
	__asm__ __volatile__(
		"10: \n\t"
		"lwarx %0,0,%1 \n\t"
		"stwcx. %2,0,%1 \n\t"
		"bne- 10b \n\t"
		: "=r" (val)
		: "r" (&(lock->lock)), "r" ( (cpu&3)|(nip&~3L) ));
	if(val) {
		/* try cheap load until it's free */
		while(lock->lock) {
			STUCK;
			barrier();
		}
		goto again;
	}
}

void _spin_unlock(spinlock_t *lp)
{
	lp->lock = 0;
}
		
#undef STUCK
#define STUCK \
if(!--stuck) { printk("_read_lock(%p) CPU#%d\n", rw, cpu); stuck = INIT_STUCK; }

/*
 * Just like x86, implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "write" bit.
 * -- Cort
 */
void _read_lock(rwlock_t *rw)
{
	unsigned long stuck = INIT_STUCK;
	int cpu = smp_processor_id();

again:	
	/* get our read lock in there */
	atomic_inc((atomic_t *) &(rw)->lock);
	if ( (signed long)((rw)->lock) < 0) /* someone has a write lock */
	{
		/* turn off our read lock */
		atomic_dec((atomic_t *) &(rw)->lock);
		/* wait for the write lock to go away */
		while ((signed long)((rw)->lock) < 0)
		{
			STUCK;
		}
		/* try to get the read lock again */
		goto again;
	}
}

void _read_unlock(rwlock_t *rw)
{
#ifdef DEBUG_LOCKS
	if ( rw->lock == 0 )
	{
		if ( current)
		printk("_read_unlock(): %s/%d (nip %08lX) lock %lx",
		      current->comm,current->pid,current->tss.regs->nip,
		      rw->lock);
		else
		  printk("no current\n");
	}
#endif /* DEBUG_LOCKS */
	atomic_dec((atomic_t *) &(rw)->lock);
}

#undef STUCK
#define STUCK \
if(!--stuck) { printk("write_lock(%p) CPU#%d lock %lx)\n", rw, cpu,rw->lock); stuck = INIT_STUCK; }

void _write_lock(rwlock_t *rw)
{
	unsigned long stuck = INIT_STUCK;
	int cpu = smp_processor_id();

again:	
	if ( test_and_set_bit(31,&(rw)->lock) ) /* someone has a write lock */
	{
		while ( (rw)->lock & (1<<31) ) /* wait for write lock */
		{
			STUCK;
		}
		goto again;
	}
	
	if ( (rw)->lock & ~(1<<31)) /* someone has a read lock */
	{
		/* clear our write lock and wait for reads to go away */
		clear_bit(31,&(rw)->lock);
		while ( (rw)->lock & ~(1<<31) )
		{
			STUCK;
		}
		goto again;
	}
}

void _write_unlock(rwlock_t *rw)
{
#ifdef DEBUG_LOCKS
	if ( !(rw->lock & (1<<31)) )
	{
		if ( current)
		printk("_write_lock(): %s/%d (nip %08lX) lock %lx",
		      current->comm,current->pid,current->tss.regs->nip,
		      rw->lock);
		else
		  printk("no current\n");
	}
#endif /* DEBUG_LOCKS */
	clear_bit(31,&(rw)->lock);
}

void __lock_kernel(struct task_struct *task)
{
#ifdef DEBUG_LOCKS
	if ( (signed long)(task->lock_depth) < 0 )
	{
		printk("__lock_kernel(): %s/%d (nip %08lX) lock depth %x\n",
		      task->comm,task->pid,task->tss.regs->nip,
		      task->lock_depth);
	}
#endif /* DEBUG_LOCKS */
	/* mine! */
	if ( atomic_inc_return((atomic_t *) &task->lock_depth) == 1 )
		klock_info.akp = smp_processor_id();
	/* my kernel mode! mine!!! */
}
 
void __unlock_kernel(struct task_struct *task)
{
#ifdef DEBUG_LOCKS
 	if ( task->lock_depth == 0 )
	{
		printk("__unlock_kernel(): %s/%d (nip %08lX) lock depth %x\n",
		      task->comm,task->pid,task->tss.regs->nip,
		      task->lock_depth);
		klock_info.akp = NO_PROC_ID;		
		klock_info.kernel_flag = 0;
		return;
	}
#endif /* DEBUG_LOCKS */
	if ( atomic_dec_and_test((atomic_t *) &task->lock_depth) )
	{
		klock_info.akp = NO_PROC_ID;		
		klock_info.kernel_flag = 0;
	}
}	

void reacquire_kernel_lock(struct task_struct *task, int cpu,int depth)
{
	if (depth)
	{
		__cli();
		__lock_kernel(task);
		task->lock_depth = depth;
		__sti();
       }
}

