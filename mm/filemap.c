/*
 *	linux/mm/filemap.c
 *
 * Copyright (C) 1994-1999  Linus Torvalds
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/mman.h>

#include <linux/highmem.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 *
 * Shared mappings now work. 15.8.1995  Bruno.
 *
 * finished 'unifying' the page and buffer cache and SMP-threaded the
 * page-cache, 21.05.1999, Ingo Molnar <mingo@redhat.com>
 *
 * SMP-threaded pagemap-LRU 1999, Andrea Arcangeli <andrea@suse.de>
 */

atomic_t page_cache_size = ATOMIC_INIT(0);
unsigned int page_hash_bits;
struct page **page_hash_table;
struct list_head lru_cache;

static spinlock_t pagecache_lock = SPIN_LOCK_UNLOCKED;
/*
 * NOTE: to avoid deadlocking you must never acquire the pagecache_lock with
 *       the pagemap_lru_lock held.
 */
spinlock_t pagemap_lru_lock = SPIN_LOCK_UNLOCKED;

#define CLUSTER_PAGES		(1 << page_cluster)
#define CLUSTER_OFFSET(x)	(((x) >> page_cluster) << page_cluster)

void __add_page_to_hash_queue(struct page * page, struct page **p)
{
	atomic_inc(&page_cache_size);
	if((page->next_hash = *p) != NULL)
		(*p)->pprev_hash = &page->next_hash;
	*p = page;
	page->pprev_hash = p;
	if (page->buffers)
		PAGE_BUG(page);
}

static inline void remove_page_from_hash_queue(struct page * page)
{
	if(page->pprev_hash) {
		if(page->next_hash)
			page->next_hash->pprev_hash = page->pprev_hash;
		*page->pprev_hash = page->next_hash;
		page->pprev_hash = NULL;
	}
	atomic_dec(&page_cache_size);
}

static inline int sync_page(struct page *page)
{
	struct address_space *mapping = page->mapping;

	if (mapping && mapping->a_ops && mapping->a_ops->sync_page)
		return mapping->a_ops->sync_page(page);
	return 0;
}

/*
 * Remove a page from the page cache and free it. Caller has to make
 * sure the page is locked and that nobody else uses it - or that usage
 * is safe.
 */
static inline void __remove_inode_page(struct page *page)
{
	remove_page_from_inode_queue(page);
	remove_page_from_hash_queue(page);
	page->mapping = NULL;
}

void remove_inode_page(struct page *page)
{
	if (!PageLocked(page))
		PAGE_BUG(page);

	spin_lock(&pagecache_lock);
	__remove_inode_page(page);
	spin_unlock(&pagecache_lock);
}

/**
 * invalidate_inode_pages - Invalidate all the unlocked pages of one inode
 * @inode: the inode which pages we want to invalidate
 *
 * This function only removes the unlocked pages, if you want to
 * remove all the pages of one inode, you must call truncate_inode_pages.
 */

void invalidate_inode_pages(struct inode * inode)
{
	struct list_head *head, *curr;
	struct page * page;

	head = &inode->i_mapping->pages;

	spin_lock(&pagecache_lock);
	spin_lock(&pagemap_lru_lock);
	curr = head->next;

	while (curr != head) {
		page = list_entry(curr, struct page, list);
		curr = curr->next;

		/* We cannot invalidate a locked page */
		if (TryLockPage(page))
			continue;

		__lru_cache_del(page);
		__remove_inode_page(page);
		UnlockPage(page);
		page_cache_release(page);
	}

	spin_unlock(&pagemap_lru_lock);
	spin_unlock(&pagecache_lock);
}

/*
 * Truncate the page cache at a set offset, removing the pages
 * that are beyond that offset (and zeroing out partial pages).
 */
