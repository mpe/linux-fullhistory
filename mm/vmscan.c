/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Version: $Id: vmscan.c,v 1.5 1998/02/23 22:14:28 sct Exp $
 */

#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include <asm/pgtable.h>

/* 
 * The wait queue for waking up the pageout daemon:
 */
static struct task_struct * kswapd_task = NULL;

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
	unsigned long address, pte_t * page_table, int gfp_mask)
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
	    || ((gfp_mask & __GFP_DMA) && !PageDMA(page_map)))
		return 0;

	/* 
	 * Deal with page aging.  There are several special cases to
	 * consider:
	 * 
	 * Page has been accessed, but is swap cached.  If the page is
	 * getting sufficiently "interesting" --- its age is getting
	 * high --- then if we are sufficiently short of free swap
	 * pages, then delete the swap cache.  We can only do this if
	 * the swap page's reference count is one: ie. there are no
	 * other references to it beyond the swap cache (as there must
	 * still be PTEs pointing to it if count > 1).
	 * 
	 * If the page has NOT been touched, and its age reaches zero,
	 * then we are swapping it out:
	 *
	 *   If there is already a swap cache page for this page, then
	 *   another process has already allocated swap space, so just
	 *   dereference the physical page and copy in the swap entry
	 *   from the swap cache.  
	 * 
	 * Note, we rely on all pages read in from swap either having
	 * the swap cache flag set, OR being marked writable in the pte,
	 * but NEVER BOTH.  (It IS legal to be neither cached nor dirty,
	 * however.)
	 *
	 * -- Stephen Tweedie 1998 */

	if (PageSwapCache(page_map)) {
		if (pte_write(pte)) {
			struct page *found;
			printk ("VM: Found a writable swap-cached page!\n");
			/* Try to diagnose the problem ... */
			found = find_page(&swapper_inode, page_map->offset);
			if (found) {
				printk("page=%p@%08lx, found=%p, count=%d\n",
					page_map, page_map->offset,
					found, atomic_read(&found->count));
				__free_page(found);
			} else 
				printk ("Spurious, page not in cache\n");
			return 0;
		}
	}
	
	if (pte_young(pte)) {
		/*
		 * Transfer the "accessed" bit from the page
		 * tables to the global page map.
		 */
		set_pte(page_table, pte_mkold(pte));
		set_bit(PG_referenced, &page_map->flags);

		/* 
		 * We should test here to see if we want to recover any
		 * swap cache page here.  We do this if the page seeing
		 * enough activity, AND we are sufficiently low on swap
		 *
		 * We need to track both the number of available swap
		 * pages and the total number present before we can do
		 * this...  
		 */
		return 0;
	}

	if (pte_dirty(pte)) {
		if (vma->vm_ops && vma->vm_ops->swapout) {
			pid_t pid = tsk->pid;
			vma->vm_mm->rss--;
			if (vma->vm_ops->swapout(vma, address - vma->vm_start + vma->vm_offset, page_table))
				kill_proc(pid, SIGBUS, 1);
		} else {
			/*
			 * This is a dirty, swappable page.  First of all,
			 * get a suitable swap entry for it, and make sure
			 * we have the swap cache set up to associate the
			 * page with that swap entry.
			 */
        		entry = in_swap_cache(page_map);
			if (!entry) {
				entry = get_swap_page();
				if (!entry)
					return 0; /* No swap space left */
			}
			
			vma->vm_mm->rss--;
			tsk->nswap++;
			flush_cache_page(vma, address);
			set_pte(page_table, __pte(entry));
			flush_tlb_page(vma, address);
			swap_duplicate(entry);

			/* Now to write back the page.  We have two
			 * cases: if the page is already part of the
			 * swap cache, then it is already on disk.  Just
			 * free the page and return (we release the swap
			 * cache on the last accessor too).
			 *
			 * If we have made a new swap entry, then we
			 * start the write out to disk.  If the page is
			 * shared, however, we still need to keep the
			 * copy in memory, so we add it to the swap
			 * cache. */
			if (PageSwapCache(page_map)) {
				free_page(page);
				return (atomic_read(&page_map->count) == 0);
			}
			add_to_swap_cache(page_map, entry);
			/* We checked we were unlocked way up above, and we
			   have been careful not to stall until here */
			set_bit(PG_locked, &page_map->flags);
			/* OK, do a physical write to swap.  */
			rw_swap_page(WRITE, entry, (char *) page, (gfp_mask & __GFP_WAIT));
		}
		/* Now we can free the current physical page.  We also
		 * free up the swap cache if this is the last use of the
		 * page.  Note that there is a race here: the page may
		 * still be shared COW by another process, but that
		 * process may exit while we are writing out the page
		 * asynchronously.  That's no problem, shrink_mmap() can
		 * correctly clean up the occassional unshared page
		 * which gets left behind in the swap cache. */
		free_page(page);
		return 1;	/* we slept: the process may not exist any more */
	}

	/* The page was _not_ dirty, but still has a zero age.  It must
	 * already be uptodate on disk.  If it is in the swap cache,
	 * then we can just unlink the page now.  Remove the swap cache
	 * too if this is the last user.  */
        if ((entry = in_swap_cache(page_map)))  {
		vma->vm_mm->rss--;
		flush_cache_page(vma, address);
		set_pte(page_table, __pte(entry));
		flush_tlb_page(vma, address);
		swap_duplicate(entry);
		free_page(page);
		return (atomic_read(&page_map->count) == 0);
	} 
	/* 
	 * A clean page to be discarded?  Must be mmap()ed from
	 * somewhere.  Unlink the pte, and tell the filemap code to
	 * discard any cached backing page if this is the last user.
	 */
	if (PageSwapCache(page_map)) {
		printk ("VM: How can this page _still_ be cached?");
		return 0;
	}
	vma->vm_mm->rss--;
	flush_cache_page(vma, address);
	pte_clear(page_table);
	flush_tlb_page(vma, address);
	entry = (atomic_read(&page_map->count) == 1);
	__free_page(page_map);
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
	pmd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
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
		result = try_to_swap_out(tsk, vma, address, pte, gfp_mask);
		if (result)
			return result;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int swap_out_pgd(struct task_struct * tsk, struct vm_area_struct * vma,
	pgd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
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
		int result = swap_out_pmd(tsk, vma, pmd, address, end, gfp_mask);
		if (result)
			return result;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int swap_out_vma(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long address, int gfp_mask)
{
	pgd_t *pgdir;
	unsigned long end;

	/* Don't swap out areas like shared memory which have their
	    own separate swapping mechanism or areas which are locked down */
	if (vma->vm_flags & (VM_SHM | VM_LOCKED))
		return 0;

	pgdir = pgd_offset(tsk->mm, address);

	end = vma->vm_end;
	while (address < end) {
		int result = swap_out_pgd(tsk, vma, pgdir, address, end, gfp_mask);
		if (result)
			return result;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int swap_out_process(struct task_struct * p, int gfp_mask)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = p->swap_address;

	/*
	 * Find the proper vm-area
	 */
	vma = find_vma(p->mm, address);
	if (vma) {
		if (address < vma->vm_start)
			address = vma->vm_start;

		for (;;) {
			int result = swap_out_vma(p, vma, address, gfp_mask);
			if (result)
				return result;
			vma = vma->vm_next;
			if (!vma)
				break;
			address = vma->vm_start;
		}
	}

	/* We didn't find anything for the process */
	p->swap_cnt = 0;
	p->swap_address = 0;
	return 0;
}

/*
 * Select the task with maximal swap_cnt and try to swap out a page.
 * N.B. This function returns only 0 or 1.  Return values != 1 from
 * the lower level routines result in continued processing.
 */
static int swap_out(unsigned int priority, int gfp_mask)
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

		/*
		 * Nonzero means we cleared out something, but only "1" means
		 * that we actually free'd up a page as a result.
		 */
		if (swap_out_process(pbest, gfp_mask) == 1)
				return 1;
	}
out:
	return 0;
}

