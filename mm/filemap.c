/*
 *	linux/mm/filemap.c
 *
 * Copyright (C) 1994, 1995  Linus Torvalds
 */

/*
 * This file handles the generic file mmap semantics used by
 * most "normal" filesystems (but you don't /have/ to use this:
 * the NFS filesystem does this differently, for example)
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 *
 * Shared mappings now work. 15.8.1995  Bruno.
 */

unsigned long page_cache_size = 0;
struct page * page_hash_table[PAGE_HASH_SIZE];

/*
 * Simple routines for both non-shared and shared mappings.
 */

/*
 * This is a special fast page-free routine that _only_ works
 * on page-cache pages that we are currently using. We can
 * just decrement the page count, because we know that the page
 * has a count > 1 (the page cache itself counts as one, and
 * we're currently using it counts as one). So we don't need
 * the full free_page() stuff..
 */
static inline void release_page(struct page * page)
{
	atomic_dec(&page->count);
}

/*
 * Invalidate the pages of an inode, removing all pages that aren't
 * locked down (those are sure to be up-to-date anyway, so we shouldn't
 * invalidate them).
 */
void invalidate_inode_pages(struct inode * inode)
{
	struct page ** p;
	struct page * page;

	p = &inode->i_pages;
	while ((page = *p) != NULL) {
		if (PageLocked(page)) {
			p = &page->next;
			continue;
		}
		inode->i_nrpages--;
		if ((*p = page->next) != NULL)
			(*p)->prev = page->prev;
		page->dirty = 0;
		page->next = NULL;
		page->prev = NULL;
		remove_page_from_hash_queue(page);
		page->inode = NULL;
		free_page(page_address(page));
		continue;
	}
}

/*
 * Truncate the page cache at a set offset, removing the pages
 * that are beyond that offset (and zeroing out partial pages).
 */
void truncate_inode_pages(struct inode * inode, unsigned long start)
{
	struct page ** p;
	struct page * page;

repeat:
	p = &inode->i_pages;
	while ((page = *p) != NULL) {
		unsigned long offset = page->offset;

		/* page wholly truncated - free it */
		if (offset >= start) {
			if (PageLocked(page)) {
				wait_on_page(page);
				goto repeat;
			}
			inode->i_nrpages--;
			if ((*p = page->next) != NULL)
				(*p)->prev = page->prev;
			page->dirty = 0;
			page->next = NULL;
			page->prev = NULL;
			remove_page_from_hash_queue(page);
			page->inode = NULL;
			free_page(page_address(page));
			continue;
		}
		p = &page->next;
		offset = start - offset;
		/* partial truncate, clear end of page */
		if (offset < PAGE_SIZE) {
			memset((void *) (offset + page_address(page)), 0, PAGE_SIZE - offset);
			flush_page_to_ram(page_address(page));
		}
	}
}

int shrink_mmap(int priority, int dma)
{
	static int clock = 0;
	struct page * page;
	unsigned long limit = MAP_NR(high_memory);
	struct buffer_head *tmp, *bh;
	int count_max, count_min;

	count_max = (limit<<1) >> (priority>>1);
	count_min = (limit<<1) >> (priority);

	page = mem_map + clock;
	do {
		count_max--;
		if (page->inode || page->buffers)
			count_min--;

		if (PageLocked(page))
			goto next;
		if (dma && !PageDMA(page))
			goto next;
		/* First of all, regenerate the page's referenced bit
                   from any buffers in the page */
		bh = page->buffers;
		if (bh) {
			tmp = bh;
			do {
				if (buffer_touched(tmp)) {
					clear_bit(BH_Touched, &tmp->b_state);
					set_bit(PG_referenced, &page->flags);
				}
				tmp = tmp->b_this_page;
			} while (tmp != bh);
		}

		/* We can't throw away shared pages, but we do mark
		   them as referenced.  This relies on the fact that
		   no page is currently in both the page cache and the
		   buffer cache; we'd have to modify the following
		   test to allow for that case. */

		switch (page->count) {
			case 1:
				/* If it has been referenced recently, don't free it */
				if (clear_bit(PG_referenced, &page->flags))
					break;

				/* is it a page cache page? */
				if (page->inode) {
					remove_page_from_hash_queue(page);
					remove_page_from_inode_queue(page);
					free_page(page_address(page));
					return 1;
				}

				/* is it a buffer cache page? */
				if (bh && try_to_free_buffer(bh, &bh, 6))
					return 1;
				break;

			default:
				/* more than one users: we can't throw it away */
				set_bit(PG_referenced, &page->flags);
				/* fall through */
			case 0:
				/* nothing */
		}
next:
		page++;
		clock++;
		if (clock >= limit) {
			clock = 0;
			page = mem_map;
		}
	} while (count_max > 0 && count_min > 0);
	return 0;
}