void truncate_inode_pages(struct address_space * mapping, loff_t lstart)
{
	struct list_head *head, *curr;
	struct page * page;
	unsigned partial = lstart & (PAGE_CACHE_SIZE - 1);
	unsigned long start;

	start = (lstart + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

repeat:
	head = &mapping->pages;
	spin_lock(&pagecache_lock);
	curr = head->next;
	while (curr != head) {
		unsigned long offset;

		page = list_entry(curr, struct page, list);
		curr = curr->next;

		offset = page->index;

		/* page wholly truncated - free it */
		if (offset >= start) {
			if (TryLockPage(page)) {
				page_cache_get(page);
				spin_unlock(&pagecache_lock);
				wait_on_page(page);
				page_cache_release(page);
				goto repeat;
			}
			page_cache_get(page);
			spin_unlock(&pagecache_lock);

			if (!page->buffers || block_flushpage(page, 0))
				lru_cache_del(page);

			/*
			 * We remove the page from the page cache
			 * _after_ we have destroyed all buffer-cache
			 * references to it. Otherwise some other process
			 * might think this inode page is not in the
			 * page cache and creates a buffer-cache alias
			 * to it causing all sorts of fun problems ...
			 */
			remove_inode_page(page);
			ClearPageDirty(page);

			UnlockPage(page);
			page_cache_release(page);
			page_cache_release(page);

			/*
			 * We have done things without the pagecache lock,
			 * so we'll have to repeat the scan.
			 * It's not possible to deadlock here because
			 * we are guaranteed to make progress. (ie. we have
			 * just removed a page)
			 */
			goto repeat;
		}
		/*
		 * there is only one partial page possible.
		 */
		if (!partial)
			continue;

		/* and it's the one preceeding the first wholly truncated page */
		if ((offset + 1) != start)
			continue;

		/* partial truncate, clear end of page */
		if (TryLockPage(page)) {
			spin_unlock(&pagecache_lock);
			goto repeat;
		}
		page_cache_get(page);
		spin_unlock(&pagecache_lock);

		memclear_highpage_flush(page, partial, PAGE_CACHE_SIZE-partial);
		if (page->buffers)
			block_flushpage(page, partial);

		partial = 0;

		/*
		 * we have dropped the spinlock so we have to
		 * restart.
		 */
		UnlockPage(page);
		page_cache_release(page);
		goto repeat;
	}
	spin_unlock(&pagecache_lock);
}

/*
 * nr_dirty represents the number of dirty pages that we will write async
 * before doing sync writes.  We can only do sync writes if we can
 * wait for IO (__GFP_IO set).
 */
int shrink_mmap(int priority, int gfp_mask)
{
	int ret = 0, count, nr_dirty;
	struct list_head * page_lru;
	struct page * page = NULL;
	
	count = nr_lru_pages / (priority + 1);
	nr_dirty = priority;

	/* we need pagemap_lru_lock for list_del() ... subtle code below */
	spin_lock(&pagemap_lru_lock);
	while (count > 0 && (page_lru = lru_cache.prev) != &lru_cache) {
		page = list_entry(page_lru, struct page, lru);
		list_del(page_lru);

		if (PageTestandClearReferenced(page))
			goto dispose_continue;

		count--;
		/*
		 * Avoid unscalable SMP locking for pages we can
		 * immediate tell are untouchable..
		 */
		if (!page->buffers && page_count(page) > 1)
			goto dispose_continue;

		if (TryLockPage(page))
			goto dispose_continue;

		/* Release the pagemap_lru lock even if the page is not yet
		   queued in any lru queue since we have just locked down
		   the page so nobody else may SMP race with us running
		   a lru_cache_del() (lru_cache_del() always run with the
		   page locked down ;). */
		spin_unlock(&pagemap_lru_lock);

		/* avoid freeing the page while it's locked */
		page_cache_get(page);

		/*
		 * Is it a buffer page? Try to clean it up regardless
		 * of zone - it's old.
		 */
		if (page->buffers) {
			int wait;
			/*
			 * 0 - free it if can do so without IO
			 * 1 - start write-out of dirty buffers
			 * 2 - wait for locked buffers
			 */
			wait = (gfp_mask & __GFP_IO) ? (nr_dirty-- < 0) ? 2 : 1 : 0;
			if (!try_to_free_buffers(page, wait))
				goto unlock_continue;
			/* page was locked, inode can't go away under us */
			if (!page->mapping) {
				atomic_dec(&buffermem_pages);
				goto made_buffer_progress;
			}
		}

		/* Take the pagecache_lock spinlock held to avoid
		   other tasks to notice the page while we are looking at its
		   page count. If it's a pagecache-page we'll free it
		   in one atomic transaction after checking its page count. */
		spin_lock(&pagecache_lock);

		/*
		 * We can't free pages unless there's just one user
		 * (count == 2 because we added one ourselves above).
		 */
		if (page_count(page) != 2)
			goto cache_unlock_continue;

		/*
		 * Is it a page swap page? If so, we want to
		 * drop it if it is no longer used, even if it
		 * were to be marked referenced..
		 */
		if (PageSwapCache(page)) {
			spin_unlock(&pagecache_lock);
			__delete_from_swap_cache(page);
			goto made_inode_progress;
		}	

		/*
		 * Page is from a zone we don't care about.
		 * Don't drop page cache entries in vain.
		 */
		if (page->zone->free_pages > page->zone->pages_high)
			goto cache_unlock_continue;

		/* is it a page-cache page? */
		if (page->mapping) {
			if (!PageDirty(page) && !pgcache_under_min()) {
				__remove_inode_page(page);
				spin_unlock(&pagecache_lock);
				goto made_inode_progress;
			}
			goto cache_unlock_continue;
		}

		printk(KERN_ERR "shrink_mmap: unknown LRU page!\n");

cache_unlock_continue:
		spin_unlock(&pagecache_lock);
unlock_continue:
		spin_lock(&pagemap_lru_lock);
		UnlockPage(page);
		page_cache_release(page);
dispose_continue:
		list_add(page_lru, &lru_cache);
	}
	goto out;

made_inode_progress:
	page_cache_release(page);
made_buffer_progress:
	UnlockPage(page);
	page_cache_release(page);
	ret = 1;
	spin_lock(&pagemap_lru_lock);
	/* nr_lru_pages needs the spinlock */
	nr_lru_pages--;

out:
	spin_unlock(&pagemap_lru_lock);

	return ret;
}

static inline struct page * __find_page_nolock(struct address_space *mapping, unsigned long offset, struct page *page)
{
	goto inside;

	for (;;) {
		page = page->next_hash;
inside:
		if (!page)
			goto not_found;
		if (page->mapping != mapping)
			continue;
		if (page->index == offset)
			break;
	}
	SetPageReferenced(page);
not_found:
	return page;
}

/*
 * By the time this is called, the page is locked and
 * we don't have to worry about any races any more.
 *
 * Start the IO..
 */
static int writeout_one_page(struct page *page)
{
	struct buffer_head *bh, *head = page->buffers;

	bh = head;
	do {
		if (buffer_locked(bh) || !buffer_dirty(bh) || !buffer_uptodate(bh))
			continue;

		bh->b_flushtime = 0;
		ll_rw_block(WRITE, 1, &bh);	
	} while ((bh = bh->b_this_page) != head);
	return 0;
}

static int waitfor_one_page(struct page *page)
{
	int error = 0;
	struct buffer_head *bh, *head = page->buffers;

	bh = head;
	do {
		wait_on_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			error = -EIO;
	} while ((bh = bh->b_this_page) != head);
	return error;
}

static int do_buffer_fdatasync(struct inode *inode, unsigned long start, unsigned long end, int (*fn)(struct page *))
{
	struct list_head *head, *curr;
	struct page *page;
	int retval = 0;

	head = &inode->i_mapping->pages;

	spin_lock(&pagecache_lock);
	curr = head->next;
	while (curr != head) {
		page = list_entry(curr, struct page, list);
		curr = curr->next;
		if (!page->buffers)
			continue;
		if (page->index >= end)
			continue;
		if (page->index < start)
			continue;

		page_cache_get(page);
		spin_unlock(&pagecache_lock);
		lock_page(page);

		/* The buffers could have been free'd while we waited for the page lock */
		if (page->buffers)
			retval |= fn(page);

		UnlockPage(page);
		spin_lock(&pagecache_lock);
		curr = page->list.next;
		page_cache_release(page);
	}
	spin_unlock(&pagecache_lock);

	return retval;
}

/*
 * Two-stage data sync: first start the IO, then go back and
 * collect the information..
 */
int generic_buffer_fdatasync(struct inode *inode, unsigned long start_idx, unsigned long end_idx)
{
	int retval;

	retval = do_buffer_fdatasync(inode, start_idx, end_idx, writeout_one_page);
	retval |= do_buffer_fdatasync(inode, start_idx, end_idx, waitfor_one_page);
	return retval;
}

/*
 * Add a page to the inode page cache.
 *
 * The caller must have locked the page and 
 * set all the page flags correctly..
 */
void add_to_page_cache_locked(struct page * page, struct address_space *mapping, unsigned long index)
{
	if (!PageLocked(page))
		BUG();

	page_cache_get(page);
	spin_lock(&pagecache_lock);
	page->index = index;
	add_page_to_inode_queue(mapping, page);
	__add_page_to_hash_queue(page, page_hash(mapping, index));
	lru_cache_add(page);
	spin_unlock(&pagecache_lock);
}

/*
 * This adds a page to the page cache, starting out as locked,
 * owned by us, but unreferenced, not uptodate and with no errors.
 */
static inline void __add_to_page_cache(struct page * page,
	struct address_space *mapping, unsigned long offset,
	struct page **hash)
{
	struct page *alias;
	unsigned long flags;

	if (PageLocked(page))
		BUG();

	flags = page->flags & ~((1 << PG_uptodate) | (1 << PG_error) | (1 << PG_dirty) | (1 << PG_referenced));
	page->flags = flags | (1 << PG_locked);
	page_cache_get(page);
	page->index = offset;
	add_page_to_inode_queue(mapping, page);
	__add_page_to_hash_queue(page, hash);
	lru_cache_add(page);
	alias = __find_page_nolock(mapping, offset, *hash);
	if (alias != page)
		BUG();
}

void add_to_page_cache(struct page * page, struct address_space * mapping, unsigned long offset)
{
	spin_lock(&pagecache_lock);
	__add_to_page_cache(page, mapping, offset, page_hash(mapping, offset));
	spin_unlock(&pagecache_lock);
}

static int add_to_page_cache_unique(struct page * page,
	struct address_space *mapping, unsigned long offset,
	struct page **hash)
{
	int err;
	struct page *alias;

	spin_lock(&pagecache_lock);
	alias = __find_page_nolock(mapping, offset, *hash);

	err = 1;
	if (!alias) {
		__add_to_page_cache(page,mapping,offset,hash);
		err = 0;
	}

	spin_unlock(&pagecache_lock);
	return err;
}

/*
 * This adds the requested page to the page cache if it isn't already there,
 * and schedules an I/O to read in its contents from disk.
 */
static inline int page_cache_read(struct file * file, unsigned long offset) 
{
	struct inode *inode = file->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	struct page **hash = page_hash(mapping, offset);
	struct page *page; 

	spin_lock(&pagecache_lock);
	page = __find_page_nolock(mapping, offset, *hash); 
	spin_unlock(&pagecache_lock);
	if (page)
		return 0;

	page = page_cache_alloc();
	if (!page)
		return -ENOMEM;

	if (!add_to_page_cache_unique(page, mapping, offset, hash)) {
		int error = mapping->a_ops->readpage(file, page);
		page_cache_release(page);
		return error;
	}
	/*
	 * We arrive here in the unlikely event that someone 
	 * raced with us and added our page to the cache first.
	 */
	page_cache_free(page);
	return 0;
}

/*
 * Read in an entire cluster at once.  A cluster is usually a 64k-
 * aligned block that includes the page requested in "offset."
 */
static int read_cluster_nonblocking(struct file * file, unsigned long offset,
	unsigned long filesize)
{
	unsigned long pages = CLUSTER_PAGES;

	offset = CLUSTER_OFFSET(offset);
	while ((pages-- > 0) && (offset < filesize)) {
		int error = page_cache_read(file, offset);
		if (error < 0)
			return error;
		offset ++;
	}

	return 0;
}

/* 
 * Wait for a page to get unlocked.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 */
void ___wait_on_page(struct page *page)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	add_wait_queue(&page->wait, &wait);
	do {
		sync_page(page);
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!PageLocked(page))
			break;
		schedule();
	} while (PageLocked(page));
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&page->wait, &wait);
}

