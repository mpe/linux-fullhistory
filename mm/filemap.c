/*
 *	linux/mm/filemap.c
 *
 * Copyright (C) 1994, 1995  Linus Torvalds
 */

/*
 * This file handles the generic file mmap semantics used by
 * most "normal" filesystems (but you don't /have/ to use this:
 * the NFS filesystem used to do this differently, for example)
 */
#include <linux/malloc.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/locks.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/swapctl.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

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

#define release_page(page) __free_page((page))

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
		page->next = NULL;
		page->prev = NULL;
		remove_page_from_hash_queue(page);
		page->inode = NULL;
		__free_page(page);
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
			page->next = NULL;
			page->prev = NULL;
			remove_page_from_hash_queue(page);
			page->inode = NULL;
			__free_page(page);
			continue;
		}
		p = &page->next;
		offset = start - offset;
		/* partial truncate, clear end of page */
		if (offset < PAGE_SIZE) {
			unsigned long address = page_address(page);
			memset((void *) (offset + address), 0, PAGE_SIZE - offset);
			flush_page_to_ram(address);
		}
	}
}

/*
 * Remove a page from the page cache and free it.
 */
void remove_inode_page(struct page *page)
{
	remove_page_from_hash_queue(page);
	remove_page_from_inode_queue(page);
	__free_page(page);
}

int shrink_mmap(int priority, int gfp_mask)
{
	static unsigned long clock = 0;
	unsigned long limit = num_physpages;
	struct page * page;
	int count;

	count = limit >> priority;

	page = mem_map + clock;
	do {
		int referenced;

		/* This works even in the presence of PageSkip because
		 * the first two entries at the beginning of a hole will
		 * be marked, not just the first.
		 */
		page++;
		clock++;
		if (clock >= max_mapnr) {
			clock = 0;
			page = mem_map;
		}
		if (PageSkip(page)) {
			/* next_hash is overloaded for PageSkip */
			page = page->next_hash;
			clock = page - mem_map;
		}
		
		referenced = test_and_clear_bit(PG_referenced, &page->flags);

		if (PageLocked(page))
			continue;

		if ((gfp_mask & __GFP_DMA) && !PageDMA(page))
			continue;

		/* We can't free pages unless there's just one user */
		if (atomic_read(&page->count) != 1)
			continue;

		count--;

		/*
		 * Is it a page swap page? If so, we want to
		 * drop it if it is no longer used, even if it
		 * were to be marked referenced..
		 */
		if (PageSwapCache(page)) {
			if (referenced && swap_count(page->offset) != 1)
				continue;
			delete_from_swap_cache(page);
			return 1;
		}	

		if (referenced)
			continue;

		/* Is it a buffer page? */
		if (page->buffers) {
			if (buffer_under_min())
				continue;
			if (!try_to_free_buffers(page))
				continue;
			return 1;
		}

		/* is it a page-cache page? */
		if (page->inode) {
			if (pgcache_under_min())
				continue;
			remove_inode_page(page);
			return 1;
		}

	} while (count > 0);
	return 0;
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
	struct inode * inode, unsigned long offset,
	struct page **hash)
{
	atomic_inc(&page->count);
	page->flags = (page->flags & ~((1 << PG_uptodate) | (1 << PG_error))) | (1 << PG_referenced);
	page->offset = offset;
	add_page_to_inode_queue(inode, page);
	__add_page_to_hash_queue(page, hash);
}

/*
 * Try to read ahead in the file. "page_cache" is a potentially free page
 * that we could use for the cache (if it is 0 we can try to create one,
 * this is all overlapped with the IO on the previous page finishing anyway)
 */
static unsigned long try_to_read_ahead(struct file * file,
				unsigned long offset, unsigned long page_cache)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct page * page;
	struct page ** hash;

	offset &= PAGE_MASK;
	switch (page_cache) {
	case 0:
		page_cache = __get_free_page(GFP_USER);
		if (!page_cache)
			break;
	default:
		if (offset >= inode->i_size)
			break;
		hash = page_hash(inode, offset);
		page = __find_page(inode, offset, *hash);
		if (!page) {
			/*
			 * Ok, add the new page to the hash-queues...
			 */
			page = mem_map + MAP_NR(page_cache);
			add_to_page_cache(page, inode, offset, hash);
			inode->i_op->readpage(file, page);
			page_cache = 0;
		}
		release_page(page);
	}
	return page_cache;
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
	struct task_struct *tsk = current;
	struct wait_queue wait;

	wait.task = tsk;
	add_wait_queue(&page->wait, &wait);
