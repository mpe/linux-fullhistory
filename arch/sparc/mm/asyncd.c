/*  $Id: asyncd.c,v 1.8 1996/09/21 04:30:12 davem Exp $
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
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

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

	if (!a)
		panic("out of memory in asyncd\n");

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

#if 0
	printk("paging in %x for task=%d\n",address,taskid);
#endif
	add_to_async_queue(taskid, mm, address, write, callback);
	wake_up(&asyncd_wait);  
}

static int fault_in_page(int taskid,
			 struct vm_area_struct *vma,
			 unsigned address,int write)
{
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
	
	pgd = pgd_offset(vma->vm_mm, address);
	pmd = pmd_alloc(pgd,address);
	if(!pmd)
		goto no_memory;
	pte = pte_alloc(pmd, address);
	if(!pte)
		goto no_memory;
	if(!pte_present(*pte)) {
		do_no_page(tsk, vma, address, write);
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
	do_wp_page(tsk, vma, address, write);

	/* Fall through for do_wp_page */
finish_up:
	update_mmu_cache(vma, address, *pte);
	return 0;

no_memory:
	oom(tsk);
	return 1;
	
bad_area:	  
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
	while (async_queue) {
		volatile struct async_job *a = async_queue;
		struct mm_struct *mm = a->mm;
		struct vm_area_struct *vma;
		async_queue = async_queue->next;
		down(&mm->mmap_sem);
		vma = find_vma(mm, a->address);
		ret = fault_in_page(a->taskid,vma,a->address,a->write);
		a->callback(a->taskid,a->address,a->write,ret);
		up(&mm->mmap_sem);
		kfree_s((void *)a,sizeof(*a));
	}
}




/*
 * The background async daemon.
 * Started as a kernel thread from the init process.
 */
int asyncd(void *unused)
{
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "asyncd");
	current->blocked = ~0UL; /* block all signals */
  
	/* Give kswapd a realtime priority. */
	current->policy = SCHED_FIFO;
	current->priority = 32;  /* Fixme --- we need to standardise our
				    namings for POSIX.4 realtime scheduling
				    priorities.  */
  
	printk("Started asyncd\n");
  
	while (1) {
		current->signal = 0;
		interruptible_sleep_on(&asyncd_wait);
		run_async_queue();
	}
}

