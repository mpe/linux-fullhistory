/*
 * $Id: locks.c,v 1.21 1998/12/28 10:28:53 paulus Exp $
 *
 * Locks for smp ppc 
 * 
 * Written by Cort Dougan (cort@cs.nmt.edu)
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/spinlock.h>
#include <asm/io.h>

#define DEBUG_LOCKS 1

#undef INIT_STUCK
#define INIT_STUCK 200000000 /*0xffffffff*/

void _spin_lock(spinlock_t *lock)
{
	int cpu = smp_processor_id();
#ifdef DEBUG_LOCKS
	unsigned int stuck = INIT_STUCK;
#endif /* DEBUG_LOCKS */
	/* try expensive atomic load/store to get lock */
	while((unsigned long )xchg_u32((void *)&lock->lock,0xffffffff)) {
		/* try cheap load until it's free */
		while(lock->lock) {
#ifdef DEBUG_LOCKS
			if(!--stuck)
			{
				printk("_spin_lock(%p) CPU#%d NIP %p"
				       " holder: cpu %ld pc %08lX\n",
				       lock, cpu, __builtin_return_address(0),
				       lock->owner_cpu,lock->owner_pc);
				stuck = INIT_STUCK;
				/* steal the lock */
				/*xchg_u32((void *)&lock->lock,0);*/
			}
#endif /* DEBUG_LOCKS */
			barrier();
		}
	}
	lock->owner_pc = (unsigned long)__builtin_return_address(0);
	lock->owner_cpu = cpu;
}

int spin_trylock(spinlock_t *lock)
{
	unsigned long result;

	result = (unsigned long )xchg_u32((void *)&lock->lock,0xffffffff);
	if ( !result ) 
	{ 
		lock->owner_cpu = smp_processor_id(); 
		lock->owner_pc = (unsigned long)__builtin_return_address(0);
	} 
	return (result == 0);
}



void _spin_unlock(spinlock_t *lp)
{
#ifdef DEBUG_LOCKS
  	if ( !lp->lock )
		printk("_spin_unlock(%p): no lock cpu %d %s/%d\n", lp,
		      smp_processor_id(),current->comm,current->pid);
	if ( lp->owner_cpu != smp_processor_id() )
		printk("_spin_unlock(%p): cpu %d trying clear of cpu %d pc %lx val %lx\n",
		      lp, smp_processor_id(), (int)lp->owner_cpu,
		      lp->owner_pc,lp->lock);
#endif /* DEBUG_LOCKS */
	lp->owner_pc = lp->owner_cpu = 0;
	eieio();	/* actually I believe eieio only orders */
	lp->lock = 0;	/* non-cacheable accesses (on 604 at least) */
	eieio();	/*  - paulus. */
}
		
/*
 * Just like x86, implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "write" bit.
 * -- Cort
 */
void _read_lock(rwlock_t *rw)
{
#ifdef DEBUG_LOCKS
	unsigned long stuck = INIT_STUCK;
	int cpu = smp_processor_id();
#endif /* DEBUG_LOCKS */		  

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
#ifdef DEBUG_LOCKS
			if(!--stuck)
			{
				printk("_read_lock(%p) CPU#%d\n", rw, cpu);
				stuck = INIT_STUCK;
			}
#endif /* DEBUG_LOCKS */
		}
		/* try to get the read lock again */
		goto again;
	}
}

void _read_unlock(rwlock_t *rw)
{
#ifdef DEBUG_LOCKS
	if ( rw->lock == 0 )
		printk("_read_unlock(): %s/%d (nip %08lX) lock %lx\n",
		       current->comm,current->pid,current->tss.regs->nip,
		      rw->lock);
#endif /* DEBUG_LOCKS */
	atomic_dec((atomic_t *) &(rw)->lock);
}

void _write_lock(rwlock_t *rw)
{
#ifdef DEBUG_LOCKS
	unsigned long stuck = INIT_STUCK;
	int cpu = smp_processor_id();
#endif /* DEBUG_LOCKS */		  

again:	
	if ( test_and_set_bit(31,&(rw)->lock) ) /* someone has a write lock */
	{
		while ( (rw)->lock & (1<<31) ) /* wait for write lock */
		{
#ifdef DEBUG_LOCKS
			if(!--stuck)
			{
				printk("write_lock(%p) CPU#%d lock %lx)\n",
				       rw, cpu,rw->lock);
				stuck = INIT_STUCK;
			}
#endif /* DEBUG_LOCKS */		  
			barrier();
		}
		goto again;
	}
	
	if ( (rw)->lock & ~(1<<31)) /* someone has a read lock */
	{
		/* clear our write lock and wait for reads to go away */
		clear_bit(31,&(rw)->lock);
		while ( (rw)->lock & ~(1<<31) )
		{
#ifdef DEBUG_LOCKS
			if(!--stuck)
			{
				printk("write_lock(%p) 2 CPU#%d lock %lx)\n",
				       rw, cpu,rw->lock);
				stuck = INIT_STUCK;
			}
#endif /* DEBUG_LOCKS */
			barrier();
		}
		goto again;
	}
}

void _write_unlock(rwlock_t *rw)
{
#ifdef DEBUG_LOCKS
	if ( !(rw->lock & (1<<31)) )
		printk("_write_lock(): %s/%d (nip %08lX) lock %lx\n",
		      current->comm,current->pid,current->tss.regs->nip,
		      rw->lock);
#endif /* DEBUG_LOCKS */
	clear_bit(31,&(rw)->lock);
}

void __lock_kernel(struct task_struct *task)
{
#ifdef DEBUG_LOCKS
	unsigned long stuck = INIT_STUCK;
	
	if ( (signed long)(task->lock_depth) < 0 )
	{
		printk("__lock_kernel(): %s/%d (nip %08lX) lock depth %x\n",
		      task->comm,task->pid,task->tss.regs->nip,
		      task->lock_depth);
	}
#endif /* DEBUG_LOCKS */

	if ( atomic_inc_return((atomic_t *) &task->lock_depth) != 1 )
		return;
	/* mine! */
	while ( xchg_u32( (void *)&klock_info.kernel_flag, KLOCK_HELD) )
	{
		/* try cheap load until it's free */
		while(klock_info.kernel_flag) {
#ifdef DEBUG_LOCKS
			if(!--stuck)
			{
				printk("_lock_kernel() CPU#%d NIP %p\n",
				       smp_processor_id(),
				       __builtin_return_address(0));
				stuck = INIT_STUCK;
			}
#endif /* DEBUG_LOCKS */
			barrier();
		}
	}
	
	klock_info.akp = smp_processor_id();
	/* my kernel mode! mine!!! */
}

void __unlock_kernel(struct task_struct *task)
{
#ifdef DEBUG_LOCKS
 	if ( (task->lock_depth == 0) || (klock_info.kernel_flag != KLOCK_HELD) )
	{
		printk("__unlock_kernel(): %s/%d (nip %08lX) "
		       "lock depth %x flags %lx\n",
		       task->comm,task->pid,task->tss.regs->nip,
		       task->lock_depth, klock_info.kernel_flag);
		klock_info.akp = NO_PROC_ID;		
		klock_info.kernel_flag = 0;
		return;
	}
#endif /* DEBUG_LOCKS */
	if ( atomic_dec_and_test((atomic_t *) &task->lock_depth) )
	{
		klock_info.akp = NO_PROC_ID;
		klock_info.kernel_flag = KLOCK_CLEAR;
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