repeat:
	tsk->state = TASK_UNINTERRUPTIBLE;
	run_task_queue(&tq_disk);
	if (PageLocked(page)) {
		schedule();
		goto repeat;
	}
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&page->wait, &wait);
}

#if 0
#define PROFILE_READAHEAD
#define DEBUG_READAHEAD
#endif

/*
 * Read-ahead profiling information
 * --------------------------------
 * Every PROFILE_MAXREADCOUNT, the following information is written 
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
 *      and 32K if defined (4K page size assumed).
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
 *   64k if defined (4K page size assumed).
 */

static inline int get_max_readahead(struct inode * inode)
{
	if (!inode->i_dev || !max_readahead[MAJOR(inode->i_dev)])
		return MAX_READAHEAD;
	return max_readahead[MAJOR(inode->i_dev)][MINOR(inode->i_dev)];
}

static inline unsigned long generic_file_readahead(int reada_ok,
	struct file * filp, struct inode * inode,
	unsigned long ppos, struct page * page, unsigned long page_cache)
{
	unsigned long max_ahead, ahead;
	unsigned long raend;
	int max_readahead = get_max_readahead(inode);

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
		page_cache = try_to_read_ahead(filp, raend + ahead,
						page_cache);
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

		if (filp->f_ramax > max_readahead)
			filp->f_ramax = max_readahead;

#ifdef PROFILE_READAHEAD
		profile_readahead((reada_ok == 2), filp);
#endif
	}

	return page_cache;
}

/*
 * "descriptor" for what we're up to with a read.
 * This allows us to use the same read code yet
 * have multiple different users of the data that
 * we read from a file.
 *
 * The simplest case just copies the data to user
 * mode.
 */
typedef struct {
	size_t written;
	size_t count;
	char * buf;
	int error;
} read_descriptor_t;

typedef int (*read_actor_t)(read_descriptor_t *, const char *, unsigned long);

/*
 * This is a generic file read routine, and uses the
 * inode->i_op->readpage() function for the actual low-level
 * stuff.
 *
 * This is really ugly. But the goto's actually try to clarify some
 * of the logic when it comes to error handling etc.
 */
static void do_generic_file_read(struct file * filp, loff_t *ppos, read_descriptor_t * desc, read_actor_t actor)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	size_t pos, pgpos, page_cache;
	int reada_ok;
	int max_readahead = get_max_readahead(inode);

	page_cache = 0;

	pos = *ppos;
	pgpos = pos & PAGE_MASK;