/*
 * This is called from try_to_swap_out() when we try to get rid of some
 * pages..  If we're unmapping the last occurrence of this page, we also
 * free it from the page hash-queues etc, as we don't want to keep it
 * in-core unnecessarily.
 */
unsigned long page_unuse(unsigned long page)
{
	struct page * p = mem_map + MAP_NR(page);
	int count = p->count;

	if (count != 2)
		return count;
	if (!p->inode)
		return count;
	remove_page_from_hash_queue(p);
	remove_page_from_inode_queue(p);
	free_page(page);
	return 1;
}

/*
 * Update a page cache copy, when we're doing a "write()" system call
 * See also "update_vm_cache()".
 */
void update_vm_cache(struct inode * inode, unsigned long pos, const char * buf, int count)
{
	unsigned long offset, len;

	offset = (pos & ~PAGE_MASK);
	pos = pos & PAGE_MASK;
	len = PAGE_SIZE - offset;
	do {
		struct page * page;

		if (len > count)
			len = count;
		page = find_page(inode, pos);
		if (page) {
			wait_on_page(page);
			memcpy((void *) (offset + page_address(page)), buf, len);
			release_page(page);
		}
		count -= len;
		buf += len;
		len = PAGE_SIZE;
		offset = 0;
		pos += PAGE_SIZE;
	} while (count);
}

static inline void add_to_page_cache(struct page * page,
	struct inode * inode, unsigned long offset)
{
	page->count++;
	page->flags &= ~((1 << PG_uptodate) | (1 << PG_error));
	page->offset = offset;
	add_page_to_inode_queue(inode, page);
	add_page_to_hash_queue(inode, page);
}

/*
 * Try to read ahead in the file. "page_cache" is a potentially free page
 * that we could use for the cache (if it is 0 we can try to create one,
 * this is all overlapped with the IO on the previous page finishing anyway)
 */
static unsigned long try_to_read_ahead(struct inode * inode, unsigned long offset, unsigned long page_cache)
{
	struct page * page;

	offset &= PAGE_MASK;
	if (!page_cache) {
		page_cache = __get_free_page(GFP_KERNEL);
		if (!page_cache)
			return 0;
	}
	if (offset >= inode->i_size)
		return page_cache;
#if 1
	page = find_page(inode, offset);
	if (page) {
		release_page(page);
		return page_cache;
	}
	/*
	 * Ok, add the new page to the hash-queues...
	 */
	page = mem_map + MAP_NR(page_cache);
	add_to_page_cache(page, inode, offset);
	inode->i_op->readpage(inode, page);
	free_page(page_cache);
	return 0;
#else
	return page_cache;
#endif
}

/* 
 * Wait for IO to complete on a locked page.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 */
void __wait_on_page(struct page *page)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&page->wait, &wait);
repeat:
	run_task_queue(&tq_disk);
	current->state = TASK_UNINTERRUPTIBLE;
	if (PageLocked(page)) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&page->wait, &wait);
	current->state = TASK_RUNNING;
}

#if 0
#define PROFILE_READAHEAD
#define DEBUG_READAHEAD
#endif

/*
 * Read-ahead profiling informations
 * ---------------------------------
 * Every PROFILE_MAXREADCOUNT, the following informations are written 
 * to the syslog:
 *   Percentage of asynchronous read-ahead.
 *   Average of read-ahead fields context value.
 * If DEBUG_READAHEAD is defined, a snapshot of these fields is written 
 * to the syslog.
 */

#ifdef PROFILE_READAHEAD

#define PROFILE_MAXREADCOUNT 1000