/*
 * Before we start the kernel thread, print out the 
 * kswapd initialization message (otherwise the init message 
 * may be printed in the middle of another driver's init 
 * message).  It looks very bad when that happens.
 */
void __init kswapd_setup(void)
{
       int i;
       char *revision="$Revision: 1.5 $", *s, *e;

       swap_setup();
       
       if ((s = strchr(revision, ':')) &&
           (e = strchr(s, '$')))
               s++, i = e - s;
       else
               s = revision, i = -1;
       printk ("Starting kswapd v%.*s\n", i, s);
}

#define free_memory(fn) \
	count++; do { if (!--count) goto done; } while (fn)

static int kswapd_free_pages(int kswapd_state)
{
	unsigned long end_time;

	/* Always trim SLAB caches when memory gets low. */
	kmem_cache_reap(0);

	/* max one hundreth of a second */
	end_time = jiffies + (HZ-1)/100;
	do {
		int priority = 5;
		int count = pager_daemon.swap_cluster;

		switch (kswapd_state) {
			do {
			default:
				free_memory(shrink_mmap(priority, 0));
				kswapd_state++;
			case 1:
				free_memory(shm_swap(priority, 0));
				kswapd_state++;
			case 2:
				free_memory(swap_out(priority, 0));
				shrink_dcache_memory(priority, 0);
				kswapd_state = 0;
			} while (--priority >= 0);
			return kswapd_state;
		}
done:
		if (nr_free_pages > freepages.high + pager_daemon.swap_cluster)
			break;
	} while (time_before_eq(jiffies,end_time));
	return kswapd_state;
}