/*
 * Get an exclusive lock on the page..
 */
void lock_page(struct page *page)
{
	while (TryLockPage(page))
		___wait_on_page(page);
}


/*
 * a rather lightweight function, finding and getting a reference to a
 * hashed page atomically, waiting for it if it's locked.
 */
struct page * __find_get_page (struct address_space *mapping,
				unsigned long offset, struct page **hash)
{
	struct page *page;

	/*
	 * We scan the hash list read-only. Addition to and removal from
	 * the hash-list needs a held write-lock.
	 */
repeat:
	spin_lock(&pagecache_lock);
	page = __find_page_nolock(mapping, offset, *hash);
	if (page)
		page_cache_get(page);
	spin_unlock(&pagecache_lock);

	/* Found the page, sleep if locked. */
	if (page && PageLocked(page)) {
		struct task_struct *tsk = current;
		DECLARE_WAITQUEUE(wait, tsk);

		sync_page(page);

		__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		add_wait_queue(&page->wait, &wait);

		if (PageLocked(page))
			schedule();
		__set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(&page->wait, &wait);

		/*
		 * The page might have been unhashed meanwhile. It's
		 * not freed though because we hold a reference to it.
		 * If this is the case then it will be freed _here_,
		 * and we recheck the hash anyway.
		 */
		page_cache_release(page);
		goto repeat;
	}
	/*
	 * It's not locked so we can return the page and we hold
	 * a reference to it.
	 */
	return page;
}

/*
 * Get the lock to a page atomically.
 */
