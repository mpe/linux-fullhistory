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
#include <linux/highmem.h>
#include <linux/file.h>

#include <asm/pgalloc.h>

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
static int try_to_swap_out(struct vm_area_struct* vma, unsigned long address, pte_t * page_table, int gfp_mask)
{
	pte_t pte;
	swp_entry_t entry;
	struct page * page;
	int (*swapout)(struct page *, struct file *);

	pte = *page_table;
	if (!pte_present(pte))
		goto out_failed;
	page = pte_page(pte);
	if (page-mem_map >= max_mapnr)
		goto out_failed;

	/* Don't look at this pte if it's been accessed recently. */
	if (pte_young(pte)) {
		/*
		 * Transfer the "accessed" bit from the page
		 * tables to the global page map.
		 */
		set_pte(page_table, pte_mkold(pte));
		set_bit(PG_referenced, &page->flags);
		goto out_failed;
	}

	if (PageReserved(page) || PageLocked(page))
		goto out_failed;

	/*
	 * Is the page already in the swap cache? If so, then
	 * we can just drop our reference to it without doing
	 * any IO - it's already up-to-date on disk.
	 *
	 * Return 0, as we didn't actually free any real
	 * memory, and we should just continue our scan.
	 */
	if (PageSwapCache(page)) {
		entry.val = page->index;
		swap_duplicate(entry);
		set_pte(page_table, swp_entry_to_pte(entry));
drop_pte:
		vma->vm_mm->rss--;
		flush_tlb_page(vma, address);
		__free_page(page);
		goto out_failed;
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
		flush_cache_page(vma, address);
		pte_clear(page_table);
		goto drop_pte;
	}

	/*
	 * Don't go down into the swap-out stuff if
	 * we cannot do I/O! Avoid recursing on FS
	 * locks etc.
	 */
	if (!(gfp_mask & __GFP_IO))
		goto out_failed;

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
	 * dirty bit in 'page', and just drop the
	 * pte. All the hard work would be done by
	 * shrink_mmap().
	 *
	 * That would get rid of a lot of problems.
	 */
	flush_cache_page(vma, address);
	if (vma->vm_ops && (swapout = vma->vm_ops->swapout)) {
		int error;
		struct file *file = vma->vm_file;
		if (file) get_file(file);
		pte_clear(page_table);
		vma->vm_mm->rss--;
		flush_tlb_page(vma, address);
		vmlist_access_unlock(vma->vm_mm);
		error = swapout(page, file);
		if (file) fput(file);
		if (!error)
			goto out_free_success;
		__free_page(page);
		return error;
	}

	/*
	 * This is a dirty, swappable page.  First of all,
	 * get a suitable swap entry for it, and make sure
	 * we have the swap cache set up to associate the
	 * page with that swap entry.
	 */
	entry = acquire_swap_entry(page);
	if (!entry.val)
		goto out_failed; /* No swap space left */
		
	if (!(page = prepare_highmem_swapout(page)))
		goto out_swap_free;

	swap_duplicate(entry);	/* One for the process, one for the swap cache */

	/* This will also lock the page */
	add_to_swap_cache(page, entry);
	/* Put the swap entry into the pte after the page is in swapcache */
	vma->vm_mm->rss--;
	set_pte(page_table, swp_entry_to_pte(entry));
	flush_tlb_page(vma, address);
	vmlist_access_unlock(vma->vm_mm);

	/* OK, do a physical asynchronous write to swap.  */
	rw_swap_page(WRITE, page, 0);

out_free_success:
	__free_page(page);
	return 1;
out_swap_free:
	swap_free(entry);
out_failed:
	return 0;

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

static inline int swap_out_pmd(struct vm_area_struct * vma, pmd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		pmd_ERROR(*dir);
		pmd_clear(dir);
		return 0;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		int result;
		vma->vm_mm->swap_address = address + PAGE_SIZE;
		result = try_to_swap_out(vma, address, pte, gfp_mask);
		if (result)
			return result;
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	return 0;
}

static inline int swap_out_pgd(struct vm_area_struct * vma, pgd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return 0;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (pgd_end && (end > pgd_end))
		end = pgd_end;
	
	do {
		int result = swap_out_pmd(vma, pmd, address, end, gfp_mask);
		if (result)
			return result;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int swap_out_vma(struct vm_area_struct * vma, unsigned long address, int gfp_mask)
{
	pgd_t *pgdir;
	unsigned long end;

	/* Don't swap out areas which are locked down */
	if (vma->vm_flags & VM_LOCKED)
		return 0;

	pgdir = pgd_offset(vma->vm_mm, address);

	end = vma->vm_end;
	if (address >= end)
		BUG();
	do {
		int result = swap_out_pgd(vma, pgdir, address, end, gfp_mask);
		if (result)
			return result;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	} while (address && (address < end));
	return 0;
}

static int swap_out_mm(struct mm_struct * mm, int gfp_mask)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = mm->swap_address;

	/*
	 * Find the proper vm-area after freezing the vma chain 
	 * and ptes.
	 */
	vmlist_access_lock(mm);
	vma = find_vma(mm, address);
	if (vma) {
		if (address < vma->vm_start)
			address = vma->vm_start;

		for (;;) {
			int result = swap_out_vma(vma, address, gfp_mask);
			if (result)
				return result;
			vma = vma->vm_next;
			if (!vma)
				break;
			address = vma->vm_start;
		}
	}
	vmlist_access_unlock(mm);

	/* We didn't find anything for the process */
	mm->swap_cnt = 0;
	mm->swap_address = 0;
	return 0;
}

/*
 * Select the task with maximal swap_cnt and try to swap out a page.
 * N.B. This function returns only 0 or 1.  Return values != 1 from
 * the lower level routines result in continued processing.
 */
static int swap_out(unsigned int priority, int gfp_mask)
{
	struct task_struct * p;
	int counter;
	int __ret = 0;

	lock_kernel();
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
	counter = nr_threads / (priority+1);
	if (counter < 1)
		counter = 1;
	if (counter > nr_threads)
		counter = nr_threads;

	for (; counter >= 0; counter--) {
		int assign = 0;
		int max_cnt = 0;
		struct mm_struct *best = NULL;
		int pid = 0;
	select:
		read_lock(&tasklist_lock);
		p = init_task.next_task;
		for (; p != &init_task; p = p->next_task) {
			struct mm_struct *mm = p->mm;
			if (!p->swappable || !mm)
				continue;
	 		if (mm->rss <= 0)
				continue;
			/* Refresh swap_cnt? */
			if (assign)
				mm->swap_cnt = mm->rss;
			if (mm->swap_cnt > max_cnt) {
				max_cnt = mm->swap_cnt;
				best = mm;
				pid = p->pid;
			}
		}
		read_unlock(&tasklist_lock);
		if (!best) {
			if (!assign) {
				assign = 1;
				goto select;
			}
			goto out;
		} else {
			int ret;

			atomic_inc(&best->mm_count);
			ret = swap_out_mm(best, gfp_mask);
			mmdrop(best);

			if (!ret)
				continue;

			if (ret < 0)
				kill_proc(pid, SIGBUS, 1);
			__ret = 1;
			goto out;
		}
	}
out:
	unlock_kernel();
	return __ret;
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
static int do_try_to_free_pages(unsigned int gfp_mask, zone_t *zone)
{
	int priority;
	int count = SWAP_CLUSTER_MAX;

	/* Always trim SLAB caches when memory gets low. */
	kmem_cache_reap(gfp_mask);

	priority = 6;
	do {
		while (shrink_mmap(priority, gfp_mask, zone)) {
			if (!--count)
				goto done;
		}


		/* Try to get rid of some shared memory pages.. */
		if (gfp_mask & __GFP_IO) {
			/*
			 * don't be too light against the d/i cache since
		   	 * shrink_mmap() almost never fail when there's
		   	 * really plenty of memory free. 
			 */
			count -= shrink_dcache_memory(priority, gfp_mask, zone);
			count -= shrink_icache_memory(priority, gfp_mask, zone);
			if (count <= 0)
				goto done;
			while (shm_swap(priority, gfp_mask, zone)) {
				if (!--count)
					goto done;
			}
		}

		/* Then, try to page stuff out.. */
		while (swap_out(priority, gfp_mask)) {
			if (!--count)
				goto done;
		}
	} while (--priority >= 0);
done:

	return priority >= 0;
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
			/* kswapd is critical to provide GFP_ATOMIC
			   allocations (not GFP_HIGHMEM ones). */
			if (nr_free_pages() - nr_free_highpages() >= freepages.high)
				break;
			if (!do_try_to_free_pages(GFP_KSWAPD, 0))
				break;
			run_task_queue(&tq_disk);
		} while (!tsk->need_resched);
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
int try_to_free_pages(unsigned int gfp_mask, zone_t *zone)
{
	int retval = 1;

	wake_up_process(kswapd_process);
	if (gfp_mask & __GFP_WAIT) {
		current->flags |= PF_MEMALLOC;
		retval = do_try_to_free_pages(gfp_mask, zone);
		current->flags &= ~PF_MEMALLOC;
	}
	return retval;
}

static int __init kswapd_init(void)
{
	printk("Starting kswapd v1.6\n");
	swap_setup();
	kernel_thread(kswapd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	return 0;
}

module_init(kswapd_init)