/*
 * If the current position is outside the previous read-ahead window, 
 * we reset the current read-ahead context and set read ahead max to zero
 * (will be set to just needed value later),
 * otherwise, we assume that the file accesses are sequential enough to
 * continue read-ahead.
 */
	if (pgpos > filp->f_raend || pgpos + filp->f_rawin < filp->f_raend) {
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
	if (pos + desc->count <= (PAGE_SIZE >> 1)) {
		filp->f_ramax = 0;
	} else {
		unsigned long needed;

		needed = ((pos + desc->count) & PAGE_MASK) - pgpos;

		if (filp->f_ramax < needed)
			filp->f_ramax = needed;

		if (reada_ok && filp->f_ramax < MIN_READAHEAD)
				filp->f_ramax = MIN_READAHEAD;
		if (filp->f_ramax > max_readahead)
			filp->f_ramax = max_readahead;
	}

	for (;;) {
		struct page *page, **hash;

		if (pos >= inode->i_size)
			break;

		/*
		 * Try to find the data in the page cache..
		 */
		hash = page_hash(inode, pos & PAGE_MASK);
		page = __find_page(inode, pos & PAGE_MASK, *hash);
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
			page_cache = generic_file_readahead(reada_ok, filp, inode, pos & PAGE_MASK, page, page_cache);
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
		if (nr > inode->i_size - pos)
			nr = inode->i_size - pos;

		/*
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		nr = actor(desc, (const char *) (page_address(page) + offset), nr);
		pos += nr;
		release_page(page);
		if (nr && desc->count)
			continue;
		break;
	}

no_cached_page:
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 */
		if (!page_cache) {
			page_cache = __get_free_page(GFP_USER);
			/*
			 * That could have slept, so go around to the
			 * very beginning..
			 */
			if (page_cache)
				continue;
			desc->error = -ENOMEM;
			break;
		}

		/*
		 * Ok, add the new page to the hash-queues...
		 */
		page = mem_map + MAP_NR(page_cache);
		page_cache = 0;
		add_to_page_cache(page, inode, pos & PAGE_MASK, hash);

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

		{
			int error = inode->i_op->readpage(filp, page);
			if (!error)
				goto found_page;
			desc->error = error;
			release_page(page);
			break;
		}

page_read_error:
		/*
		 * We found the page, but it wasn't up-to-date.
		 * Try to re-read it _once_. We do this synchronously,
		 * because this happens only if there were errors.
		 */
		{
			int error = inode->i_op->readpage(filp, page);
			if (!error) {
				wait_on_page(page);
				if (PageUptodate(page) && !PageError(page))
					goto success;
				error = -EIO; /* Some unspecified error occurred.. */
			}
			desc->error = error;
			release_page(page);
			break;
		}
	}

	*ppos = pos;
	filp->f_reada = 1;
	if (page_cache)
		free_page(page_cache);
	UPDATE_ATIME(inode);
}

static int file_read_actor(read_descriptor_t * desc, const char *area, unsigned long size)
{
	unsigned long left;
	unsigned long count = desc->count;

	if (size > count)
		size = count;
	left = __copy_to_user(desc->buf, area, size);
	if (left) {
		size -= left;
		desc->error = -EFAULT;
	}
	desc->count = count - size;
	desc->written += size;
	desc->buf += size;
	return size;
}

/*
 * This is the "read()" routine for all filesystems
 * that can use the page cache directly.
 */
ssize_t generic_file_read(struct file * filp, char * buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = -EFAULT;
	if (access_ok(VERIFY_WRITE, buf, count)) {
		retval = 0;
		if (count) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.count = count;
			desc.buf = buf;
			desc.error = 0;
			do_generic_file_read(filp, ppos, &desc, file_read_actor);

			retval = desc.written;
			if (!retval)
				retval = desc.error;
		}
	}
	return retval;
}

static int file_send_actor(read_descriptor_t * desc, const char *area, unsigned long size)
{
	ssize_t written;
	unsigned long count = desc->count;
	struct file *file = (struct file *) desc->buf;
	struct inode *inode = file->f_dentry->d_inode;
	mm_segment_t old_fs;

	if (size > count)
		size = count;
	down(&inode->i_sem);
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	written = file->f_op->write(file, area, size, &file->f_pos);
	set_fs(old_fs);
	up(&inode->i_sem);
	if (written < 0) {
		desc->error = written;
		written = 0;
	}
	desc->count = count - written;
	desc->written += written;
	return written;
}

asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	ssize_t retval;
	struct file * in_file, * out_file;
	struct inode * in_inode, * out_inode;

	lock_kernel();

	/*
	 * Get input file, and verify that it is ok..
	 */
	retval = -EBADF;
	in_file = fget(in_fd);
	if (!in_file)
		goto out;
	if (!(in_file->f_mode & FMODE_READ))
		goto fput_in;
	retval = -EINVAL;
	in_inode = in_file->f_dentry->d_inode;
	if (!in_inode)
		goto fput_in;
	if (!in_inode->i_op || !in_inode->i_op->readpage)
		goto fput_in;
	retval = locks_verify_area(FLOCK_VERIFY_READ, in_inode, in_file, in_file->f_pos, count);
	if (retval)
		goto fput_in;

	/*
	 * Get output file, and verify that it is ok..
	 */
	retval = -EBADF;
	out_file = fget(out_fd);
	if (!out_file)
		goto fput_in;
	if (!(out_file->f_mode & FMODE_WRITE))
		goto fput_out;
	retval = -EINVAL;
	if (!out_file->f_op || !out_file->f_op->write)
		goto fput_out;
	out_inode = out_file->f_dentry->d_inode;
	if (!out_inode)
		goto fput_out;
	retval = locks_verify_area(FLOCK_VERIFY_WRITE, out_inode, out_file, out_file->f_pos, count);
	if (retval)
		goto fput_out;

	retval = 0;
	if (count) {
		read_descriptor_t desc;
		loff_t pos = 0, *ppos;

		retval = -EFAULT;
		ppos = &in_file->f_pos;
		if (offset) {
			if (get_user(pos, offset))
				goto fput_out;
			ppos = &pos;
		}

		desc.written = 0;
		desc.count = count;
		desc.buf = (char *) out_file;
		desc.error = 0;
		do_generic_file_read(in_file, ppos, &desc, file_send_actor);

		retval = desc.written;
		if (!retval)
			retval = desc.error;
		if (offset)
			put_user(pos, offset);
	}


fput_out:
	fput(out_file);
fput_in:
	fput(in_file);
out:
	unlock_kernel();
	return retval;
}

/*
 * Semantics for shared and private memory areas are different past the end
 * of the file. A shared mapping past the last page of the file is an error
 * and results in a SIGBUS, while a private mapping just maps in a zero page.
 *
 * The goto's are kind of ugly, but this streamlines the normal case of having
 * it in the page cache, and handles the special cases reasonably without
 * having a lot of duplicated code.
 *
 * WSH 06/04/97: fixed a memory leak and moved the allocation of new_page
 * ahead of the wait if we're sure to need it.
 */
static unsigned long filemap_nopage(struct vm_area_struct * area, unsigned long address, int no_share)
{
	struct file * file = area->vm_file;
	struct dentry * dentry = file->f_dentry;
	struct inode * inode = dentry->d_inode;
	unsigned long offset, reada, i;
	struct page * page, **hash;
	unsigned long old_page, new_page;

	new_page = 0;
	offset = (address & PAGE_MASK) - area->vm_start + area->vm_offset;
	if (offset >= inode->i_size && (area->vm_flags & VM_SHARED) && area->vm_mm == current->mm)
		goto no_page;

	/*
	 * Do we have something in the page cache already?
	 */
	hash = page_hash(inode, offset);
	page = __find_page(inode, offset, *hash);
	if (!page)
		goto no_cached_page;

found_page:
	/*
	 * Ok, found a page in the page cache, now we need to check
	 * that it's up-to-date.  First check whether we'll need an
	 * extra page -- better to overlap the allocation with the I/O.
	 */
	if (no_share && !new_page) {
		new_page = __get_free_page(GFP_USER);
		if (!new_page)
			goto failure;
	}

	if (PageLocked(page))
		goto page_locked_wait;
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
	 * No sharing ... copy to the new page.
	 */
	copy_page(new_page, old_page);
	flush_page_to_ram(new_page);
	release_page(page);
	return new_page;

no_cached_page:
	/*
	 * Try to read in an entire cluster at once.
	 */
	reada   = offset;
	reada >>= PAGE_SHIFT + page_cluster;
	reada <<= PAGE_SHIFT + page_cluster;

	for (i = 1 << page_cluster; i > 0; --i, reada += PAGE_SIZE)
		new_page = try_to_read_ahead(file, reada, new_page);

	if (!new_page)
		new_page = __get_free_page(GFP_USER);
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
	add_to_page_cache(page, inode, offset, hash);

	if (inode->i_op->readpage(file, page) != 0)
		goto failure;

	goto found_page;

page_locked_wait:
	__wait_on_page(page);
	if (PageUptodate(page))
		goto success;
	
page_read_error:
	/*
	 * Umm, take care of errors if the page isn't up-to-date.
	 * Try to re-read it _once_. We do this synchronously,
	 * because there really aren't any performance issues here
	 * and we need to check for errors.
	 */
	if (inode->i_op->readpage(file, page) != 0)
		goto failure;
	wait_on_page(page);
	if (PageError(page))
		goto failure;
	if (PageUptodate(page))
		goto success;

	/*
	 * Things didn't work out. Return zero to tell the
	 * mm layer so, possibly freeing the page cache page first.
	 */
failure:
	release_page(page);
	if (new_page)
		free_page(new_page);
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
	int retval;
	unsigned long size;
	loff_t loff = offset;
	mm_segment_t old_fs;

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
	if (size == file->f_op->write(file, (const char *) page, size, &loff))
		retval = 0;
	set_fs(old_fs);
	return retval;
}

