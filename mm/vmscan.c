/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to free_pages_high: 2.4.97, Rik van Riel.
 *  Version: $Id: vmscan.c,v 1.23 1997/04/12 04:31:05 davem Exp $
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/dcache.h>

#include <asm/bitops.h>
#include <asm/pgtable.h>

/* 
 * When are we next due for a page scan? 
 */
static int next_swap_jiffies = 0;

/* 
 * How often do we do a pageout scan during normal conditions?
 * Default is four times a second.
 */
int swapout_interval = HZ / 4;

/* 
 * The wait queue for waking up the pageout daemon:
 */
static struct wait_queue * kswapd_wait = NULL;

/* 
 * We avoid doing a reschedule if the pageout daemon is already awake;
 */
static int kswapd_awake = 0;

static void init_swap_timer(void);

/*
 * The swap-out functions return 1 if they successfully
 * threw something out, and we got a free page. It returns
 * zero if it couldn't do anything, and any other value
 * indicates it decreased rss, but the page was shared.
 *
 * NOTE! If it sleeps, it *must* return 1 to make sure we
 * don't continue with the swap-out. Otherwise we may be
 * using a process that no longer actually exists (it might
 * have died while we slept).
 */
static inline int try_to_swap_out(struct task_struct * tsk, struct vm_area_struct* vma,
	unsigned long address, pte_t * page_table, int dma, int wait)
{
	pte_t pte;
	unsigned long entry;
	unsigned long page;
	struct page * page_map;

	pte = *page_table;
	if (!pte_present(pte))
		return 0;
	page = pte_page(pte);
	if (MAP_NR(page) >= max_mapnr)
		return 0;

	page_map = mem_map + MAP_NR(page);
	if (PageReserved(page_map)
	    || PageLocked(page_map)
	    || (dma && !PageDMA(page_map)))
		return 0;
	/* Deal with page aging.  Pages age from being unused; they
	 * rejuvenate on being accessed.  Only swap old pages (age==0
	 * is oldest).
	 *
	 * This test will no longer work once swap cached pages can be
	 * shared!  
	 */
	if ((pte_dirty(pte) && delete_from_swap_cache(page_map)) 
	    || pte_young(pte))  {
		set_pte(page_table, pte_mkold(pte));
		touch_page(page_map);
		return 0;
	}
	age_page(page_map);
	if (page_map->age)
		return 0;
	if (pte_dirty(pte)) {
		if (PageSwapCache(page_map))
			panic ("Can't still be swap cached!!!");
		if (vma->vm_ops && vma->vm_ops->swapout) {
			pid_t pid = tsk->pid;
			vma->vm_mm->rss--;
			if (vma->vm_ops->swapout(vma, address - vma->vm_start + vma->vm_offset, page_table))
				kill_proc(pid, SIGBUS, 1);
		} else {
			if (!(entry = get_swap_page()))
				return 0;
			vma->vm_mm->rss--;
			flush_cache_page(vma, address);
			set_pte(page_table, __pte(entry));
			flush_tlb_page(vma, address);
			tsk->nswap++;
			rw_swap_page(WRITE, entry, (char *) page, wait);
		}
		/* 
		 * For now, this is safe, because the test above makes
		 * sure that this page is currently not swap-cached.  
		 */
		if (PageSwapCache(page_map))
			panic ("Page became cached after IO");
		free_page(page);
		return 1;	/* we slept: the process may not exist any more */
	}
	/* 
	 * Eventually, find_in_swap_cache will be able to return true
	 * even for pages shared with other processes.  
	 */
        if ((entry = find_in_swap_cache(page_map)))  {
		if (atomic_read(&page_map->count) != 1) {
			set_pte(page_table, pte_mkdirty(pte));
			printk("Aiee.. duplicated cached swap-cache entry\n");
			return 0;
		}
		vma->vm_mm->rss--;
		flush_cache_page(vma, address);
		set_pte(page_table, __pte(entry));
		flush_tlb_page(vma, address);
		free_page(page);
		return 1;
	} 
	vma->vm_mm->rss--;
	flush_cache_page(vma, address);
	pte_clear(page_table);
	flush_tlb_page(vma, address);
	entry = page_unuse(page);
	if (PageSwapCache(page_map))
		panic ("How can this page _still_ be cached?");
	free_page(page);
	return entry;
}

/*
 * A new implementation of swap_out().  We do not swap complete processes,
 * but only a small number of blocks, before we continue with the next
 * process.  The number of blocks actually swapped is determined on the
 * number of page faults, that this process actually had in the last time,
 * so we won't swap heavily used processes all the time ...
 *
 * Note: the priority argument is a hint on much CPU to waste with the
 *       swap block search, not a hint, of how much blocks to swap with
 *       each process.
 *
 * (C) 1993 Kai Petzke, wpp@marie.physik.tu-berlin.de
 */