static unsigned long total_reada;
static unsigned long total_async;
static unsigned long total_ramax;
static unsigned long total_ralen;
static unsigned long total_rawin;

static void profile_readahead(int async, struct file *filp)
{
	unsigned long flags;

	++total_reada;
	if (async)
		++total_async;

	total_ramax	+= filp->f_ramax;
	total_ralen	+= filp->f_ralen;
	total_rawin	+= filp->f_rawin;

	if (total_reada > PROFILE_MAXREADCOUNT) {
		save_flags(flags);
		cli();
		if (!(total_reada > PROFILE_MAXREADCOUNT)) {
			restore_flags(flags);
			return;
		}

		printk("Readahead average:  max=%ld, len=%ld, win=%ld, async=%ld%%\n",
			total_ramax/total_reada,
			total_ralen/total_reada,
			total_rawin/total_reada,
			(total_async*100)/total_reada);
#ifdef DEBUG_READAHEAD
		printk("Readahead snapshot: max=%ld, len=%ld, win=%ld, raend=%ld\n",
			filp->f_ramax, filp->f_ralen, filp->f_rawin, filp->f_raend);
#endif

		total_reada	= 0;
		total_async	= 0;
		total_ramax	= 0;
		total_ralen	= 0;
		total_rawin	= 0;

		restore_flags(flags);
	}
}
#endif  /* defined PROFILE_READAHEAD */

/*
 * Read-ahead context:
 * -------------------
 * The read ahead context fields of the "struct file" are the following:
 * - f_raend : position of the first byte after the last page we tried to
 *             read ahead.
 * - f_ramax : current read-ahead maximum size.
 * - f_ralen : length of the current IO read block we tried to read-ahead.
 * - f_rawin : length of the current read-ahead window.
 *             if last read-ahead was synchronous then
 *                  f_rawin = f_ralen
 *             otherwise (was asynchronous)
 *                  f_rawin = previous value of f_ralen + f_ralen
 *
 * Read-ahead limits:
 * ------------------
 * MIN_READAHEAD   : minimum read-ahead size when read-ahead.
 * MAX_READAHEAD   : maximum read-ahead size when read-ahead.
 *
 * Synchronous read-ahead benefits:
 * --------------------------------
 * Using reasonable IO xfer length from peripheral devices increase system 
 * performances.
 * Reasonable means, in this context, not too large but not too small.
 * The actual maximum value is:
 *	MAX_READAHEAD + PAGE_SIZE = 76k is CONFIG_READA_SMALL is undefined
 *      and 32K if defined.
 *
 * Asynchronous read-ahead benefits:
 * ---------------------------------
 * Overlapping next read request and user process execution increase system 
 * performance.
 *
 * Read-ahead risks:
 * -----------------
 * We have to guess which further data are needed by the user process.
 * If these data are often not really needed, it's bad for system 
 * performances.
 * However, we know that files are often accessed sequentially by 
 * application programs and it seems that it is possible to have some good 
 * strategy in that guessing.
 * We only try to read-ahead files that seems to be read sequentially.
 *
 * Asynchronous read-ahead risks:
 * ------------------------------
 * In order to maximize overlapping, we must start some asynchronous read 
 * request from the device, as soon as possible.
 * We must be very careful about:
 * - The number of effective pending IO read requests.
 *   ONE seems to be the only reasonable value.
 * - The total memory pool usage for the file access stream.
 *   This maximum memory usage is implicitly 2 IO read chunks:
 *   2*(MAX_READAHEAD + PAGE_SIZE) = 156K if CONFIG_READA_SMALL is undefined,
 *   64k if defined.
 */

#if 0 /* small readahead */
#define MAX_READAHEAD (PAGE_SIZE*7)
#define MIN_READAHEAD (PAGE_SIZE*2)
#else
#define MAX_READAHEAD (PAGE_SIZE*18)
#define MIN_READAHEAD (PAGE_SIZE*3)
#endif