struct page * __find_lock_page (struct address_space *mapping,
				unsigned long offset, struct page **hash)
{
	struct page *page;

	/*
	 * We scan the hash list read-only. Addition to and removal from
	 * the hash-list needs a held write-lock.
	 */
repeat:
	spin_lock(&pagecache_lock);
	page = __find_page_nolock(mapping, offset, *hash);
	if (page)
		page_cache_get(page);
	spin_unlock(&pagecache_lock);

	/* Found the page, sleep if locked. */
	if (page && TryLockPage(page)) {
		struct task_struct *tsk = current;
		DECLARE_WAITQUEUE(wait, tsk);

		sync_page(page);

		__set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		add_wait_queue(&page->wait, &wait);

		if (PageLocked(page))
			schedule();
		__set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(&page->wait, &wait);

		/*
		 * The page might have been unhashed meanwhile. It's
		 * not freed though because we hold a reference to it.
		 * If this is the case then it will be freed _here_,
		 * and we recheck the hash anyway.
		 */
		page_cache_release(page);
		goto repeat;
	}
	/*
	 * It's not locked so we can return the page and we hold
	 * a reference to it.
	 */
	return page;
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
		printk("Readahead snapshot: max=%ld, len=%ld, win=%ld, raend=%Ld\n",
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
 *	       read ahead.
 * - f_ramax : current read-ahead maximum size.
 * - f_ralen : length of the current IO read block we tried to read-ahead.
 * - f_rawin : length of the current read-ahead window.
 *		if last read-ahead was synchronous then
 *			f_rawin = f_ralen
 *		otherwise (was asynchronous)
 *			f_rawin = previous value of f_ralen + f_ralen
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
 *	MAX_READAHEAD + PAGE_CACHE_SIZE = 76k is CONFIG_READA_SMALL is undefined
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
 *   2*(MAX_READAHEAD + PAGE_CACHE_SIZE) = 156K if CONFIG_READA_SMALL is undefined,
 *   64k if defined (4K page size assumed).
 */

static inline int get_max_readahead(struct inode * inode)
{
	if (!inode->i_dev || !max_readahead[MAJOR(inode->i_dev)])
		return MAX_READAHEAD;
	return max_readahead[MAJOR(inode->i_dev)][MINOR(inode->i_dev)];
}

static void generic_file_readahead(int reada_ok,
	struct file * filp, struct inode * inode,
	struct page * page)
{
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	unsigned long index = page->index;
	unsigned long max_ahead, ahead;
	unsigned long raend;
	int max_readahead = get_max_readahead(inode);

	raend = filp->f_raend;
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
		if (!filp->f_ralen || index >= raend || index + filp->f_ralen < raend) {
			raend = index;
			if (raend < end_index)
				max_ahead = filp->f_ramax;
			filp->f_rawin = 0;
			filp->f_ralen = 1;
			if (!max_ahead) {
				filp->f_raend  = index + filp->f_ralen;
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
	else if (reada_ok && filp->f_ramax && raend >= 1 &&
		 index <= raend && index + filp->f_ralen >= raend) {
/*
 * Add ONE page to max_ahead in order to try to have about the same IO max size
 * as synchronous read-ahead (MAX_READAHEAD + 1)*PAGE_CACHE_SIZE.
 * Compute the position of the last page we have tried to read in order to 
 * begin to read ahead just at the next page.
 */
		raend -= 1;
		if (raend < end_index)
			max_ahead = filp->f_ramax + 1;

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
		ahead ++;
		if ((raend + ahead) >= end_index)
			break;
		if (page_cache_read(filp, raend + ahead) < 0)
			break;
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
		filp->f_raend = raend + ahead + 1;

		filp->f_ramax += filp->f_ramax;

		if (filp->f_ramax > max_readahead)
			filp->f_ramax = max_readahead;

#ifdef PROFILE_READAHEAD
		profile_readahead((reada_ok == 2), filp);
#endif
	}

	return;
}


/*
 * This is a generic file read routine, and uses the
 * inode->i_op->readpage() function for the actual low-level
 * stuff.
 *
 * This is really ugly. But the goto's actually try to clarify some
 * of the logic when it comes to error handling etc.
 */
void do_generic_file_read(struct file * filp, loff_t *ppos, read_descriptor_t * desc, read_actor_t actor)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	unsigned long index, offset;
	struct page *cached_page;
	int reada_ok;
	int error;
	int max_readahead = get_max_readahead(inode);

	cached_page = NULL;
	index = *ppos >> PAGE_CACHE_SHIFT;
	offset = *ppos & ~PAGE_CACHE_MASK;

/*
 * If the current position is outside the previous read-ahead window, 
 * we reset the current read-ahead context and set read ahead max to zero
 * (will be set to just needed value later),
 * otherwise, we assume that the file accesses are sequential enough to
 * continue read-ahead.
 */
	if (index > filp->f_raend || index + filp->f_rawin < filp->f_raend) {
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
	if (!index && offset + desc->count <= (PAGE_CACHE_SIZE >> 1)) {
		filp->f_ramax = 0;
	} else {
		unsigned long needed;

		needed = ((offset + desc->count) >> PAGE_CACHE_SHIFT) + 1;

		if (filp->f_ramax < needed)
			filp->f_ramax = needed;

		if (reada_ok && filp->f_ramax < MIN_READAHEAD)
				filp->f_ramax = MIN_READAHEAD;
		if (filp->f_ramax > max_readahead)
			filp->f_ramax = max_readahead;
	}

	for (;;) {
		struct page *page, **hash;
		unsigned long end_index, nr;

		end_index = inode->i_size >> PAGE_CACHE_SHIFT;
		if (index > end_index)
			break;
		nr = PAGE_CACHE_SIZE;
		if (index == end_index) {
			nr = inode->i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset)
				break;
		}

		nr = nr - offset;

		/*
		 * Try to find the data in the page cache..
		 */
		hash = page_hash(mapping, index);

		spin_lock(&pagecache_lock);
		page = __find_page_nolock(mapping, index, *hash);
		if (!page)
			goto no_cached_page;
found_page:
		page_cache_get(page);
		spin_unlock(&pagecache_lock);

		if (!Page_Uptodate(page))
			goto page_not_up_to_date;
page_ok:
		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		nr = actor(desc, page, offset, nr);
		offset += nr;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;
	
		page_cache_release(page);
		if (nr && desc->count)
			continue;
		break;

/*
 * Ok, the page was not immediately readable, so let's try to read ahead while we're at it..
 */
page_not_up_to_date:
		generic_file_readahead(reada_ok, filp, inode, page);

		if (Page_Uptodate(page))
			goto page_ok;

		/* Get exclusive access to the page ... */
		lock_page(page);
		if (Page_Uptodate(page)) {
			UnlockPage(page);
			goto page_ok;
		}

readpage:
		/* ... and start the actual read. The read will unlock the page. */
		error = mapping->a_ops->readpage(filp, page);

		if (!error) {
			if (Page_Uptodate(page))
				goto page_ok;

			/* Again, try some read-ahead while waiting for the page to finish.. */
			generic_file_readahead(reada_ok, filp, inode, page);
			wait_on_page(page);
			if (Page_Uptodate(page))
				goto page_ok;
			error = -EIO;
		}

		/* UHHUH! A synchronous read error occurred. Report it */
		desc->error = error;
		page_cache_release(page);
		break;

no_cached_page:
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 *
		 * We get here with the page cache lock held.
		 */
		if (!cached_page) {
			spin_unlock(&pagecache_lock);
			cached_page = page_cache_alloc();
			if (!cached_page) {
				desc->error = -ENOMEM;
				break;
			}

			/*
			 * Somebody may have added the page while we
			 * dropped the page cache lock. Check for that.
			 */
			spin_lock(&pagecache_lock);
			page = __find_page_nolock(mapping, index, *hash);
			if (page)
				goto found_page;
		}

		/*
		 * Ok, add the new page to the hash-queues...
		 */
		page = cached_page;
		__add_to_page_cache(page, mapping, index, hash);
		spin_unlock(&pagecache_lock);
		cached_page = NULL;

		goto readpage;
	}

	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	filp->f_reada = 1;
	if (cached_page)
		page_cache_free(cached_page);
	UPDATE_ATIME(inode);
}

static int file_read_actor(read_descriptor_t * desc, struct page *page, unsigned long offset, unsigned long size)
{
	unsigned long kaddr;
	unsigned long left, count = desc->count;

	if (size > count)
		size = count;

	kaddr = kmap(page);
	left = __copy_to_user(desc->buf, (void *)(kaddr + offset), size);
	kunmap(page);
	
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

static int file_send_actor(read_descriptor_t * desc, struct page *page, unsigned long offset , unsigned long size)
{
	unsigned long kaddr;
	ssize_t written;
	unsigned long count = desc->count;
	struct file *file = (struct file *) desc->buf;
	mm_segment_t old_fs;

	if (size > count)
		size = count;
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	kaddr = kmap(page);
	written = file->f_op->write(file, (char *)kaddr + offset,
						 size, &file->f_pos);
	kunmap(page);
	set_fs(old_fs);
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
	if (!in_inode->i_mapping->a_ops->readpage)
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
	return retval;
}

/*
 * Read-ahead and flush behind for MADV_SEQUENTIAL areas.  Since we are
 * sure this is sequential access, we don't need a flexible read-ahead
 * window size -- we can always use a large fixed size window.
 */
static void nopage_sequential_readahead(struct vm_area_struct * vma,
	unsigned long pgoff, unsigned long filesize)
{
	unsigned long ra_window;

	ra_window = get_max_readahead(vma->vm_file->f_dentry->d_inode);
	ra_window = CLUSTER_OFFSET(ra_window + CLUSTER_PAGES - 1);

	/* vm_raend is zero if we haven't read ahead in this area yet.  */
	if (vma->vm_raend == 0)
		vma->vm_raend = vma->vm_pgoff + ra_window;

	/*
	 * If we've just faulted the page half-way through our window,
	 * then schedule reads for the next window, and release the
	 * pages in the previous window.
	 */
	if ((pgoff + (ra_window >> 1)) == vma->vm_raend) {
		unsigned long start = vma->vm_pgoff + vma->vm_raend;
		unsigned long end = start + ra_window;

		if (end > ((vma->vm_end >> PAGE_SHIFT) + vma->vm_pgoff))
			end = (vma->vm_end >> PAGE_SHIFT) + vma->vm_pgoff;
		if (start > end)
			return;

		while ((start < end) && (start < filesize)) {
			if (read_cluster_nonblocking(vma->vm_file,
							start, filesize) < 0)
				break;
			start += CLUSTER_PAGES;
		}
		run_task_queue(&tq_disk);

		/* if we're far enough past the beginning of this area,
		   recycle pages that are in the previous window. */
		if (vma->vm_raend > (vma->vm_pgoff + ra_window + ra_window)) {
			unsigned long window = ra_window << PAGE_SHIFT;

			end = vma->vm_start + (vma->vm_raend << PAGE_SHIFT);
			end -= window + window;
			filemap_sync(vma, end - window, window, MS_INVALIDATE);
		}

		vma->vm_raend += ra_window;
	}

	return;
}

/*
 * filemap_nopage() is invoked via the vma operations vector for a
 * mapped memory region to read in file data during a page fault.
 *
 * The goto's are kind of ugly, but this streamlines the normal case of having
 * it in the page cache, and handles the special cases reasonably without
 * having a lot of duplicated code.
 */
struct page * filemap_nopage(struct vm_area_struct * area,
	unsigned long address, int no_share)
{
	int error;
	struct file *file = area->vm_file;
	struct inode *inode = file->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	struct page *page, **hash, *old_page;
	unsigned long size = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	unsigned long pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff;

	/*
	 * Semantics for shared and private memory areas are different
	 * past the end of the file. A shared mapping past the last page
	 * of the file is an error and results in a SIGBUS, while a
	 * private mapping just maps in a zero page.
	 */
	if ((pgoff >= size) && (area->vm_mm == current->mm))
		return NULL;

	/*
	 * Do we have something in the page cache already?
	 */
	hash = page_hash(mapping, pgoff);
retry_find:
	page = __find_get_page(mapping, pgoff, hash);
	if (!page)
		goto no_cached_page;

	/*
	 * Ok, found a page in the page cache, now we need to check
	 * that it's up-to-date.
	 */
	if (!Page_Uptodate(page))
		goto page_not_uptodate;

success:
 	/*
	 * Try read-ahead for sequential areas.
	 */
	if (VM_SequentialReadHint(area))
		nopage_sequential_readahead(area, pgoff, size);

	/*
	 * Found the page and have a reference on it, need to check sharing
	 * and possibly copy it over to another page..
	 */
	old_page = page;
	if (no_share) {
		struct page *new_page = page_cache_alloc();

		if (new_page) {
			copy_user_highpage(new_page, old_page, address);
			flush_page_to_ram(new_page);
		} else
			new_page = NOPAGE_OOM;
		page_cache_release(page);
		return new_page;
	}

	flush_page_to_ram(old_page);
	return old_page;

no_cached_page:
	/*
	 * If the requested offset is within our file, try to read a whole 
	 * cluster of pages at once.
	 *
	 * Otherwise, we're off the end of a privately mapped file,
	 * so we need to map a zero page.
	 */
	if ((pgoff < size) && !VM_RandomReadHint(area))
		error = read_cluster_nonblocking(file, pgoff, size);
	else
		error = page_cache_read(file, pgoff);

	/*
	 * The page we want has now been added to the page cache.
	 * In the unlikely event that someone removed it in the
	 * meantime, we'll just come back here and read it again.
	 */
	if (error >= 0)
		goto retry_find;

	/*
	 * An error return from page_cache_read can result if the
	 * system is low on memory, or a problem occurs while trying
	 * to schedule I/O.
	 */
	if (error == -ENOMEM)
		return NOPAGE_OOM;
	return NULL;

page_not_uptodate:
	lock_page(page);
	if (Page_Uptodate(page)) {
		UnlockPage(page);
		goto success;
	}

	if (!mapping->a_ops->readpage(file, page)) {
		wait_on_page(page);
		if (Page_Uptodate(page))
			goto success;
	}

	/*
	 * Umm, take care of errors if the page isn't up-to-date.
	 * Try to re-read it _once_. We do this synchronously,
	 * because there really aren't any performance issues here
	 * and we need to check for errors.
	 */
	lock_page(page);
	if (Page_Uptodate(page)) {
		UnlockPage(page);
		goto success;
	}
	ClearPageError(page);
	if (!mapping->a_ops->readpage(file, page)) {
		wait_on_page(page);
		if (Page_Uptodate(page))
			goto success;
	}

	/*
	 * Things didn't work out. Return zero to tell the
	 * mm layer so, possibly freeing the page cache page first.
	 */
	page_cache_release(page);
	return NULL;
}

static int filemap_write_page(struct file *file,
			      struct page * page,
			      int wait)
{
	/*
	 * If a task terminates while we're swapping the page, the vma and
	 * and file could be released: try_to_swap_out has done a get_file.
	 * vma/file is guaranteed to exist in the unmap/sync cases because
	 * mmap_sem is held.
	 */
	return page->mapping->a_ops->writepage(file, page);
}


/*
 * The page cache takes care of races between somebody
 * trying to swap something out and swap something in
 * at the same time..
 */
extern void wakeup_bdflush(int);
int filemap_swapout(struct page * page, struct file * file)
{
	int retval = filemap_write_page(file, page, 0);
	wakeup_bdflush(0);
	return retval;
}

static inline int filemap_sync_pte(pte_t * ptep, struct vm_area_struct *vma,
	unsigned long address, unsigned int flags)
{
	unsigned long pgoff;
	pte_t pte = *ptep;
	struct page *page;
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
		page_cache_get(page);
	} else {
		if (pte_none(pte))
			return 0;
		flush_cache_page(vma, address);
		pte_clear(ptep);
		flush_tlb_page(vma, address);
		if (!pte_present(pte)) {
			swap_free(pte_to_swp_entry(pte));
			return 0;
		}
		page = pte_page(pte);
		if (!pte_dirty(pte) || flags == MS_INVALIDATE) {
			page_cache_free(page);
			return 0;
		}
	}
	pgoff = (address - vma->vm_start) >> PAGE_CACHE_SHIFT;
	pgoff += vma->vm_pgoff;
	if (page->index != pgoff) {
		printk("weirdness: pgoff=%lu index=%lu address=%lu vm_start=%lu vm_pgoff=%lu\n",
			pgoff, page->index, address, vma->vm_start, vma->vm_pgoff);
	}
	lock_page(page);
	error = filemap_write_page(vma->vm_file, page, 1);
	UnlockPage(page);
	page_cache_free(page);
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
		pmd_ERROR(*pmd);
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
	} while (address && (address < end));
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
		pgd_ERROR(*pgd);
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
	} while (address && (address < end));
	return error;
}

int filemap_sync(struct vm_area_struct * vma, unsigned long address,
	size_t size, unsigned int flags)
{
	pgd_t * dir;
	unsigned long end = address + size;
	int error = 0;

	dir = pgd_offset(vma->vm_mm, address);
	flush_cache_range(vma->vm_mm, end - size, end);
	if (address >= end)
		BUG();
	do {
		error |= filemap_sync_pmd_range(dir, address, end - address, vma, flags);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
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
	unmap:		filemap_unmap,		/* unmap - we need to sync the pages */
	sync:		filemap_sync,
	nopage:		filemap_nopage,
	swapout:	filemap_swapout,
};

/*
 * Private mappings just need to be able to load in the map.
 *
 * (This is actually used for shared mappings as well, if we
 * know they can't ever get write permissions..)
 */
static struct vm_operations_struct file_private_mmap = {
	nopage:		filemap_nopage,
};

/* This is used for a general mmap of a disk file */

int generic_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;
	struct inode *inode = file->f_dentry->d_inode;

	ops = &file_private_mmap;
	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE)) {
		if (!inode->i_mapping->a_ops->writepage)
			return -EINVAL;
		ops = &file_shared_mmap;
	}
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!inode->i_mapping->a_ops->readpage)
		return -ENOEXEC;
	UPDATE_ATIME(inode);
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
			if (file && file->f_op && file->f_op->fsync) {
				down(&file->f_dentry->d_inode->i_sem);
				error = file->f_op->fsync(file, file->f_dentry, 1);
				up(&file->f_dentry->d_inode->i_sem);
			}
		}
		return error;
	}
	return 0;
}