/*
 * The background pageout daemon.
 * Started as a kernel thread from the init process.
 */
int kswapd(void *unused)
{
	current->session = 1;
	current->pgrp = 1;
	strcpy(current->comm, "kswapd");
	sigfillset(&current->blocked);
	
	/*
	 *	As a kernel thread we want to tamper with system buffers
	 *	and other internals and thus be subject to the SMP locking
	 *	rules. (On a uniprocessor box this does nothing).
	 */
	lock_kernel();

	/*
	 * Set the base priority to something smaller than a
	 * regular process. We will scale up the priority
	 * dynamically depending on how much memory we need.
	 */
	current->priority = (DEF_PRIORITY * 2) / 3;

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "try_to_free_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	current->flags |= PF_MEMALLOC;

	init_swap_timer();
	kswapd_task = current;
	while (1) {
		int state = 0;

		current->state = TASK_INTERRUPTIBLE;
		flush_signals(current);
		run_task_queue(&tq_disk);
		schedule();
		swapstats.wakeups++;
		state = kswapd_free_pages(state);
	}
	/* As if we could ever get here - maybe we want to make this killable */
	kswapd_task = NULL;
	unlock_kernel();
	return 0;
}

/*
 * We need to make the locks finer granularity, but right
 * now we need this so that we can do page allocations
 * without holding the kernel lock etc.
 *
 * The "PF_MEMALLOC" flag protects us against recursion:
 * if we need more memory as part of a swap-out effort we
 * will just silently return "success" to tell the page
 * allocator to accept the allocation.
 *
 * We want to try to free "count" pages, and we need to 
 * cluster them so that we get good swap-out behaviour. See
 * the "free_memory()" macro for details.
 */
int try_to_free_pages(unsigned int gfp_mask, int count)
{
	int retval;

	lock_kernel();

	/* Always trim SLAB caches when memory gets low. */
	kmem_cache_reap(gfp_mask);

	retval = 1;
	if (!(current->flags & PF_MEMALLOC)) {
		int priority;

		current->flags |= PF_MEMALLOC;
	
		priority = 5;
		do {
			shrink_dcache_memory(priority, gfp_mask);
			free_memory(shrink_mmap(priority, gfp_mask));
			free_memory(shm_swap(priority, gfp_mask));
			free_memory(swap_out(priority, gfp_mask));
		} while (--priority >= 0);
		retval = 0;
done:
		current->flags &= ~PF_MEMALLOC;
	}
	unlock_kernel();

	return retval;
}

/*
 * Wake up kswapd according to the priority
 *	0 - no wakeup
 *	1 - wake up as a low-priority process
 *	2 - wake up as a normal process
 *	3 - wake up as an almost real-time process
 *
 * This plays mind-games with the "goodness()"
 * function in kernel/sched.c.
 */
static inline void kswapd_wakeup(struct task_struct *p, int priority)
{
	if (priority) {
		p->counter = p->priority << priority;
		wake_up_process(p);
	}
}

/* 
 * The swap_tick function gets called on every clock tick.
 */
void swap_tick(void)
{
	struct task_struct *p = kswapd_task;

	/*
	 * Only bother to try to wake kswapd up
	 * if the task exists and can be woken.
	 */
	if (p && (p->state & TASK_INTERRUPTIBLE)) {
		unsigned int pages;
		int want_wakeup;

		/*
		 * Schedule for wakeup if there isn't lots
		 * of free memory or if there is too much
		 * of it used for buffers or pgcache.
		 *
		 * "want_wakeup" is our priority: 0 means
		 * not to wake anything up, while 3 means
		 * that we'd better give kswapd a realtime
		 * priority.
		 */
		want_wakeup = 0;
		pages = nr_free_pages;
		if (pages < freepages.high)
			want_wakeup = 1;
		if (pages < freepages.low)
			want_wakeup = 2;
		if (pages < freepages.min)
			want_wakeup = 3;
	
		kswapd_wakeup(p,want_wakeup);
	}

	timer_active |= (1<<SWAP_TIMER);
}

/* 
 * Initialise the swap timer
 */

void init_swap_timer(void)
{
	timer_table[SWAP_TIMER].expires = jiffies;
	timer_table[SWAP_TIMER].fn = swap_tick;
	timer_active |= (1<<SWAP_TIMER);
}
