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
static int try_to_swap_out(struct task_struct * tsk, struct vm_area_struct* vma,
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

	if (pte_young(pte)) {
		/*
		 * Transfer the "accessed" bit from the page
		 * tables to the global page map.
		 */
		set_pte(page_table, pte_mkold(pte));
		set_bit(PG_referenced, &page_map->flags);
		return 0;
	}

	/*
	 * Is the page already in the swap cache? If so, then
	 * we can just drop our reference to it without doing
	 * any IO - it's already up-to-date on disk.
	 *
	 * Return 0, as we didn't actually free any real
	 * memory, and we should just continue our scan.
	 */
	if (PageSwapCache(page_map)) {
		entry = page_map->offset;
		swap_duplicate(entry);
		set_pte(page_table, __pte(entry));
drop_pte:
		vma->vm_mm->rss--;
		flush_tlb_page(vma, address);
		__free_page(page_map);
		return 0;
	}

	/*
	 * Is it a clean page? Then it must be recoverable
	 * by just paging it in again, and we can just drop
	 * it..
	 *
	 * However, this won't actually free any real
	 * memory, as the page will just be in the page cache
	 * somewhere, and as such we should just continue
	 * our scan.
	 *
	 * Basically, this just makes it possible for us to do
	 * some real work in the future in "shrink_mmap()".
	 */
	if (!pte_dirty(pte)) {
		pte_clear(page_table);
		goto drop_pte;
	}

	/*
	 * Don't go down into the swap-out stuff if
	 * we cannot do I/O! Avoid recursing on FS
	 * locks etc.
	 */
	if (!(gfp_mask & __GFP_IO))
		return 0;

	/*
	 * Ok, it's really dirty. That means that
	 * we should either create a new swap cache
	 * entry for it, or we should write it back
	 * to its own backing store.
	 *
	 * Note that in neither case do we actually
	 * know that we make a page available, but
	 * as we potentially sleep we can no longer
	 * continue scanning, so we migth as well
	 * assume we free'd something.
	 *
	 * NOTE NOTE NOTE! This should just set a
	 * dirty bit in page_map, and just drop the
	 * pte. All the hard work would be done by
	 * shrink_mmap().
	 *
	 * That would get rid of a lot of problems.
	 */
	flush_cache_page(vma, address);
	if (vma->vm_ops && vma->vm_ops->swapout) {
		pid_t pid = tsk->pid;
		pte_clear(page_table);
		flush_tlb_page(vma, address);
		vma->vm_mm->rss--;
		
		if (vma->vm_ops->swapout(vma, page_map))
			kill_proc(pid, SIGBUS, 1);
		__free_page(page_map);
		return 1;
	}

	/*
	 * This is a dirty, swappable page.  First of all,
	 * get a suitable swap entry for it, and make sure
	 * we have the swap cache set up to associate the
	 * page with that swap entry.
	 */
	entry = get_swap_page();
	if (!entry)
		return 0; /* No swap space left */
		
	vma->vm_mm->rss--;
	tsk->nswap++;
	set_pte(page_table, __pte(entry));
	flush_tlb_page(vma, address);
	swap_duplicate(entry);	/* One for the process, one for the swap cache */
	add_to_swap_cache(page_map, entry);
	/* We checked we were unlocked way up above, and we
	   have been careful not to stall until here */
	set_bit(PG_locked, &page_map->flags);

	/* OK, do a physical asynchronous write to swap.  */
	rw_swap_page(WRITE, entry, (char *) page, 0);

	__free_page(page_map);
	return 1;
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
	 *   Pass 1: select the swappable task with maximal RSS that has
	 *         not yet been swapped out. 
	 *   Pass 2: re-assign rss swap_cnt values, then select as above.
	 *
	 * With this approach, there's no need to remember the last task
	 * swapped out.  If the swap-out fails, we clear swap_cnt so the 
	 * task won't be selected again until all others have been tried.
	 *
	 * Think of swap_cnt as a "shadow rss" - it tells us which process
	 * we want to page out (always try largest first).
	 */
	counter = nr_tasks / (priority+1);
	if (counter < 1)
		counter = 1;
	if (counter > nr_tasks)
		counter = nr_tasks;

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
			/* Refresh swap_cnt? */
			if (assign)
				p->swap_cnt = p->mm->rss;
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

		if (swap_out_process(pbest, gfp_mask))
			return 1;
	}
out:
	return 0;
}

/*
 * We need to make the locks finer granularity, but right
 * now we need this so that we can do page allocations
 * without holding the kernel lock etc.
 *
 * We want to try to free "count" pages, and we need to 
 * cluster them so that we get good swap-out behaviour. See
 * the "free_memory()" macro for details.
 */
static int do_try_to_free_pages(unsigned int gfp_mask)
{
	int priority;
	int count = SWAP_CLUSTER_MAX;

	lock_kernel();

	/* Always trim SLAB caches when memory gets low. */
	kmem_cache_reap(gfp_mask);

	priority = 6;
	do {
		while (shrink_mmap(priority, gfp_mask)) {
			if (!--count)
				goto done;
		}

		/* Try to get rid of some shared memory pages.. */
		if (gfp_mask & __GFP_IO) {
			while (shm_swap(priority, gfp_mask)) {
				if (!--count)
					goto done;
			}
		}

		/* Then, try to page stuff out.. */
		while (swap_out(priority, gfp_mask)) {
			if (!--count)
				goto done;
		}

		shrink_dcache_memory(priority, gfp_mask);
	} while (--priority >= 0);
done:
	unlock_kernel();

	return priority >= 0;
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

static struct task_struct *kswapd_process;

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process. 
 *
 * This basically executes once a second, trickling out pages
 * so that we have _some_ free memory available even if there
 * is no other activity that frees anything up. This is needed
 * for things like routing etc, where we otherwise might have
 * all activity going on in asynchronous contexts that cannot
 * page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
int kswapd(void *unused)
{
	struct task_struct *tsk = current;

	kswapd_process = tsk;
	tsk->session = 1;
	tsk->pgrp = 1;
	strcpy(tsk->comm, "kswapd");
	sigfillset(&tsk->blocked);
	
	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__get_free_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC;

	while (1) {
		/*
		 * Wake up once a second to see if we need to make
		 * more memory available.
		 *
		 * If we actually get into a low-memory situation,
		 * the processes needing more memory will wake us
		 * up on a more timely basis.
		 */
		do {
			if (nr_free_pages >= freepages.high)
				break;

			if (!do_try_to_free_pages(GFP_KSWAPD))
				break;
		} while (!tsk->need_resched);
		run_task_queue(&tq_disk);
		tsk->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ);
	}
}

/*
 * Called by non-kswapd processes when they want more
 * memory.
 *
 * In a perfect world, this should just wake up kswapd
 * and return. We don't actually want to swap stuff out
 * from user processes, because the locking issues are
 * nasty to the extreme (file write locks, and MM locking)
 *
 * One option might be to let kswapd do all the page-out
 * and VM page table scanning that needs locking, and this
 * process thread could do just the mmap shrink stage that
 * can be done by just dropping cached pages without having
 * any deadlock issues.
 */
int try_to_free_pages(unsigned int gfp_mask)
{
	int retval = 1;

	wake_up_process(kswapd_process);
	if (gfp_mask & __GFP_WAIT)
		retval = do_try_to_free_pages(gfp_mask);
	return retval;
}
	