asmlinkage long sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error, error = -EINVAL;

	down(&current->mm->mmap_sem);
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
	up(&current->mm->mmap_sem);
	return error;
}

static inline void setup_read_behavior(struct vm_area_struct * vma,
	int behavior)
{
	VM_ClearReadHint(vma);
	switch(behavior) {
		case MADV_SEQUENTIAL:
			vma->vm_flags |= VM_SEQ_READ;
			break;
		case MADV_RANDOM:
			vma->vm_flags |= VM_RAND_READ;
			break;
		default:
			break;
	}
	return;
}

static long madvise_fixup_start(struct vm_area_struct * vma,
	unsigned long end, int behavior)
{
	struct vm_area_struct * n;

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	n->vm_end = end;
	setup_read_behavior(n, behavior);
	n->vm_raend = 0;
	get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	vmlist_modify_lock(vma->vm_mm);
	vma->vm_pgoff += (end - vma->vm_start) >> PAGE_SHIFT;
	vma->vm_start = end;
	insert_vm_struct(current->mm, n);
	vmlist_modify_unlock(vma->vm_mm);
	return 0;
}

static long madvise_fixup_end(struct vm_area_struct * vma,
	unsigned long start, int behavior)
{
	struct vm_area_struct * n;

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	n->vm_start = start;
	n->vm_pgoff += (n->vm_start - vma->vm_start) >> PAGE_SHIFT;
	setup_read_behavior(n, behavior);
	n->vm_raend = 0;
	get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	vmlist_modify_lock(vma->vm_mm);
	vma->vm_end = start;
	insert_vm_struct(current->mm, n);
	vmlist_modify_unlock(vma->vm_mm);
	return 0;
}

