/*
 * mm/page-writeback.c.
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains functions related to writing back dirty pages at the
 * address_space level.
 *
 * 10Apr2002	akpm@zip.com.au
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/init.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>

/*
 * The maximum number of pages to writeout in a single bdflush/kupdate
 * operation.  We do this so we don't hold I_LOCK against an inode for
 * enormous amounts of time, which would block a userspace task which has
 * been forced to throttle against that inode.  Also, the code reevaluates
 * the dirty each time it has written this many pages.
 */
#define MAX_WRITEBACK_PAGES	1024

/*
 * After a CPU has dirtied this many pages, balance_dirty_pages_ratelimited
 * will look to see if it needs to force writeback or throttling.
 */
static long ratelimit_pages = 32;

static long total_pages;	/* The total number of pages in the machine. */
static int dirty_exceeded;	/* Dirty mem may be over limit */

/*
 * When balance_dirty_pages decides that the caller needs to perform some
 * non-background writeback, this is how many pages it will attempt to write.
 * It should be somewhat larger than RATELIMIT_PAGES to ensure that reasonably
 * large amounts of I/O are submitted.
 */
static inline long sync_writeback_pages(void)
{
	return ratelimit_pages + ratelimit_pages / 2;
}

/* The following parameters are exported via /proc/sys/vm */

/*
 * Start background writeback (via pdflush) at this percentage
 */
int dirty_background_ratio = 10;

/*
 * The generator of dirty data starts writeback at this percentage
 */
int vm_dirty_ratio = 40;

/*
 * The interval between `kupdate'-style writebacks, in centiseconds
 * (hundredths of a second)
 */
int dirty_writeback_centisecs = 5 * 100;

/*
 * The longest number of centiseconds for which data is allowed to remain dirty
 */
int dirty_expire_centisecs = 30 * 100;

/* End of sysctl-exported parameters */


static void background_writeout(unsigned long _min_pages);

/*
 * Work out the current dirty-memory clamping and background writeout
 * thresholds.
 *
 * The main aim here is to lower them aggressively if there is a lot of mapped
 * memory around.  To avoid stressing page reclaim with lots of unreclaimable
 * pages.  It is better to clamp down on writers than to start swapping, and
 * performing lots of scanning.
 *
 * We only allow 1/2 of the currently-unmapped memory to be dirtied.
 *
 * We don't permit the clamping level to fall below 5% - that is getting rather
 * excessive.
 *
 * We make sure that the background writeout level is below the adjusted
 * clamping level.
 */
static void
get_dirty_limits(struct page_state *ps, long *pbackground, long *pdirty)
{
	int background_ratio;		/* Percentages */
	int dirty_ratio;
	int unmapped_ratio;
	long background;
	long dirty;
	struct task_struct *tsk;

	get_page_state(ps);

	unmapped_ratio = 100 - (ps->nr_mapped * 100) / total_pages;

	dirty_ratio = vm_dirty_ratio;
	if (dirty_ratio > unmapped_ratio / 2)
		dirty_ratio = unmapped_ratio / 2;

	if (dirty_ratio < 5)
		dirty_ratio = 5;

	background_ratio = dirty_background_ratio;
	if (background_ratio >= dirty_ratio)
		background_ratio = dirty_ratio / 2;

	background = (background_ratio * total_pages) / 100;
	dirty = (dirty_ratio * total_pages) / 100;
	tsk = current;
	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk)) {
		background += background / 4;
		dirty += dirty / 4;
	}
	*pbackground = background;
	*pdirty = dirty;
}

/*
 * balance_dirty_pages() must be called by processes which are generating dirty
 * data.  It looks at the number of dirty pages in the machine and will force
 * the caller to perform writeback if the system is over `vm_dirty_ratio'.
 * If we're over `background_thresh' then pdflush is woken to perform some
 * writeout.
 */