static int filemap_write_page(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long page)
{
	int result;
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;

	file = vma->vm_file;
	dentry = file->f_dentry;
	inode = dentry->d_inode;
	if (!file->f_op->write)
		return -EIO;

	/*
	 * If a task terminates while we're swapping the page, the vma and
	 * and file could be released ... increment the count to be safe.
	 */
	file->f_count++;
	down(&inode->i_sem);
	result = do_write_page(inode, file, (const char *) page, offset);
	up(&inode->i_sem);
	fput(file);
	return result;
}


/*
 * The page cache takes care of races between somebody
 * trying to swap something out and swap something in
 * at the same time..
 */
int filemap_swapout(struct vm_area_struct * vma, struct page * page)
{
	return filemap_write_page(vma, page->offset, page_address(page));
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
		atomic_inc(&mem_map[MAP_NR(page)].count);
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
	NULL,			/* swapin */
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

int generic_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;
	struct inode *inode = file->f_dentry->d_inode;

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
	UPDATE_ATIME(inode);
	vma->vm_file = file;
	file->f_count++;
	vma->vm_ops = ops;
	return 0;
}


/*
 * The msync() system call.
 */

static int msync_interval(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int flags)
{
	if (vma->vm_file && vma->vm_ops && vma->vm_ops->sync) {
		int error;
		error = vma->vm_ops->sync(vma, start, end-start, flags);
		if (!error && (flags & MS_SYNC)) {
			struct file * file = vma->vm_file;
			if (file) {
				struct dentry * dentry = file->f_dentry;
				struct inode * inode = dentry->d_inode;
				down(&inode->i_sem);
				error = file_fsync(file, dentry);
				up(&inode->i_sem);
			}
		}
		return error;
	}
	return 0;
}

asmlinkage int sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error, error = -EINVAL;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (start & ~PAGE_MASK)
		goto out;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;
	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
		goto out;
	error = 0;
	if (end == start)
		goto out;
	/*
	 * If the interval [start,end) covers some unmapped address ranges,
	 * just ignore them, but return -EFAULT at the end.
	 */
	vma = find_vma(current->mm, start);
	unmapped_error = 0;
	for (;;) {
		/* Still start < end. */
		error = -EFAULT;
		if (!vma)
			goto out;
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
					goto out;
			}
			error = unmapped_error;
			goto out;
		}
		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = msync_interval(vma, start, vma->vm_end, flags);
		if (error)
			goto out;
		start = vma->vm_end;
		vma = vma->vm_next;
	}
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return error;
}

/*
 * Write to a file through the page cache. This is mainly for the
 * benefit of NFS and possibly other network-based file systems.
 *
 * We currently put everything into the page cache prior to writing it.
 * This is not a problem when writing full pages. With partial pages,
 * however, we first have to read the data into the cache, then
 * dirty the page, and finally schedule it for writing. Alternatively, we
 * could write-through just the portion of data that would go into that
 * page, but that would kill performance for applications that write data
 * line by line, and it's prone to race conditions.
 *
 * Note that this routine doesn't try to keep track of dirty pages. Each
 * file system has to do this all by itself, unfortunately.
 *							okir@monad.swb.de
 */