static long madvise_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int behavior)
{
	struct vm_area_struct * left, * right;

	left = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!left)
		return -EAGAIN;
	right = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!right) {
		kmem_cache_free(vm_area_cachep, left);
		return -EAGAIN;
	}
	*left = *vma;
	*right = *vma;
	left->vm_end = start;
	right->vm_start = end;
	right->vm_pgoff += (right->vm_start - left->vm_start) >> PAGE_SHIFT;
	left->vm_raend = 0;
	right->vm_raend = 0;
	atomic_add(2, &vma->vm_file->f_count);

	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	vmlist_modify_lock(vma->vm_mm);
	vma->vm_pgoff += (start - vma->vm_start) >> PAGE_SHIFT;
	vma->vm_start = start;
	vma->vm_end = end;
	setup_read_behavior(vma, behavior);
	vma->vm_raend = 0;
	insert_vm_struct(current->mm, left);
	insert_vm_struct(current->mm, right);
	vmlist_modify_unlock(vma->vm_mm);
	return 0;
}

/*
 * We can potentially split a vm area into separate
 * areas, each area with its own behavior.
 */
static long madvise_behavior(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int behavior)
{
	int error = 0;

	/* This caps the number of vma's this process can own */
	if (vma->vm_mm->map_count > MAX_MAP_COUNT)
		return -ENOMEM;

	if (start == vma->vm_start) {
		if (end == vma->vm_end) {
			setup_read_behavior(vma, behavior);
			vma->vm_raend = 0;
		} else
			error = madvise_fixup_start(vma, end, behavior);
	} else {
		if (end == vma->vm_end)
			error = madvise_fixup_end(vma, start, behavior);
		else
			error = madvise_fixup_middle(vma, start, end, behavior);
	}

	return error;
}

/*
 * Schedule all required I/O operations, then run the disk queue
 * to make sure they are started.  Do not wait for completion.
 */
static long madvise_willneed(struct vm_area_struct * vma,
	unsigned long start, unsigned long end)
{
	long error = -EBADF;
	struct file * file;
	unsigned long size, rlim_rss;

	/* Doesn't work if there's no mapped file. */
	if (!vma->vm_file)
		return error;
	file = vma->vm_file;
	size = (file->f_dentry->d_inode->i_size + PAGE_CACHE_SIZE - 1) >>
							PAGE_CACHE_SHIFT;

	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	/* Make sure this doesn't exceed the process's max rss. */
	error = -EIO;
	rlim_rss = current->rlim ?  current->rlim[RLIMIT_RSS].rlim_cur :
				LONG_MAX; /* default: see resource.h */
	if ((vma->vm_mm->rss + (end - start)) > rlim_rss)
		return error;

	/* round to cluster boundaries if this isn't a "random" area. */
	if (!VM_RandomReadHint(vma)) {
		start = CLUSTER_OFFSET(start);
		end = CLUSTER_OFFSET(end + CLUSTER_PAGES - 1);

		while ((start < end) && (start < size)) {
			error = read_cluster_nonblocking(file, start, size);
			start += CLUSTER_PAGES;
			if (error < 0)
				break;
		}
	} else {
		while ((start < end) && (start < size)) {
			error = page_cache_read(file, start);
			start++;
			if (error < 0)
				break;
		}
	}

	/* Don't wait for someone else to push these requests. */
	run_task_queue(&tq_disk);

	return error;
}