static void balance_dirty_pages(struct address_space *mapping)
{
	struct page_state ps;
	long nr_reclaimable;
	long background_thresh;
	long dirty_thresh;
	unsigned long pages_written = 0;
	unsigned long write_chunk = sync_writeback_pages();

	struct backing_dev_info *bdi = mapping->backing_dev_info;

	for (;;) {
		struct writeback_control wbc = {
			.bdi		= bdi,
			.sync_mode	= WB_SYNC_NONE,
			.older_than_this = NULL,
			.nr_to_write	= write_chunk,
		};

		get_dirty_limits(&ps, &background_thresh, &dirty_thresh);
		nr_reclaimable = ps.nr_dirty + ps.nr_unstable;
		if (nr_reclaimable + ps.nr_writeback <= dirty_thresh)
			break;

		dirty_exceeded = 1;

		/* Note: nr_reclaimable denotes nr_dirty + nr_unstable.
		 * Unstable writes are a feature of certain networked
		 * filesystems (i.e. NFS) in which data may have been
		 * written to the server's write cache, but has not yet
		 * been flushed to permanent storage.
		 */
		if (nr_reclaimable) {
			writeback_inodes(&wbc);
			get_dirty_limits(&ps, &background_thresh,
					&dirty_thresh);
			nr_reclaimable = ps.nr_dirty + ps.nr_unstable;
			if (nr_reclaimable + ps.nr_writeback <= dirty_thresh)
				break;
			pages_written += write_chunk - wbc.nr_to_write;
			if (pages_written >= write_chunk)
				break;		/* We've done our duty */
		}
		blk_congestion_wait(WRITE, HZ/10);
	}

	if (nr_reclaimable + ps.nr_writeback <= dirty_thresh)
		dirty_exceeded = 0;

	if (!writeback_in_progress(bdi) && nr_reclaimable > background_thresh)
		pdflush_operation(background_writeout, 0);
}

/**
 * balance_dirty_pages_ratelimited - balance dirty memory state
 * @mapping - address_space which was dirtied
 *
 * Processes which are dirtying memory should call in here once for each page
 * which was newly dirtied.  The function will periodically check the system's
 * dirty state and will initiate writeback if needed.
 *
 * On really big machines, get_page_state is expensive, so try to avoid calling
 * it too often (ratelimiting).  But once we're over the dirty memory limit we
 * decrease the ratelimiting by a lot, to prevent individual processes from
 * overshooting the limit by (ratelimit_pages) each.
 */
void balance_dirty_pages_ratelimited(struct address_space *mapping)
{
	static DEFINE_PER_CPU(int, ratelimits) = 0;
	long ratelimit;

	ratelimit = ratelimit_pages;
	if (dirty_exceeded)
		ratelimit = 8;

	/*
	 * Check the rate limiting. Also, we do not want to throttle real-time
	 * tasks in balance_dirty_pages(). Period.
	 */
	if (get_cpu_var(ratelimits)++ >= ratelimit) {
		__get_cpu_var(ratelimits) = 0;
		put_cpu_var(ratelimits);
		balance_dirty_pages(mapping);
		return;
	}
	put_cpu_var(ratelimits);
}
EXPORT_SYMBOL(balance_dirty_pages_ratelimited);

/*
 * writeback at least _min_pages, and keep writing until the amount of dirty
 * memory is less than the background threshold, or until we're all clean.
 */
static void background_writeout(unsigned long _min_pages)
{
	long min_pages = _min_pages;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = NULL,
		.nr_to_write	= 0,
		.nonblocking	= 1,
	};

	for ( ; ; ) {
		struct page_state ps;
		long background_thresh;
		long dirty_thresh;

		get_dirty_limits(&ps, &background_thresh, &dirty_thresh);
		if (ps.nr_dirty + ps.nr_unstable < background_thresh
				&& min_pages <= 0)
			break;
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		writeback_inodes(&wbc);
		min_pages -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
		if (wbc.nr_to_write > 0) {
			/* Wrote less than expected */
			if (wbc.encountered_congestion)
				blk_congestion_wait(WRITE, HZ/10);
			else
				break;
		}
	}
}

/*
 * Start writeback of `nr_pages' pages.  If `nr_pages' is zero, write back
 * the whole world.  Returns 0 if a pdflush thread was dispatched.  Returns
 * -1 if all pdflush threads were busy.
 */
int wakeup_bdflush(long nr_pages)
{
	if (nr_pages == 0) {
		struct page_state ps;

		get_page_state(&ps);
		nr_pages = ps.nr_dirty + ps.nr_unstable;
	}
	return pdflush_operation(background_writeout, nr_pages);
}