static inline unsigned long generic_file_readahead(int reada_ok, struct file * filp, struct inode * inode,
	unsigned long pos, struct page * page,
	unsigned long page_cache)
{
	unsigned long max_ahead, ahead;
	unsigned long raend, ppos;

	ppos = pos & PAGE_MASK;
	raend = filp->f_raend & PAGE_MASK;
	max_ahead = 0;

/*
 * The current page is locked.
 * If the current position is inside the previous read IO request, do not
 * try to reread previously read ahead pages.
 * Otherwise decide or not to read ahead some pages synchronously.
 * If we are not going to read ahead, set the read ahead context for this 
 * page only.
 */
	if (PageLocked(page)) {
		if (!filp->f_ralen || ppos >= raend || ppos + filp->f_ralen < raend) {
			raend = ppos;
			if (raend < inode->i_size)
				max_ahead = filp->f_ramax;
			filp->f_rawin = 0;
			filp->f_ralen = PAGE_SIZE;
			if (!max_ahead) {
				filp->f_raend  = ppos + filp->f_ralen;
				filp->f_rawin += filp->f_ralen;
			}
		}
	}
/*
 * The current page is not locked.
 * If we were reading ahead and,
 * if the current max read ahead size is not zero and,
 * if the current position is inside the last read-ahead IO request,
 *   it is the moment to try to read ahead asynchronously.
 * We will later force unplug device in order to force asynchronous read IO.
 */
	else if (reada_ok && filp->f_ramax && raend >= PAGE_SIZE &&
	         ppos <= raend && ppos + filp->f_ralen >= raend) {
/*
 * Add ONE page to max_ahead in order to try to have about the same IO max size
 * as synchronous read-ahead (MAX_READAHEAD + 1)*PAGE_SIZE.
 * Compute the position of the last page we have tried to read in order to 
 * begin to read ahead just at the next page.
 */
		raend -= PAGE_SIZE;
		if (raend < inode->i_size)
			max_ahead = filp->f_ramax + PAGE_SIZE;

		if (max_ahead) {
			filp->f_rawin = filp->f_ralen;
			filp->f_ralen = 0;
			reada_ok      = 2;
		}
	}
/*
 * Try to read ahead pages.
 * We hope that ll_rw_blk() plug/unplug, coalescence, requests sort and the
 * scheduler, will work enough for us to avoid too bad actuals IO requests.
 */
	ahead = 0;
	while (ahead < max_ahead) {
		ahead += PAGE_SIZE;
		page_cache = try_to_read_ahead(inode, raend + ahead, page_cache);
	}
/*
 * If we tried to read ahead some pages,
 * If we tried to read ahead asynchronously,
 *   Try to force unplug of the device in order to start an asynchronous
 *   read IO request.
 * Update the read-ahead context.
 * Store the length of the current read-ahead window.
 * Double the current max read ahead size.
 *   That heuristic avoid to do some large IO for files that are not really
 *   accessed sequentially.
 */
	if (ahead) {
		if (reada_ok == 2) {
			run_task_queue(&tq_disk);
		}

		filp->f_ralen += ahead;
		filp->f_rawin += filp->f_ralen;
		filp->f_raend = raend + ahead + PAGE_SIZE;

		filp->f_ramax += filp->f_ramax;

		if (filp->f_ramax > MAX_READAHEAD)
			filp->f_ramax = MAX_READAHEAD;

#ifdef PROFILE_READAHEAD
		profile_readahead((reada_ok == 2), filp);
#endif
	}

	return page_cache;
}


/*
 * This is a generic file read routine, and uses the
 * inode->i_op->readpage() function for the actual low-level
 * stuff.
 *
 * This is really ugly. But the goto's actually try to clarify some
 * of the logic when it comes to error handling etc.
 */

int generic_file_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int error, read;
	unsigned long pos, ppos, page_cache;
	int reada_ok;

	error = 0;
	read = 0;
	page_cache = 0;

	pos = filp->f_pos;
	ppos = pos & PAGE_MASK;
/*
 * If the current position is outside the previous read-ahead window, 
 * we reset the current read-ahead context and set read ahead max to zero
 * (will be set to just needed value later),
 * otherwise, we assume that the file accesses are sequential enough to
 * continue read-ahead.
 */
	if (ppos > filp->f_raend || ppos + filp->f_rawin < filp->f_raend) {
		reada_ok = 0;
		filp->f_raend = 0;
		filp->f_ralen = 0;
		filp->f_ramax = 0;
		filp->f_rawin = 0;
	} else {
		reada_ok = 1;
	}