/*
 * Application no longer needs these pages.  If the pages are dirty,
 * it's OK to just throw them away.  The app will be more careful about
 * data it wants to keep.  Be sure to free swap resources too.  The
 * zap_page_range call sets things up for shrink_mmap to actually free
 * these pages later if no one else has touched them in the meantime,
 * although we could add these pages to a global reuse list for
 * shrink_mmap to pick up before reclaiming other pages.
 *
 * NB: This interface discards data rather than pushes it out to swap,
 * as some implementations do.  This has performance implications for
 * applications like large transactional databases which want to discard
 * pages in anonymous maps after committing to backing store the data
 * that was kept in them.  There is no reason to write this data out to
 * the swap area if the application is discarding it.
 *
 * An interface that causes the system to free clean pages and flush
 * dirty pages is already available as msync(MS_INVALIDATE).
 */
static long madvise_dontneed(struct vm_area_struct * vma,
	unsigned long start, unsigned long end)
{
	if (vma->vm_flags & VM_LOCKED)
		return -EINVAL;

	flush_cache_range(vma->vm_mm, start, end);
	zap_page_range(vma->vm_mm, start, end - start);
	flush_tlb_range(vma->vm_mm, start, end);
	return 0;
}

static long madvise_vma(struct vm_area_struct * vma, unsigned long start,
	unsigned long end, int behavior)
{
	long error = -EBADF;

	switch (behavior) {
	case MADV_NORMAL:
	case MADV_SEQUENTIAL:
	case MADV_RANDOM:
		error = madvise_behavior(vma, start, end, behavior);
		break;

	case MADV_WILLNEED:
		error = madvise_willneed(vma, start, end);
		break;

	case MADV_DONTNEED:
		error = madvise_dontneed(vma, start, end);
		break;

	default:
		error = -EINVAL;
		break;
	}
		
	return error;
}

/*
 * The madvise(2) system call.
 *
 * Applications can use madvise() to advise the kernel how it should
 * handle paging I/O in this VM area.  The idea is to help the kernel
 * use appropriate read-ahead and caching techniques.  The information
 * provided is advisory only, and can be safely disregarded by the
 * kernel without affecting the correct operation of the application.
 *
 * behavior values:
 *  MADV_NORMAL - the default behavior is to read clusters.  This
 *		results in some read-ahead and read-behind.
 *  MADV_RANDOM - the system should read the minimum amount of data
 *		on any access, since it is unlikely that the appli-
 *		cation will need more than what it asks for.
 *  MADV_SEQUENTIAL - pages in the given range will probably be accessed
 *		once, so they can be aggressively read ahead, and
 *		can be freed soon after they are accessed.
 *  MADV_WILLNEED - the application is notifying the system to read
 *		some pages ahead.
 *  MADV_DONTNEED - the application is finished with the given range,
 *		so the kernel can free resources associated with it.
 *
 * return values:
 *  zero    - success
 *  -EINVAL - start + len < 0, start is not page-aligned,
 *		"behavior" is not a valid value, or application
 *		is attempting to release locked or shared pages.
 *  -ENOMEM - addresses in the specified range are not currently
 *		mapped, or are outside the AS of the process.
 *  -EIO    - an I/O error occurred while paging in data.
 *  -EBADF  - map exists, but area maps something that isn't a file.
 *  -EAGAIN - a kernel resource was temporarily unavailable.
 */
asmlinkage long sys_madvise(unsigned long start, size_t len, int behavior)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error = 0;
	int error = -EINVAL;

	down(&current->mm->mmap_sem);

	if (start & ~PAGE_MASK)
		goto out;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;

	error = 0;
	if (end == start)
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}

		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = madvise_vma(vma, start, end,
							behavior);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}

		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = madvise_vma(vma, start, vma->vm_end, behavior);
		if (error)
			goto out;
		start = vma->vm_end;
		vma = vma->vm_next;
	}

out:
	up(&current->mm->mmap_sem);
	return error;
}

/*
 * Later we can get more picky about what "in core" means precisely.
 * For now, simply check to see if the page is in the page cache,
 * and is up to date; i.e. that no page-in operation would be required
 * at this time if an application were to map and access this page.
 */
static unsigned char mincore_page(struct vm_area_struct * vma,
	unsigned long pgoff)
{
	unsigned char present = 0;
	struct address_space * as = &vma->vm_file->f_dentry->d_inode->i_data;
	struct page * page, ** hash = page_hash(as, pgoff);

	spin_lock(&pagecache_lock);
	page = __find_page_nolock(as, pgoff, *hash);
	if ((page) && (Page_Uptodate(page)))
		present = 1;
	spin_unlock(&pagecache_lock);

	return present;
}

static long mincore_vma(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, unsigned char * vec)
{
	long error, i, remaining;
	unsigned char * tmp;

	error = -ENOMEM;
	if (!vma->vm_file)
		return error;

	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	error = -EAGAIN;
	tmp = (unsigned char *) __get_free_page(GFP_KERNEL);
	if (!tmp)
		return error;

	/* (end - start) is # of pages, and also # of bytes in "vec */
	remaining = (end - start),

	error = 0;
	for (i = 0; remaining > 0; remaining -= PAGE_SIZE, i++) {
		int j = 0;
		long thispiece = (remaining < PAGE_SIZE) ?
						remaining : PAGE_SIZE;

		while (j < thispiece)
			tmp[j++] = mincore_page(vma, start++);

		if (copy_to_user(vec + PAGE_SIZE * i, tmp, thispiece)) {
			error = -EFAULT;
			break;
		}
	}

	free_page((unsigned long) tmp);
	return error;
}

/*
 * The mincore(2) system call.
 *
 * mincore() returns the memory residency status of the pages in the
 * current process's address space specified by [addr, addr + len).
 * The status is returned in a vector of bytes.  The least significant
 * bit of each byte is 1 if the referenced page is in memory, otherwise
 * it is zero.
 *
 * Because the status of a page can change after mincore() checks it
 * but before it returns to the application, the returned vector may
 * contain stale information.  Only locked pages are guaranteed to
 * remain in memory.
 *
 * return values:
 *  zero    - success
 *  -EFAULT - vec points to an illegal address
 *  -EINVAL - addr is not a multiple of PAGE_CACHE_SIZE,
 *		or len has a nonpositive value
 *  -ENOMEM - Addresses in the range [addr, addr + len] are
 *		invalid for the address space of this process, or
 *		specify one or more pages which are not currently
 *		mapped
 *  -EAGAIN - A kernel resource was temporarily unavailable.
 */