static struct timer_list wb_timer;

/*
 * Periodic writeback of "old" data.
 *
 * Define "old": the first time one of an inode's pages is dirtied, we mark the
 * dirtying-time in the inode's address_space.  So this periodic writeback code
 * just walks the superblock inode list, writing back any inodes which are
 * older than a specific point in time.
 *
 * Try to run once per dirty_writeback_centisecs.  But if a writeback event
 * takes longer than a dirty_writeback_centisecs interval, then leave a
 * one-second gap.
 *
 * older_than_this takes precedence over nr_to_write.  So we'll only write back
 * all dirty pages if they are all attached to "old" mappings.
 */
static void wb_kupdate(unsigned long arg)
{
	unsigned long oldest_jif;
	unsigned long start_jif;
	unsigned long next_jif;
	long nr_to_write;
	struct page_state ps;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = &oldest_jif,
		.nr_to_write	= 0,
		.nonblocking	= 1,
		.for_kupdate	= 1,
	};

	sync_supers();

	get_page_state(&ps);
	oldest_jif = jiffies - (dirty_expire_centisecs * HZ) / 100;
	start_jif = jiffies;
	next_jif = start_jif + (dirty_writeback_centisecs * HZ) / 100;
	nr_to_write = ps.nr_dirty + ps.nr_unstable +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);
	while (nr_to_write > 0) {
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		writeback_inodes(&wbc);
		if (wbc.nr_to_write > 0) {
			if (wbc.encountered_congestion)
				blk_congestion_wait(WRITE, HZ/10);
			else
				break;	/* All the old data is written */
		}
		nr_to_write -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
	}
	if (time_before(next_jif, jiffies + HZ))
		next_jif = jiffies + HZ;
	if (dirty_writeback_centisecs)
		mod_timer(&wb_timer, next_jif);
}

/*
 * sysctl handler for /proc/sys/vm/dirty_writeback_centisecs
 */
int dirty_writeback_centisecs_handler(ctl_table *table, int write,
		struct file *file, void __user *buffer, size_t *length)
{
	proc_dointvec(table, write, file, buffer, length);
	if (dirty_writeback_centisecs) {
		mod_timer(&wb_timer,
			jiffies + (dirty_writeback_centisecs * HZ) / 100);
	} else {
		del_timer(&wb_timer);
	}
	return 0;
}

static void wb_timer_fn(unsigned long unused)
{
	if (pdflush_operation(wb_kupdate, 0) < 0)
		mod_timer(&wb_timer, jiffies + HZ); /* delay 1 second */

}

/*
 * If ratelimit_pages is too high then we can get into dirty-data overload
 * if a large number of processes all perform writes at the same time.
 * If it is too low then SMP machines will call the (expensive) get_page_state
 * too often.
 *
 * Here we set ratelimit_pages to a level which ensures that when all CPUs are
 * dirtying in parallel, we cannot go more than 3% (1/32) over the dirty memory
 * thresholds before writeback cuts in.
 *
 * But the limit should not be set too high.  Because it also controls the
 * amount of memory which the balance_dirty_pages() caller has to write back.
 * If this is too large then the caller will block on the IO queue all the
 * time.  So limit it to four megabytes - the balance_dirty_pages() caller
 * will write six megabyte chunks, max.
 */

static void set_ratelimit(void)
{
	ratelimit_pages = total_pages / (num_online_cpus() * 32);
	if (ratelimit_pages < 16)
		ratelimit_pages = 16;
	if (ratelimit_pages * PAGE_CACHE_SIZE > 4096 * 1024)
		ratelimit_pages = (4096 * 1024) / PAGE_CACHE_SIZE;
}

static int
ratelimit_handler(struct notifier_block *self, unsigned long u, void *v)
{
	set_ratelimit();
	return 0;
}

static struct notifier_block ratelimit_nb = {
	.notifier_call	= ratelimit_handler,
	.next		= NULL,
};

/*
 * If the machine has a large highmem:lowmem ratio then scale back the default
 * dirty memory thresholds: allowing too much dirty highmem pins an excessive
 * number of buffer_heads.
 */