/*
 * Adjust the current value of read-ahead max.
 * If the read operation stay in the first half page, force no readahead.
 * Otherwise try to increase read ahead max just enough to do the read request.
 * Then, at least MIN_READAHEAD if read ahead is ok,
 * and at most MAX_READAHEAD in all cases.
 */
	if (pos + count <= (PAGE_SIZE >> 1)) {
		filp->f_ramax = 0;
	} else {
		unsigned long needed;

		needed = ((pos + count) & PAGE_MASK) - (pos & PAGE_MASK);

		if (filp->f_ramax < needed)
			filp->f_ramax = needed;

		if (reada_ok && filp->f_ramax < MIN_READAHEAD)
				filp->f_ramax = MIN_READAHEAD;
		if (filp->f_ramax > MAX_READAHEAD)
			filp->f_ramax = MAX_READAHEAD;
	}

	for (;;) {
		struct page *page;

		if (pos >= inode->i_size)
			break;

		/*
		 * Try to find the data in the page cache..
		 */
		page = find_page(inode, pos & PAGE_MASK);
		if (!page)
			goto no_cached_page;

found_page:
/*
 * Try to read ahead only if the current page is filled or being filled.
 * Otherwise, if we were reading ahead, decrease max read ahead size to
 * the minimum value.
 * In this context, that seems to may happen only on some read error or if 
 * the page has been rewritten.
 */
		if (PageUptodate(page) || PageLocked(page))
			page_cache = generic_file_readahead(reada_ok, filp, inode, pos, page, page_cache);
		else if (reada_ok && filp->f_ramax > MIN_READAHEAD)
				filp->f_ramax = MIN_READAHEAD;

		wait_on_page(page);

		if (!PageUptodate(page))
			goto page_read_error;

success:
		/*
		 * Ok, we have the page, it's up-to-date and ok,
		 * so now we can finally copy it to user space...
		 */
	{
		unsigned long offset, nr;
		offset = pos & ~PAGE_MASK;
		nr = PAGE_SIZE - offset;
		if (nr > count)
			nr = count;

		if (nr > inode->i_size - pos)
			nr = inode->i_size - pos;
		memcpy_tofs(buf, (void *) (page_address(page) + offset), nr);
		release_page(page);
		buf += nr;
		pos += nr;
		read += nr;
		count -= nr;
		if (count)
			continue;
		break;
	}

no_cached_page:
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 */
		if (!page_cache) {
			page_cache = __get_free_page(GFP_KERNEL);
			/*
			 * That could have slept, so go around to the
			 * very beginning..
			 */
			if (page_cache)
				continue;
			error = -ENOMEM;
			break;
		}

		/*
		 * Ok, add the new page to the hash-queues...
		 */
		page = mem_map + MAP_NR(page_cache);
		page_cache = 0;
		add_to_page_cache(page, inode, pos & PAGE_MASK);

		/*
		 * Error handling is tricky. If we get a read error,
		 * the cached page stays in the cache (but uptodate=0),
		 * and the next process that accesses it will try to
		 * re-read it. This is needed for NFS etc, where the
		 * identity of the reader can decide if we can read the
		 * page or not..
		 */
/*
 * We have to read the page.
 * If we were reading ahead, we had previously tried to read this page,
 * That means that the page has probably been removed from the cache before 
 * the application process needs it, or has been rewritten.
 * Decrease max readahead size to the minimum value in that situation.
 */
		if (reada_ok && filp->f_ramax > MIN_READAHEAD)
			filp->f_ramax = MIN_READAHEAD;

		error = inode->i_op->readpage(inode, page);
		if (!error)
			goto found_page;
		release_page(page);
		break;

page_read_error:
		/*
		 * We found the page, but it wasn't up-to-date.
		 * Try to re-read it _once_. We do this synchronously,
		 * because this happens only if there were errors.
		 */
		error = inode->i_op->readpage(inode, page);
		if (!error) {
			wait_on_page(page);
			if (PageUptodate(page) && !PageError(page))
				goto success;
			error = -EIO; /* Some unspecified error occurred.. */
		}
		release_page(page);
		break;
	}

	filp->f_pos = pos;
	filp->f_reada = 1;
	if (page_cache)
		free_page(page_cache);
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	if (!read)
		read = error;
	return read;
}