asmlinkage long sys_mincore(unsigned long start, size_t len,
	unsigned char * vec)
{
	int index = 0;
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error = 0;
	long error = -EINVAL;

	down(&current->mm->mmap_sem);

	if (start & ~PAGE_CACHE_MASK)
		goto out;
	len = (len + ~PAGE_CACHE_MASK) & PAGE_CACHE_MASK;
	end = start + len;
	if (end < start)
		goto out;

	error = 0;
	if (end == start)
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}

		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = mincore_vma(vma, start, end,
							&vec[index]);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}

		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = mincore_vma(vma, start, vma->vm_end, &vec[index]);
		if (error)
			goto out;
		index += (vma->vm_end - start) >> PAGE_CACHE_SHIFT;
		start = vma->vm_end;
		vma = vma->vm_next;
	}

out:
	up(&current->mm->mmap_sem);
	return error;
}

static inline
struct page *__read_cache_page(struct address_space *mapping,
				unsigned long index,
				int (*filler)(void *,struct page*),
				void *data)
{
	struct page **hash = page_hash(mapping, index);
	struct page *page, *cached_page = NULL;
	int err;
repeat:
	page = __find_get_page(mapping, index, hash);
	if (!page) {
		if (!cached_page) {
			cached_page = page_cache_alloc();
			if (!cached_page)
				return ERR_PTR(-ENOMEM);
		}
		page = cached_page;
		if (add_to_page_cache_unique(page, mapping, index, hash))
			goto repeat;
		cached_page = NULL;
		err = filler(data, page);
		if (err < 0) {
			page_cache_release(page);
			page = ERR_PTR(err);
		}
	}
	if (cached_page)
		page_cache_free(cached_page);
	return page;
}

/*
 * Read into the page cache. If a page already exists,
 * and Page_Uptodate() is not set, try to fill the page.
 */
struct page *read_cache_page(struct address_space *mapping,
				unsigned long index,
				int (*filler)(void *,struct page*),
				void *data)
{
	struct page *page = __read_cache_page(mapping, index, filler, data);
	int err;

	if (IS_ERR(page) || Page_Uptodate(page))
		goto out;

	lock_page(page);
	if (Page_Uptodate(page)) {
		UnlockPage(page);
		goto out;
	}
	err = filler(data, page);
	if (err < 0) {
		page_cache_release(page);
		page = ERR_PTR(err);
	}
 out:
	return page;
}

static inline struct page * __grab_cache_page(struct address_space *mapping,
				unsigned long index, struct page **cached_page)
{
	struct page *page, **hash = page_hash(mapping, index);
repeat:
	page = __find_lock_page(mapping, index, hash);
	if (!page) {
		if (!*cached_page) {
			*cached_page = page_cache_alloc();
			if (!*cached_page)
				return NULL;
		}
		page = *cached_page;
		if (add_to_page_cache_unique(page, mapping, index, hash))
			goto repeat;
		*cached_page = NULL;
	}
	return page;
}

/*
 * Returns locked page at given index in given cache, creating it if needed.
 */

struct page *grab_cache_page(struct address_space *mapping, unsigned long index)
{
	struct page *cached_page = NULL;
	struct page *page = __grab_cache_page(mapping,index,&cached_page);
	if (cached_page)
		page_cache_free(cached_page);
	return page;
}

static inline void remove_suid(struct inode *inode)
{
	unsigned int mode;

	/* set S_IGID if S_IXGRP is set, and always set S_ISUID */
	mode = (inode->i_mode & S_IXGRP)*(S_ISGID/S_IXGRP) | S_ISUID;

	/* was any of the uid bits set? */
	mode &= inode->i_mode;
	if (mode && !capable(CAP_FSETID)) {
		inode->i_mode &= ~mode;
		mark_inode_dirty(inode);
	}
}

/*
 * Write to a file through the page cache. 
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
generic_file_write(struct file *file,const char *buf,size_t count,loff_t *ppos)
{
	struct inode	*inode = file->f_dentry->d_inode; 
	struct address_space *mapping = inode->i_mapping;
	unsigned long	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	loff_t		pos;
	struct page	*page, *cached_page;
	unsigned long	written;
	long		status;
	int		err;

	cached_page = NULL;

	down(&inode->i_sem);

	pos = *ppos;
	err = -EINVAL;
	if (pos < 0)
		goto out;

	err = file->f_error;
	if (err) {
		file->f_error = 0;
		goto out;
	}

	written = 0;

	if (file->f_flags & O_APPEND)
		pos = inode->i_size;

	/*
	 * Check whether we've reached the file size limit.
	 */
	err = -EFBIG;
	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (count > limit - pos) {
			send_sig(SIGXFSZ, current, 0);
			count = limit - pos;
		}
	}

	status  = 0;
	if (count) {
		remove_suid(inode);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}

	while (count) {
		unsigned long bytes, index, offset;
		char *kaddr;

		/*
		 * Try to find the page in the cache. If it isn't there,
		 * allocate a free page.
		 */
		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		status = -ENOMEM;	/* we'll assign it later anyway */
		page = __grab_cache_page(mapping, index, &cached_page);
		if (!page)
			break;

		/* We have exclusive IO access to the page.. */
		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		status = mapping->a_ops->prepare_write(file, page, offset, offset+bytes);
		if (status)
			goto unlock;
		kaddr = page_address(page);
		status = copy_from_user(kaddr+offset, buf, bytes);
		flush_dcache_page(page);
		if (status)
			goto fail_write;
		status = mapping->a_ops->commit_write(file, page, offset, offset+bytes);
		if (!status)
			status = bytes;

		if (status >= 0) {
			written += status;
			count -= status;
			pos += status;
			buf += status;
		}
unlock:
		/* Mark it unlocked again and drop the page.. */
		UnlockPage(page);
		page_cache_release(page);

		if (status < 0)
			break;
	}
	*ppos = pos;

	if (cached_page)
		page_cache_free(cached_page);

	err = written ? written : status;
out:
	up(&inode->i_sem);
	return err;
fail_write:
	status = -EFAULT;
	ClearPageUptodate(page);
	kunmap(page);
	goto unlock;
}

void __init page_cache_init(unsigned long mempages)
{
	unsigned long htable_size, order;

	htable_size = mempages;
	htable_size *= sizeof(struct page *);
	for(order = 0; (PAGE_SIZE << order) < htable_size; order++)
		;

	do {
		unsigned long tmp = (PAGE_SIZE << order) / sizeof(struct page *);

		page_hash_bits = 0;
		while((tmp >>= 1UL) != 0UL)
			page_hash_bits++;

		page_hash_table = (struct page **)
			__get_free_pages(GFP_ATOMIC, order);
	} while(page_hash_table == NULL && --order > 0);

	printk("Page-cache hash table entries: %d (order: %ld, %ld bytes)\n",
	       (1 << page_hash_bits), order, (PAGE_SIZE << order));
	if (!page_hash_table)
		panic("Failed to allocate page hash table\n");
	memset((void *)page_hash_table, 0, PAGE_HASH_SIZE * sizeof(struct page *));
}