static inline int swap_out_pmd(struct task_struct * tsk, struct vm_area_struct * vma,
	pmd_t *dir, unsigned long address, unsigned long end, int dma, int wait)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("swap_out_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		int result;
		tsk->swap_address = address + PAGE_SIZE;
		result = try_to_swap_out(tsk, vma, address, pte, dma, wait);
		if (result)
			return result;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int swap_out_pgd(struct task_struct * tsk, struct vm_area_struct * vma,
	pgd_t *dir, unsigned long address, unsigned long end, int dma, int wait)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("swap_out_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (end > pgd_end)
		end = pgd_end;
	
	do {
		int result = swap_out_pmd(tsk, vma, pmd, address, end, dma, wait);
		if (result)
			return result;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int swap_out_vma(struct task_struct * tsk, struct vm_area_struct * vma,
	pgd_t *pgdir, unsigned long start, int dma, int wait)
{
	unsigned long end;

	/* Don't swap out areas like shared memory which have their
	    own separate swapping mechanism or areas which are locked down */
	if (vma->vm_flags & (VM_SHM | VM_LOCKED))
		return 0;

	end = vma->vm_end;
	while (start < end) {
		int result = swap_out_pgd(tsk, vma, pgdir, start, end, dma, wait);
		if (result)
			return result;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int swap_out_process(struct task_struct * p, int dma, int wait)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = p->swap_address;
	p->swap_address = 0;

	/*
	 * Find the proper vm-area
	 */
	vma = find_vma(p->mm, address);
	if (!vma)
		return 0;
	if (address < vma->vm_start)
		address = vma->vm_start;

	for (;;) {
		int result = swap_out_vma(p, vma, pgd_offset(p->mm, address), address, dma, wait);
		if (result)
			return result;
		vma = vma->vm_next;
		if (!vma)
			break;
		address = vma->vm_start;
	}
	p->swap_address = 0;
	return 0;
}

/*
 * Select the task with maximal swap_cnt and try to swap out a page.
 * N.B. This function returns only 0 or 1.  Return values != 1 from
 * the lower level routines result in continued processing.
 */
static int swap_out(unsigned int priority, int dma, int wait)
{
	struct task_struct * p, * pbest;
	int counter, assign, max_cnt;

	/* 
	 * We make one or two passes through the task list, indexed by 
	 * assign = {0, 1}:
	 *   Pass 1: select the swappable task with maximal swap_cnt.
	 *   Pass 2: assign new swap_cnt values, then select as above.
	 * With this approach, there's no need to remember the last task
	 * swapped out.  If the swap-out fails, we clear swap_cnt so the 
	 * task won't be selected again until all others have been tried.
	 */
	counter = ((PAGEOUT_WEIGHT * nr_tasks) >> 10) >> priority;
	for (; counter >= 0; counter--) {
		assign = 0;
		max_cnt = 0;
		pbest = NULL;
	select:
		read_lock(&tasklist_lock);
		p = init_task.next_task;
		for (; p != &init_task; p = p->next_task) {
			if (!p->swappable)
				continue;
	 		if (p->mm->rss <= 0)
				continue;
			if (assign) {
				/* 
				 * If we didn't select a task on pass 1, 
				 * assign each task a new swap_cnt.
				 * Normalise the number of pages swapped
				 * by multiplying by (RSS / 1MB)
				 */
				p->swap_cnt = AGE_CLUSTER_SIZE(p->mm->rss);
			}
			if (p->swap_cnt > max_cnt) {
				max_cnt = p->swap_cnt;
				pbest = p;
			}
		}
		read_unlock(&tasklist_lock);
		if (!pbest) {
			if (!assign) {
				assign = 1;
				goto select;
			}
			goto out;
		}
		pbest->swap_cnt--;

		switch (swap_out_process(pbest, dma, wait)) {
		case 0:
			/*
			 * Clear swap_cnt so we don't look at this task
			 * again until we've tried all of the others.
			 * (We didn't block, so the task is still here.)
			 */
			pbest->swap_cnt = 0;
			break;
		case 1:
			return 1;
		default:
			break;
		};
	}
out:
	return 0;
}

/*
 * We are much more aggressive about trying to swap out than we used
 * to be.  This works out OK, because we now do proper aging on page
 * contents. 
 */
static inline int do_try_to_free_page(int priority, int dma, int wait)
{
	static int state = 0;
	int i=6;
	int stop;

	/* Let the dcache know we're looking for memory ... */
	shrink_dcache_memory();
	/* Always trim SLAB caches when memory gets low. */
	(void) kmem_cache_reap(0, dma, wait);

	/* we don't try as hard if we're not waiting.. */
	stop = 3;
	if (wait)
		stop = 0;
	switch (state) {
		do {
		case 0:
			if (shrink_mmap(i, dma))
				return 1;
			state = 1;
		case 1:
			if (shm_swap(i, dma))
				return 1;
			state = 2;
		default:
			if (swap_out(i, dma, wait))
				return 1;
			state = 0;
		i--;
		} while ((i - stop) >= 0);
	}
	return 0;
}

/*
 * This is REALLY ugly.
 *
 * We need to make the locks finer granularity, but right
 * now we need this so that we can do page allocations
 * without holding the kernel lock etc.
 */
int try_to_free_page(int priority, int dma, int wait)
{
	int retval;

	lock_kernel();
	retval = do_try_to_free_page(priority,dma,wait);
	unlock_kernel();
	return retval;
}

/*
 * Before we start the kernel thread, print out the 
 * kswapd initialization message (otherwise the init message 
 * may be printed in the middle of another driver's init 
 * message).  It looks very bad when that happens.
 */
void kswapd_setup(void)
{
       int i;
       char *revision="$Revision: 1.23 $", *s, *e;

       if ((s = strchr(revision, ':')) &&
           (e = strchr(s, '$')))
               s++, i = e - s;
       else
               s = revision, i = -1;
       printk ("Starting kswapd v%.*s\n", i, s);
}

#define MAX_SWAP_FAIL 3
/*
 * The background pageout daemon.
 * Started as a kernel thread from the init process.
 */
int kswapd(void *unused)
{
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "kswapd");
	sigfillset(&current->blocked);
	
	/*
	 *	As a kernel thread we want to tamper with system buffers
	 *	and other internals and thus be subject to the SMP locking
	 *	rules. (On a uniprocessor box this does nothing).
	 */
	lock_kernel();

	/* Give kswapd a realtime priority. */
	current->policy = SCHED_FIFO;
	current->priority = 32;  /* Fixme --- we need to standardise our
				    namings for POSIX.4 realtime scheduling
				    priorities.  */

	init_swap_timer();
	
	while (1) {
		int fail;

		kswapd_awake = 0;
		flush_signals(current);
		run_task_queue(&tq_disk);
		interruptible_sleep_on(&kswapd_wait);
		kswapd_awake = 1;
		swapstats.wakeups++;
		/* Do the background pageout: 
		 * We now only swap out as many pages as needed.
		 * When we are truly low on memory, we swap out
		 * synchronously (WAIT == 1).  -- Rik.
		 * If we've had too many consecutive failures,
		 * go back to sleep to let other tasks run.
		 */
		for (fail = 0; fail++ < MAX_SWAP_FAIL;) {
			int pages, wait;

			pages = nr_free_pages;
			if (nr_free_pages >= min_free_pages)
				pages += atomic_read(&nr_async_pages);
			if (pages >= free_pages_high)
				break;
			wait = (pages < free_pages_low);
			if (try_to_free_page(GFP_KERNEL, 0, wait))
				fail = 0;
		}
		/*
		 * Report failure if we couldn't reach the minimum goal.
		 */
		if (nr_free_pages < min_free_pages)
			printk("kswapd: failed, got %d of %d\n",
				nr_free_pages, min_free_pages);
	}
}

/* 
 * The swap_tick function gets called on every clock tick.
 */

void swap_tick(void)
{
	int want_wakeup = 0, memory_low = 0;
	int pages = nr_free_pages + atomic_read(&nr_async_pages);

	if (pages < free_pages_low)
		memory_low = want_wakeup = 1;
	else if (pages < free_pages_high && jiffies >= next_swap_jiffies)
		want_wakeup = 1;

	if (want_wakeup) { 
		if (!kswapd_awake) {
			wake_up(&kswapd_wait);
			need_resched = 1;
		}
		/* Set the next wake-up time */
		next_swap_jiffies = jiffies;
		if (!memory_low) 
			next_swap_jiffies += swapout_interval;
	}
	timer_active |= (1<<SWAP_TIMER);
}

/* 
 * Initialise the swap timer
 */

void init_swap_timer(void)
{
	timer_table[SWAP_TIMER].expires = 0;
	timer_table[SWAP_TIMER].fn = swap_tick;
	timer_active |= (1<<SWAP_TIMER);
}
