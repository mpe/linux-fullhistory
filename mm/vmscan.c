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
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/uaccess.h> /* for copy_to/from_user */
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
	 * is oldest). */
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
		if (vma->vm_ops && vma->vm_ops->swapout) {
			pid_t pid = tsk->pid;
			vma->vm_mm->rss--;
			if (vma->vm_ops->swapout(vma, address - vma->vm_start + vma->vm_offset, page_table))
				kill_proc(pid, SIGBUS, 1);
		} else {
			if (atomic_read(&page_map->count) != 1)
				return 0;
			if (!(entry = get_swap_page()))
				return 0;
			vma->vm_mm->rss--;
			flush_cache_page(vma, address);
			set_pte(page_table, __pte(entry));
			flush_tlb_page(vma, address);
			tsk->nswap++;
			rw_swap_page(WRITE, entry, (char *) page, wait);
		}
		free_page(page);
		return 1;	/* we slept: the process may not exist any more */
	}
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

static int swap_out(unsigned int priority, int dma, int wait)
{
	static int skip_factor = 0;
	int limit = nr_tasks - 1;
	int loop, counter, i;
	struct task_struct *p;

	counter = ((PAGEOUT_WEIGHT * nr_tasks) >> 10) >> priority;
	if(skip_factor > nr_tasks)
		skip_factor = 0;

	read_lock(&tasklist_lock);
	p = init_task.next_task;
	i = skip_factor;
	while(i--)
		p = p->next_task;
	for(; counter >= 0; counter--) {
		/* Check if task is suitable for swapping. */
		loop = 0;
		while(1) {
			if(!--limit) {
				limit = nr_tasks - 1;
				/* See if all processes are unswappable or
				 * already swapped out.
				 */
				if (loop)
					goto out;
				loop = 1;
			}
			if (p->swappable && p->mm->rss)
				break;
			if((p = p->next_task) == &init_task)
				p = p->next_task;
		}
		skip_factor++;

		/* Determine the number of pages to swap from this process. */
		if (!p->swap_cnt) {
			/* Normalise the number of pages swapped by
			   multiplying by (RSS / 1MB) */
			p->swap_cnt = AGE_CLUSTER_SIZE(p->mm->rss);
		}
		if (!--p->swap_cnt)
			skip_factor++;
		read_unlock(&tasklist_lock);

		switch (swap_out_process(p, dma, wait)) {
		case 0:
			if (p->swap_cnt)
				skip_factor++;
			break;
		case 1:
			return 1;
		default:
			break;
		};

		/* Whoever we swapped may not even exist now, in fact we cannot
		 * assume anything about the list we were searching previously.
		 */
		read_lock(&tasklist_lock);
		p = init_task.next_task;
		i = skip_factor;
		while(i--)
			p = p->next_task;
	}
out:
	read_unlock(&tasklist_lock);
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
			shrink_dcache();
			state = 2;
		case 2:
			/*
			 * We shouldn't have a priority here:
			 * If we're low on memory we should
			 * unconditionally throw away _all_
			 * kmalloc caches!
			 */
			if (kmem_cache_reap(0, dma, wait))
				return 1;
			state = 3;
		case 3:
			if (shm_swap(i, dma))
				return 1;
			state = 4;
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
 * The background pageout daemon.
 * Started as a kernel thread from the init process.
 */
int kswapd(void *unused)
{
	int i;
	char *revision="$Revision: 1.23 $", *s, *e;
	
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "kswapd");
	current->blocked = ~0UL;
	
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
	
	if ((s = strchr(revision, ':')) &&
	    (e = strchr(s, '$')))
		s++, i = e - s;
	else
		s = revision, i = -1;
	printk ("Started kswapd v%.*s\n", i, s);

	while (1) {
		kswapd_awake = 0;
		current->signal = 0;
		run_task_queue(&tq_disk);
		interruptible_sleep_on(&kswapd_wait);
		kswapd_awake = 1;
		swapstats.wakeups++;
		/* Do the background pageout: 
		 * We now only swap out as many pages as needed.
		 * When we are truly low on memory, we swap out
		 * synchronously (WAIT == 1).  -- Rik.
		 */
		while(nr_free_pages < min_free_pages)
			try_to_free_page(GFP_KERNEL, 0, 1);
		while((nr_free_pages + atomic_read(&nr_async_pages)) < free_pages_low)
			try_to_free_page(GFP_KERNEL, 0, 1);
		while((nr_free_pages + atomic_read(&nr_async_pages)) < free_pages_high)
			try_to_free_page(GFP_KERNEL, 0, 0);
	}
}

/* 
 * The swap_tick function gets called on every clock tick.
 */

void swap_tick(void)
{
	int	want_wakeup = 0;
	static int	last_wakeup_low = 0;

	if ((nr_free_pages + atomic_read(&nr_async_pages)) < free_pages_low) {
		if (last_wakeup_low)
			want_wakeup = jiffies >= next_swap_jiffies;
		else
			last_wakeup_low = want_wakeup = 1;
	}
	else if (((nr_free_pages + atomic_read(&nr_async_pages)) < free_pages_high) && 
	         jiffies >= next_swap_jiffies) {
		last_wakeup_low = 0;
		want_wakeup = 1;
	}

	if (want_wakeup) { 
		if (!kswapd_awake) {
			wake_up(&kswapd_wait);
			need_resched = 1;
		}
		/* low on memory, we need to start swapping soon */
		if(last_wakeup_low) 
			next_swap_jiffies = jiffies;
		else  
			next_swap_jiffies = jiffies + swapout_interval;
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