void __init page_writeback_init(void)
{
	long buffer_pages = nr_free_buffer_pages();
	long correction;

	total_pages = nr_free_pagecache_pages();

	correction = (100 * 4 * buffer_pages) / total_pages;

	if (correction < 100) {
		dirty_background_ratio *= correction;
		dirty_background_ratio /= 100;
		vm_dirty_ratio *= correction;
		vm_dirty_ratio /= 100;
	}

	init_timer(&wb_timer);
	wb_timer.expires = jiffies + (dirty_writeback_centisecs * HZ) / 100;
	wb_timer.data = 0;
	wb_timer.function = wb_timer_fn;
	add_timer(&wb_timer);
	set_ratelimit();
	register_cpu_notifier(&ratelimit_nb);
}

int do_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	if (mapping->a_ops->writepages)
		return mapping->a_ops->writepages(mapping, wbc);
	return generic_writepages(mapping, wbc);
}

/**
 * write_one_page - write out a single page and optionally wait on I/O
 *
 * @page - the page to write
 * @wait - if true, wait on writeout
 *
 * The page must be locked by the caller and will be unlocked upon return.
 *
 * write_one_page() returns a negative error code if I/O failed.
 */
int write_one_page(struct page *page, int wait)
{
	struct address_space *mapping = page->mapping;
	int ret = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 1,
	};

	BUG_ON(!PageLocked(page));

	if (wait)
		wait_on_page_writeback(page);

	spin_lock(&mapping->page_lock);
	list_del(&page->list);
	if (test_clear_page_dirty(page)) {
		list_add(&page->list, &mapping->locked_pages);
		page_cache_get(page);
		spin_unlock(&mapping->page_lock);
		ret = mapping->a_ops->writepage(page, &wbc);
		if (ret == 0 && wait) {
			wait_on_page_writeback(page);
			if (PageError(page))
				ret = -EIO;
		}
		page_cache_release(page);
	} else {
		list_add(&page->list, &mapping->clean_pages);
		spin_unlock(&mapping->page_lock);
		unlock_page(page);
	}
	return ret;
}
EXPORT_SYMBOL(write_one_page);

/*
 * For address_spaces which do not use buffers.  Just set the page's dirty bit
 * and move it to the dirty_pages list.  Also perform space reservation if
 * required.
 *
 * __set_page_dirty_nobuffers() may return -ENOSPC.  But if it does, the page
 * is still safe, as long as it actually manages to find some blocks at
 * writeback time.
 *
 * This is also used when a single buffer is being dirtied: we want to set the
 * page dirty in that case, but not all the buffers.  This is a "bottom-up"
 * dirtying, whereas __set_page_dirty_buffers() is a "top-down" dirtying.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	int ret = 0;

	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			spin_lock(&mapping->page_lock);
			if (page->mapping) {	/* Race with truncate? */
				BUG_ON(page->mapping != mapping);
				if (!mapping->backing_dev_info->memory_backed)
					inc_page_state(nr_dirty);
				list_del(&page->list);
				list_add(&page->list, &mapping->dirty_pages);
			}
			spin_unlock(&mapping->page_lock);
			if (!PageSwapCache(page))
				__mark_inode_dirty(mapping->host,
							I_DIRTY_PAGES);
		}
	}
	return ret;
}
EXPORT_SYMBOL(__set_page_dirty_nobuffers);

/*
 * set_page_dirty() is racy if the caller has no reference against
 * page->mapping->host, and if the page is unlocked.  This is because another
 * CPU could truncate the page off the mapping and then free the mapping.
 *
 * Usually, the page _is_ locked, or the caller is a user-space process which
 * holds a reference on the inode by having an open file.
 *
 * In other cases, the page should be locked before running set_page_dirty().
 */
int set_page_dirty_lock(struct page *page)
{
	int ret;

	lock_page(page);
	ret = set_page_dirty(page);
	unlock_page(page);
	return ret;
}
EXPORT_SYMBOL(set_page_dirty_lock);

/*
 * Clear a page's dirty flag, while caring for dirty memory accounting. 
 * Returns true if the page was previously dirty.
 */
int test_clear_page_dirty(struct page *page)
{
	if (TestClearPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping && !mapping->backing_dev_info->memory_backed)
			dec_page_state(nr_dirty);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(test_clear_page_dirty);