ssize_t
generic_file_write(struct file *file, const char *buf,
		   size_t count, loff_t *ppos)
{
	struct dentry	*dentry = file->f_dentry; 
	struct inode	*inode = dentry->d_inode; 
	unsigned long	pos = *ppos;
	unsigned long	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	struct page	*page, **hash;
	unsigned long	page_cache = 0;
	unsigned long	written;
	long		status, sync;

	if (!inode->i_op || !inode->i_op->updatepage)
		return -EIO;

	sync    = file->f_flags & O_SYNC;
	written = 0;

	if (file->f_flags & O_APPEND)
		pos = inode->i_size;

	/*
	 * Check whether we've reached the file size limit.
	 */
	status = -EFBIG;
	if (pos >= limit) {
		send_sig(SIGXFSZ, current, 0);
		goto out;
	}

	status  = 0;
	/*
	 * Check whether to truncate the write,
	 * and send the signal if we do.
	 */
	if (count > limit - pos) {
		send_sig(SIGXFSZ, current, 0);
		count = limit - pos;
	}

	while (count) {
		unsigned long bytes, pgpos, offset;
		/*
		 * Try to find the page in the cache. If it isn't there,
		 * allocate a free page.
		 */
		offset = (pos & ~PAGE_MASK);
		pgpos = pos & PAGE_MASK;
		bytes = PAGE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		hash = page_hash(inode, pgpos);
		page = __find_page(inode, pgpos, *hash);
		if (!page) {
			if (!page_cache) {
				page_cache = __get_free_page(GFP_USER);
				if (page_cache)
					continue;
				status = -ENOMEM;
				break;
			}
			page = mem_map + MAP_NR(page_cache);
			add_to_page_cache(page, inode, pgpos, hash);
			page_cache = 0;
		}

		/* Get exclusive IO access to the page.. */
		wait_on_page(page);
		set_bit(PG_locked, &page->flags);

		/*
		 * Do the real work.. If the writer ends up delaying the write,
		 * the writer needs to increment the page use counts until he
		 * is done with the page.
		 */
		bytes -= copy_from_user((u8*)page_address(page) + offset, buf, bytes);
		status = -EFAULT;
		if (bytes)
			status = inode->i_op->updatepage(file, page, offset, bytes, sync);

		/* Mark it unlocked again and drop the page.. */
		clear_bit(PG_locked, &page->flags);
		wake_up(&page->wait);
		__free_page(page);

		if (status < 0)
			break;

		written += status;
		count -= status;
		pos += status;
		buf += status;
	}
	*ppos = pos;
	if (pos > inode->i_size)
		inode->i_size = pos;

	if (page_cache)
		free_page(page_cache);
out:
	return written ? written : status;
}

/*
 * Support routines for directory cacheing using the page cache.
 */

/*
 * Finds the page at the specified offset, installing a new page
 * if requested.  The count is incremented and the page is locked.
 *
 * Note: we don't have to worry about races here, as the caller
 * is holding the inode semaphore.
 */
unsigned long get_cached_page(struct inode * inode, unsigned long offset,
				int new)
{
	struct page * page;
	struct page ** hash;
	unsigned long page_cache = 0;

	hash = page_hash(inode, offset);
	page = __find_page(inode, offset, *hash);
	if (!page) {
		if (!new)
			goto out;
		page_cache = get_free_page(GFP_USER);
		if (!page_cache)
			goto out;
		page = mem_map + MAP_NR(page_cache);
		add_to_page_cache(page, inode, offset, hash);
	}
	if (atomic_read(&page->count) != 2)
		printk(KERN_ERR "get_cached_page: page count=%d\n",
			atomic_read(&page->count));
	if (test_bit(PG_locked, &page->flags))
		printk(KERN_ERR "get_cached_page: page already locked!\n");
	set_bit(PG_locked, &page->flags);
	page_cache = page_address(page);

out:
	return page_cache;
}

/*
 * Unlock and free a page.
 */
void put_cached_page(unsigned long addr)
{
	struct page * page = mem_map + MAP_NR(addr);

	if (!test_bit(PG_locked, &page->flags))
		printk("put_cached_page: page not locked!\n");
	if (atomic_read(&page->count) != 2)
		printk("put_cached_page: page count=%d\n", 
			atomic_read(&page->count));
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	__free_page(page);
}
