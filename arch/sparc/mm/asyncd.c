/*  $Id: asyncd.c,v 1.12 1998/09/13 04:30:30 davem Exp $
 *  The asyncd kernel daemon. This handles paging on behalf of 
 *  processes that receive page faults due to remote (async) memory
 *  accesses. 
 *
 *  Idea and skeleton code courtesy of David Miller (bless his cotton socks)
 *
 *  Implemented by tridge
 */

#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/config.h>
#include <linux/interrupt.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

#define DEBUG 0

#define WRITE_LIMIT 100
#define LOOP_LIMIT 200

static struct {
	int faults, read, write, success, failure, errors;
} stats;

/* 
 * The wait queue for waking up the async daemon:
 */
static struct wait_queue * asyncd_wait = NULL;

struct async_job {
	volatile struct async_job *next;
	int taskid;
	struct mm_struct *mm;
	unsigned long address;
	int write;
	void (*callback)(int,unsigned long,int,int);
};

static volatile struct async_job *async_queue = NULL;
static volatile struct async_job *async_queue_end = NULL;

static void add_to_async_queue(int taskid,
			       struct mm_struct *mm,
			       unsigned long address,
			       int write,
			       void (*callback)(int,unsigned long,int,int))
{
	struct async_job *a = kmalloc(sizeof(*a),GFP_ATOMIC);

	if (!a) {
		printk("ERROR: out of memory in asyncd\n");
		a->callback(taskid,address,write,1);
		return;
	}

	if (write)
		stats.write++;
	else
		stats.read++;

	a->next = NULL;
	a->taskid = taskid;
	a->mm = mm;
	a->address = address;
	a->write = write;
	a->callback = callback;

	if (!async_queue) {
		async_queue = a;
	} else {
		async_queue_end->next = a;
	}
	async_queue_end = a;
}


void async_fault(unsigned long address, int write, int taskid,
		 void (*callback)(int,unsigned long,int,int))
{
	struct task_struct *tsk = task[taskid];
	struct mm_struct *mm = tsk->mm;

	stats.faults++;

#if 0
	printk("paging in %x for task=%d\n",address,taskid);
#endif

	add_to_async_queue(taskid, mm, address, write, callback);
	wake_up(&asyncd_wait);  
	mark_bh(TQUEUE_BH);
}

static int fault_in_page(int taskid,
			 struct vm_area_struct *vma,
			 unsigned address,int write)
{
	static unsigned last_address;
	static int last_task, loop_counter;
	struct task_struct *tsk = task[taskid];
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if (!tsk || !tsk->mm)
		return 1;

	if (!vma || (write && !(vma->vm_flags & VM_WRITE)))
	  goto bad_area;
	if (vma->vm_start > address)
	  goto bad_area;

	if (address == last_address && taskid == last_task) {
		loop_counter++;
	} else {
		loop_counter = 0;
		last_address = address; 
		last_task = taskid;
	}

	if (loop_counter == WRITE_LIMIT && !write) {
		printk("MSC bug? setting write request\n");
		stats.errors++;
		write = 1;
	}

	if (loop_counter == LOOP_LIMIT) {
		printk("MSC bug? failing request\n");
		stats.errors++;
		return 1;
	}

	pgd = pgd_offset(vma->vm_mm, address);
	pmd = pmd_alloc(pgd,address);
	if(!pmd)
		goto no_memory;
	pte = pte_alloc(pmd, address);
	if(!pte)
		goto no_memory;
	if(!pte_present(*pte)) {
		handle_mm_fault(tsk, vma, address, write);
		goto finish_up;
	}
	set_pte(pte, pte_mkyoung(*pte));
	flush_tlb_page(vma, address);
	if(!write)
		goto finish_up;
	if(pte_write(*pte)) {
		set_pte(pte, pte_mkdirty(*pte));
		flush_tlb_page(vma, address);
		goto finish_up;
	}
	handle_mm_fault(tsk, vma, address, write);

	/* Fall through for do_wp_page */
finish_up:
	stats.success++;
	return 0;

no_memory:
	stats.failure++;
	oom(tsk);
	return 1;
	
bad_area:	  
	stats.failure++;
	tsk->tss.sig_address = address;
	tsk->tss.sig_desc = SUBSIG_NOMAPPING;
	send_sig(SIGSEGV, tsk, 1);
	return 1;
}


/* Note the semaphore operations must be done here, and _not_
 * in async_fault().
 */
static void run_async_queue(void)
{
	int ret;
	unsigned flags;

	while (async_queue) {
		volatile struct async_job *a;
		struct mm_struct *mm;
		struct vm_area_struct *vma;

		save_flags(flags); cli();
		a = async_queue;
		async_queue = async_queue->next;
		restore_flags(flags);

		mm = a->mm;

		down(&mm->mmap_sem);
		vma = find_vma(mm, a->address);
		ret = fault_in_page(a->taskid,vma,a->address,a->write);
#if DEBUG
		printk("fault_in_page(task=%d addr=%x write=%d) = %d\n",
		       a->taskid,a->address,a->write,ret);
#endif
		a->callback(a->taskid,a->address,a->write,ret);
		up(&mm->mmap_sem);
		kfree_s((void *)a,sizeof(*a));
	}
}


#if CONFIG_AP1000
static void asyncd_info(void)
{
	printk("CID(%d) faults: total=%d  read=%d  write=%d  success=%d fail=%d err=%d\n",
	       mpp_cid(),stats.faults, stats.read, stats.write, stats.success,
	       stats.failure, stats.errors);
}
#endif


/*
 * The background async daemon.
 * Started as a kernel thread from the init process.
 */
int asyncd(void *unused)
{
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "asyncd");
	sigfillset(&current->blocked); /* block all signals */
	recalc_sigpending(current);
  
	/* Give asyncd a realtime priority. */
	current->policy = SCHED_FIFO;
	current->priority = 32;  /* Fixme --- we need to standardise our
				    namings for POSIX.4 realtime scheduling
				    priorities.  */
  
	printk("Started asyncd\n");

#if CONFIG_AP1000
	bif_add_debug_key('a',asyncd_info,"stats on asyncd");
#endif

	while (1) {
		unsigned flags;

		save_flags(flags); cli();

		while (!async_queue) {
			spin_lock_irq(&current->sigmask_lock);
			flush_signals(current);
			spin_unlock_irq(&current->sigmask_lock);
			interruptible_sleep_on(&asyncd_wait);
		}

		restore_flags(flags);

		run_async_queue();
	}
}