/*
 * Semantics for shared and private memory areas are different past the end
 * of the file. A shared mapping past the last page of the file is an error
 * and results in a SIGBUS, while a private mapping just maps in a zero page.
 *
 * The goto's are kind of ugly, but this streamlines the normal case of having
 * it in the page cache, and handles the special cases reasonably without
 * having a lot of duplicated code.
 */
static unsigned long filemap_nopage(struct vm_area_struct * area, unsigned long address, int no_share)
{
	unsigned long offset;
	struct page * page;
	struct inode * inode = area->vm_inode;
	unsigned long old_page, new_page;

	new_page = 0;
	offset = (address & PAGE_MASK) - area->vm_start + area->vm_offset;
	if (offset >= inode->i_size && (area->vm_flags & VM_SHARED) && area->vm_mm == current->mm)
		goto no_page;

	/*
	 * Do we have something in the page cache already?
	 */
	page = find_page(inode, offset);
	if (!page)
		goto no_cached_page;

found_page:
	/*
	 * Ok, found a page in the page cache, now we need to check
	 * that it's up-to-date
	 */
	wait_on_page(page);
	if (!PageUptodate(page))
		goto page_read_error;

success:
	/*
	 * Found the page, need to check sharing and possibly
	 * copy it over to another page..
	 */
	old_page = page_address(page);
	if (!no_share) {
		/*
		 * Ok, we can share the cached page directly.. Get rid
		 * of any potential extra pages.
		 */
		if (new_page)
			free_page(new_page);

		flush_page_to_ram(old_page);
		return old_page;
	}

	/*
	 * Check that we have another page to copy it over to..
	 */
	if (!new_page) {
		new_page = __get_free_page(GFP_KERNEL);
		if (!new_page)
			goto failure;
	}
	memcpy((void *) new_page, (void *) old_page, PAGE_SIZE);
	flush_page_to_ram(new_page);
	release_page(page);
	return new_page;

no_cached_page:
	new_page = __get_free_page(GFP_KERNEL);
	if (!new_page)
		goto no_page;

	/*
	 * During getting the above page we might have slept,
	 * so we need to re-check the situation with the page
	 * cache.. The page we just got may be useful if we
	 * can't share, so don't get rid of it here.
	 */
	page = find_page(inode, offset);
	if (page)
		goto found_page;

	/*
	 * Now, create a new page-cache page from the page we got
	 */
	page = mem_map + MAP_NR(new_page);
	new_page = 0;
	add_to_page_cache(page, inode, offset);

	if (inode->i_op->readpage(inode, page) != 0)
		goto failure;

	/*
	 * Do a very limited read-ahead if appropriate
	 */
	if (PageLocked(page))
		new_page = try_to_read_ahead(inode, offset + PAGE_SIZE, 0);
	goto found_page;

page_read_error:
	/*
	 * Umm, take care of errors if the page isn't up-to-date.
	 * Try to re-read it _once_. We do this synchronously,
	 * because there really aren't any performance issues here
	 * and we need to check for errors.
	 */
	if (inode->i_op->readpage(inode, page) != 0)
		goto failure;
	wait_on_page(page);
	if (PageError(page))
		goto failure;
	if (PageUptodate(page))
		goto success;

	/*
	 * Uhhuh.. Things didn't work out. Return zero to tell the
	 * mm layer so, possibly freeing the page cache page first.
	 */
failure:
	release_page(page);
no_page:
	return 0;
}

/*
 * Tries to write a shared mapped page to its backing store. May return -EIO
 * if the disk is full.
 */
static inline int do_write_page(struct inode * inode, struct file * file,
	const char * page, unsigned long offset)
{
	int old_fs, retval;
	unsigned long size;

	size = offset + PAGE_SIZE;
	/* refuse to extend file size.. */
	if (S_ISREG(inode->i_mode)) {
		if (size > inode->i_size)
			size = inode->i_size;
		/* Ho humm.. We should have tested for this earlier */
		if (size < offset)
			return -EIO;
	}
	size -= offset;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	retval = -EIO;
	if (size == file->f_op->write(inode, file, (const char *) page, size))
		retval = 0;
	set_fs(old_fs);
	return retval;
}

static int filemap_write_page(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long page)
{
	int result;
	struct file file;
	struct inode * inode;
	struct buffer_head * bh;

	bh = mem_map[MAP_NR(page)].buffers;
	if (bh) {
		/* whee.. just mark the buffer heads dirty */
		struct buffer_head * tmp = bh;
		do {
			mark_buffer_dirty(tmp, 0);
			tmp = tmp->b_this_page;
		} while (tmp != bh);
		return 0;
	}

	inode = vma->vm_inode;
	file.f_op = inode->i_op->default_file_ops;
	if (!file.f_op->write)
		return -EIO;
	file.f_mode = 3;
	file.f_flags = 0;
	file.f_count = 1;
	file.f_inode = inode;
	file.f_pos = offset;
	file.f_reada = 0;

	down(&inode->i_sem);
	result = do_write_page(inode, &file, (const char *) page, offset);
	up(&inode->i_sem);
	return result;
}


/*
 * Swapping to a shared file: while we're busy writing out the page
 * (and the page still exists in memory), we save the page information
 * in the page table, so that "filemap_swapin()" can re-use the page
 * immediately if it is called while we're busy swapping it out..
 *
 * Once we've written it all out, we mark the page entry "empty", which
 * will result in a normal page-in (instead of a swap-in) from the now
 * up-to-date disk file.
 */
int filemap_swapout(struct vm_area_struct * vma,
	unsigned long offset,
	pte_t *page_table)
{
	int error;
	unsigned long page = pte_page(*page_table);
	unsigned long entry = SWP_ENTRY(SHM_SWP_TYPE, MAP_NR(page));

	flush_cache_page(vma, (offset + vma->vm_start - vma->vm_offset));
	set_pte(page_table, __pte(entry));
	flush_tlb_page(vma, (offset + vma->vm_start - vma->vm_offset));
	error = filemap_write_page(vma, offset, page);
	if (pte_val(*page_table) == entry)
		pte_clear(page_table);
	return error;
}

/*
 * filemap_swapin() is called only if we have something in the page
 * tables that is non-zero (but not present), which we know to be the
 * page index of a page that is busy being swapped out (see above).
 * So we just use it directly..
 */
static pte_t filemap_swapin(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long entry)
{
	unsigned long page = SWP_OFFSET(entry);

	mem_map[page].count++;
	page = (page << PAGE_SHIFT) + PAGE_OFFSET;
	return mk_pte(page,vma->vm_page_prot);
}


static inline int filemap_sync_pte(pte_t * ptep, struct vm_area_struct *vma,
	unsigned long address, unsigned int flags)
{
	pte_t pte = *ptep;
	unsigned long page;
	int error;

	if (!(flags & MS_INVALIDATE)) {
		if (!pte_present(pte))
			return 0;
		if (!pte_dirty(pte))
			return 0;
		flush_page_to_ram(pte_page(pte));
		flush_cache_page(vma, address);
		set_pte(ptep, pte_mkclean(pte));
		flush_tlb_page(vma, address);
		page = pte_page(pte);
		mem_map[MAP_NR(page)].count++;
	} else {
		if (pte_none(pte))
			return 0;
		flush_cache_page(vma, address);
		pte_clear(ptep);
		flush_tlb_page(vma, address);
		if (!pte_present(pte)) {
			swap_free(pte_val(pte));
			return 0;
		}
		page = pte_page(pte);
		if (!pte_dirty(pte) || flags == MS_INVALIDATE) {
			free_page(page);
			return 0;
		}
	}
	error = filemap_write_page(vma, address - vma->vm_start + vma->vm_offset, page);
	free_page(page);
	return error;
}

static inline int filemap_sync_pte_range(pmd_t * pmd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned long offset, unsigned int flags)
{
	pte_t * pte;
	unsigned long end;
	int error;

	if (pmd_none(*pmd))
		return 0;
	if (pmd_bad(*pmd)) {
		printk("filemap_sync_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return 0;
	}
	pte = pte_offset(pmd, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	error = 0;
	do {
		error |= filemap_sync_pte(pte, vma, address + offset, flags);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return error;
}

static inline int filemap_sync_pmd_range(pgd_t * pgd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned int flags)
{
	pmd_t * pmd;
	unsigned long offset, end;
	int error;

	if (pgd_none(*pgd))
		return 0;
	if (pgd_bad(*pgd)) {
		printk("filemap_sync_pmd_range: bad pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		return 0;
	}
	pmd = pmd_offset(pgd, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	error = 0;
	do {
		error |= filemap_sync_pte_range(pmd, address, end - address, vma, offset, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return error;
}

static int filemap_sync(struct vm_area_struct * vma, unsigned long address,
	size_t size, unsigned int flags)
{
	pgd_t * dir;
	unsigned long end = address + size;
	int error = 0;

	dir = pgd_offset(vma->vm_mm, address);
	flush_cache_range(vma->vm_mm, end - size, end);
	while (address < end) {
		error |= filemap_sync_pmd_range(dir, address, end - address, vma, flags);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range(vma->vm_mm, end - size, end);
	return error;
}

/*
 * This handles (potentially partial) area unmaps..
 */
static void filemap_unmap(struct vm_area_struct *vma, unsigned long start, size_t len)
{
	filemap_sync(vma, start, len, MS_ASYNC);
}

/*
 * Shared mappings need to be able to do the right thing at
 * close/unmap/sync. They will also use the private file as
 * backing-store for swapping..
 */
static struct vm_operations_struct file_shared_mmap = {
	NULL,			/* no special open */
	NULL,			/* no special close */
	filemap_unmap,		/* unmap - we need to sync the pages */
	NULL,			/* no special protect */
	filemap_sync,		/* sync */
	NULL,			/* advise */
	filemap_nopage,		/* nopage */
	NULL,			/* wppage */
	filemap_swapout,	/* swapout */
	filemap_swapin,		/* swapin */
};

/*
 * Private mappings just need to be able to load in the map.
 *
 * (This is actually used for shared mappings as well, if we
 * know they can't ever get write permissions..)
 */
static struct vm_operations_struct file_private_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	filemap_nopage,		/* nopage */
	NULL,			/* wppage */
	NULL,			/* swapout */
	NULL,			/* swapin */
};

/* This is used for a general mmap of a disk file */
int generic_file_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;

	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE)) {
		ops = &file_shared_mmap;
		/* share_page() can only guarantee proper page sharing if
		 * the offsets are all page aligned. */
		if (vma->vm_offset & (PAGE_SIZE - 1))
			return -EINVAL;
	} else {
		ops = &file_private_mmap;
		if (vma->vm_offset & (inode->i_sb->s_blocksize - 1))
			return -EINVAL;
	}
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!inode->i_op || !inode->i_op->readpage)
		return -ENOEXEC;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	vma->vm_inode = inode;
	inode->i_count++;
	vma->vm_ops = ops;
	return 0;
}


/*
 * The msync() system call.
 */

static int msync_interval(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int flags)
{
	if (!vma->vm_inode)
		return 0;
	if (vma->vm_ops->sync) {
		int error;
		error = vma->vm_ops->sync(vma, start, end-start, flags);
		if (error)
			return error;
		if (flags & MS_SYNC)
			return file_fsync(vma->vm_inode, NULL);
		return 0;
	}
	return 0;
}

asmlinkage int sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error, error;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
		return -EINVAL;
	if (end == start)
		return 0;
	/*
	 * If the interval [start,end) covers some unmapped address ranges,
	 * just ignore them, but return -EFAULT at the end.
	 */
	vma = find_vma(current, start);
	unmapped_error = 0;
	for (;;) {
		/* Still start < end. */
		if (!vma)
			return -EFAULT;
		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -EFAULT;
			start = vma->vm_start;
		}
		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = msync_interval(vma, start, end, flags);
				if (error)
					return error;
			}
			return unmapped_error;
		}
		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = msync_interval(vma, start, vma->vm_end, flags);
		if (error)
			return error;
		start = vma->vm_end;
		vma = vma->vm_next;
	}
}
