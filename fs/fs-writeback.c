/*
 * fs/fs-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains all the functions related to writing back and waiting
 * upon dirty inodes against superblocks, and writing back dirty
 * pages against inodes.  ie: data writeback.  Writeout of the
 * inode itself is not handled here.
 *
 * 10Apr2002	akpm@zip.com.au
 *		Split out of fs/inode.c
 *		Additions for address_space-based writeback
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>

extern struct super_block *blockdev_superblock;

/**
 *	__mark_inode_dirty -	internal function
 *	@inode: inode to mark
 *	@flags: what kind of dirty (i.e. I_DIRTY_SYNC)
 *	Mark an inode as dirty. Callers should use mark_inode_dirty or
 *  	mark_inode_dirty_sync.
 *
 * Put the inode on the super block's dirty list.
 *
 * CAREFUL! We mark it dirty unconditionally, but move it onto the
 * dirty list only if it is hashed or if it refers to a blockdev.
 * If it was not hashed, it will never be added to the dirty list
 * even if it is later hashed, as it will have been marked dirty already.
 *
 * In short, make sure you hash any inodes _before_ you start marking
 * them dirty.
 *
 * This function *must* be atomic for the I_DIRTY_PAGES case -
 * set_page_dirty() is called under spinlock in several places.
 */
void __mark_inode_dirty(struct inode *inode, int flags)
{
	struct super_block *sb = inode->i_sb;

	/*
	 * Don't do this for I_DIRTY_PAGES - that doesn't actually
	 * dirty the inode itself
	 */
	if (flags & (I_DIRTY_SYNC | I_DIRTY_DATASYNC)) {
		if (sb->s_op->dirty_inode)
			sb->s_op->dirty_inode(inode);
	}

	/*
	 * make sure that changes are seen by all cpus before we test i_state
	 * -- mikulas
	 */
	smp_mb();

	/* avoid the locking if we can */
	if ((inode->i_state & flags) == flags)
		return;

	spin_lock(&inode_lock);
	if ((inode->i_state & flags) != flags) {
		const int was_dirty = inode->i_state & I_DIRTY;
		struct address_space *mapping = inode->i_mapping;

		inode->i_state |= flags;

		/*
		 * If the inode is locked, just update its dirty state. 
		 * The unlocker will place the inode on the appropriate
		 * superblock list, based upon its state.
		 */
		if (inode->i_state & I_LOCK)
			goto out;

		/*
		 * Only add valid (hashed) inodes to the superblock's
		 * dirty list.  Add blockdev inodes as well.
		 */
		if (!S_ISBLK(inode->i_mode)) {
			if (hlist_unhashed(&inode->i_hash))
				goto out;
			if (inode->i_state & (I_FREEING|I_CLEAR))
				goto out;
		}

		/*
		 * If the inode was already on s_dirty or s_io, don't
		 * reposition it (that would break s_dirty time-ordering).
		 */
		if (!was_dirty) {
			mapping->dirtied_when = jiffies|1; /* 0 is special */
			list_move(&inode->i_list, &sb->s_dirty);
		}
	}
out:
	spin_unlock(&inode_lock);
}

EXPORT_SYMBOL(__mark_inode_dirty);

static void write_inode(struct inode *inode, int sync)
{
	if (inode->i_sb->s_op->write_inode && !is_bad_inode(inode))
		inode->i_sb->s_op->write_inode(inode, sync);
}

/*
 * Write a single inode's dirty pages and inode data out to disk.
 * If `wait' is set, wait on the writeout.
 *
 * The whole writeout design is quite complex and fragile.  We want to avoid
 * starvation of particular inodes when others are being redirtied, prevent
 * livelocks, etc.
 *
 * So what we do is to move all pages which are to be written from dirty_pages
 * onto io_pages.  And keep on writing io_pages until it's empty.  Refusing to
 * move more pages onto io_pages until io_pages is empty.  Once that point has
 * been reached, we are ready to take another pass across the inode's dirty
 * pages.
 *
 * Called under inode_lock.
 */
static void
__sync_single_inode(struct inode *inode, struct writeback_control *wbc)
{
	unsigned dirty;
	struct address_space *mapping = inode->i_mapping;
	struct super_block *sb = inode->i_sb;
	int wait = wbc->sync_mode == WB_SYNC_ALL;

	BUG_ON(inode->i_state & I_LOCK);

	/* Set I_LOCK, reset I_DIRTY */
	dirty = inode->i_state & I_DIRTY;
	inode->i_state |= I_LOCK;
	inode->i_state &= ~I_DIRTY;

	/*
	 * smp_rmb(); note: if you remove write_lock below, you must add this.
	 * mark_inode_dirty doesn't take spinlock, make sure that inode is not
	 * read speculatively by this cpu before &= ~I_DIRTY  -- mikulas
	 */

	spin_lock(&mapping->page_lock);
	if (wait || !wbc->for_kupdate || list_empty(&mapping->io_pages))
		list_splice_init(&mapping->dirty_pages, &mapping->io_pages);
	spin_unlock(&mapping->page_lock);
	spin_unlock(&inode_lock);

	do_writepages(mapping, wbc);

	/* Don't write the inode if only I_DIRTY_PAGES was set */
	if (dirty & (I_DIRTY_SYNC | I_DIRTY_DATASYNC))
		write_inode(inode, wait);

	if (wait)
		filemap_fdatawait(mapping);

	spin_lock(&inode_lock);
	inode->i_state &= ~I_LOCK;
	if (!(inode->i_state & I_FREEING)) {
		if (!list_empty(&mapping->io_pages)) {
		 	/* Needs more writeback */
			inode->i_state |= I_DIRTY_PAGES;
		} else if (!list_empty(&mapping->dirty_pages)) {
			/* Redirtied */
			inode->i_state |= I_DIRTY_PAGES;
			mapping->dirtied_when = jiffies|1;
			list_move(&inode->i_list, &sb->s_dirty);
		} else if (inode->i_state & I_DIRTY) {
			/* Redirtied */
			mapping->dirtied_when = jiffies|1;
			list_move(&inode->i_list, &sb->s_dirty);
		} else if (atomic_read(&inode->i_count)) {
			mapping->dirtied_when = 0;
			list_move(&inode->i_list, &inode_in_use);
		} else {
			mapping->dirtied_when = 0;
			list_move(&inode->i_list, &inode_unused);
		}
	}
	wake_up_inode(inode);
}

/*
 * Write out an inode's dirty pages.  Called under inode_lock.
 */
static void
__writeback_single_inode(struct inode *inode,
			struct writeback_control *wbc)
{
	if ((wbc->sync_mode != WB_SYNC_ALL) && (inode->i_state & I_LOCK)) {
		list_move(&inode->i_list, &inode->i_sb->s_dirty);
		return;
	}

	/*
	 * It's a data-integrity sync.  We must wait.
	 */
	while (inode->i_state & I_LOCK) {
		__iget(inode);
		spin_unlock(&inode_lock);
		__wait_on_inode(inode);
		iput(inode);
		spin_lock(&inode_lock);
	}
	__sync_single_inode(inode, wbc);
}

/*
 * Write out a superblock's list of dirty inodes.  A wait will be performed
 * upon no inodes, all inodes or the final one, depending upon sync_mode.
 *
 * If older_than_this is non-NULL, then only write out mappings which
 * had their first dirtying at a time earlier than *older_than_this.
 *
 * If we're a pdlfush thread, then implement pdflush collision avoidance
 * against the entire list.
 *
 * WB_SYNC_HOLD is a hack for sys_sync(): reattach the inode to sb->s_dirty so
 * that it can be located for waiting on in __writeback_single_inode().
 *
 * Called under inode_lock.
 *
 * If `bdi' is non-zero then we're being asked to writeback a specific queue.
 * This function assumes that the blockdev superblock's inodes are backed by
 * a variety of queues, so all inodes are searched.  For other superblocks,
 * assume that all inodes are backed by the same queue.
 *
 * FIXME: this linear search could get expensive with many fileystems.  But
 * how to fix?  We need to go from an address_space to all inodes which share
 * a queue with that address_space.  (Easy: have a global "dirty superblocks"
 * list).
 *
 * The inodes to be written are parked on sb->s_io.  They are moved back onto
 * sb->s_dirty as they are selected for writing.  This way, none can be missed
 * on the writer throttling path, and we get decent balancing between many
 * throttled threads: we don't want them all piling up on __wait_on_inode.
 */
static void
sync_sb_inodes(struct super_block *sb, struct writeback_control *wbc)
{
	const unsigned long start = jiffies;	/* livelock avoidance */

	if (!wbc->for_kupdate || list_empty(&sb->s_io))
		list_splice_init(&sb->s_dirty, &sb->s_io);

	while (!list_empty(&sb->s_io)) {
		struct inode *inode = list_entry(sb->s_io.prev,
						struct inode, i_list);
		struct address_space *mapping = inode->i_mapping;
		struct backing_dev_info *bdi = mapping->backing_dev_info;

		if (bdi->memory_backed) {
			if (sb == blockdev_superblock) {
				/*
				 * Dirty memory-backed blockdev: the ramdisk
				 * driver does this.
				 */
				list_move(&inode->i_list, &sb->s_dirty);
				continue;
			}
			/*
			 * Assume that all inodes on this superblock are memory
			 * backed.  Skip the superblock.
			 */
			break;
		}

		if (wbc->nonblocking && bdi_write_congested(bdi)) {
			wbc->encountered_congestion = 1;
			if (sb != blockdev_superblock)
				break;		/* Skip a congested fs */
			list_move(&inode->i_list, &sb->s_dirty);
			continue;		/* Skip a congested blockdev */
		}

		if (wbc->bdi && bdi != wbc->bdi) {
			if (sb != blockdev_superblock)
				break;		/* fs has the wrong queue */
			list_move(&inode->i_list, &sb->s_dirty);
			continue;		/* blockdev has wrong queue */
		}

		/* Was this inode dirtied after sync_sb_inodes was called? */
		if (time_after(mapping->dirtied_when, start))
			break;

		/* Was this inode dirtied too recently? */
		if (wbc->older_than_this && time_after(mapping->dirtied_when,
						*wbc->older_than_this))
			break;

		/* Is another pdflush already flushing this queue? */
		if (current_is_pdflush() && !writeback_acquire(bdi))
			break;

		BUG_ON(inode->i_state & I_FREEING);
		__iget(inode);
		__writeback_single_inode(inode, wbc);
		if (wbc->sync_mode == WB_SYNC_HOLD) {
			mapping->dirtied_when = jiffies|1;
			list_move(&inode->i_list, &sb->s_dirty);
		}
		if (current_is_pdflush())
			writeback_release(bdi);
		spin_unlock(&inode_lock);
		iput(inode);
		spin_lock(&inode_lock);
		if (wbc->nr_to_write <= 0)
			break;
	}
	return;		/* Leave any unwritten inodes on s_io */
}

/*
 * Start writeback of dirty pagecache data against all unlocked inodes.
 *
 * Note:
 * We don't need to grab a reference to superblock here. If it has non-empty
 * ->s_dirty it's hadn't been killed yet and kill_super() won't proceed
 * past sync_inodes_sb() until both the ->s_dirty and ->s_io lists are
 * empty. Since __sync_single_inode() regains inode_lock before it finally moves
 * inode from superblock lists we are OK.
 *
 * If `older_than_this' is non-zero then only flush inodes which have a
 * flushtime older than *older_than_this.
 *
 * If `bdi' is non-zero then we will scan the first inode against each
 * superblock until we find the matching ones.  One group will be the dirty
 * inodes against a filesystem.  Then when we hit the dummy blockdev superblock,
 * sync_sb_inodes will seekout the blockdev which matches `bdi'.  Maybe not
 * super-efficient but we're about to do a ton of I/O...
 */
void
writeback_inodes(struct writeback_control *wbc)
{
	struct super_block *sb;

	spin_lock(&inode_lock);
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		if (!list_empty(&sb->s_dirty) || !list_empty(&sb->s_io)) {
			spin_unlock(&sb_lock);
			sync_sb_inodes(sb, wbc);
			spin_lock(&sb_lock);
		}
		if (wbc->nr_to_write <= 0)
			break;
	}
	spin_unlock(&sb_lock);
	spin_unlock(&inode_lock);
}

/*
 * writeback and wait upon the filesystem's dirty inodes.  The caller will
 * do this in two passes - one to write, and one to wait.  WB_SYNC_HOLD is
 * used to park the written inodes on sb->s_dirty for the wait pass.
 *
 * A finite limit is set on the number of pages which will be written.
 * To prevent infinite livelock of sys_sync().
 *
 * We add in the number of potentially dirty inodes, because each inode write
 * can dirty pagecache in the underlying blockdev.
 */
void sync_inodes_sb(struct super_block *sb, int wait)
{
	struct page_state ps;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= wait ? WB_SYNC_ALL : WB_SYNC_HOLD,
		.older_than_this = NULL,
		.nr_to_write	= 0,
	};

	get_page_state(&ps);
	wbc.nr_to_write = ps.nr_dirty + ps.nr_unstable +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused) +
			ps.nr_dirty + ps.nr_unstable;
	wbc.nr_to_write += wbc.nr_to_write / 2;		/* Bit more for luck */
	spin_lock(&inode_lock);
	sync_sb_inodes(sb, &wbc);
	spin_unlock(&inode_lock);
}

/*
 * Rather lame livelock avoidance.
 */
static void set_sb_syncing(int val)
{
	struct super_block *sb;
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		sb->s_syncing = val;
	}
	spin_unlock(&sb_lock);
}

/*
 * Find a superblock with inodes that need to be synced
 */
static struct super_block *get_super_to_sync(void)
{
	struct super_block *sb;
restart:
	spin_lock(&sb_lock);
	sb = sb_entry(super_blocks.prev);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.prev)) {
		if (sb->s_syncing)
			continue;
		sb->s_syncing = 1;
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (!sb->s_root) {
			drop_super(sb);
			goto restart;
		}
		return sb;
	}
	spin_unlock(&sb_lock);
	return NULL;
}

/**
 * sync_inodes
 *
 * sync_inodes() goes through each super block's dirty inode list, writes the
 * inodes out, waits on the writeout and puts the inodes back on the normal
 * list.
 *
 * This is for sys_sync().  fsync_dev() uses the same algorithm.  The subtle
 * part of the sync functions is that the blockdev "superblock" is processed
 * last.  This is because the write_inode() function of a typical fs will
 * perform no I/O, but will mark buffers in the blockdev mapping as dirty.
 * What we want to do is to perform all that dirtying first, and then write
 * back all those inode blocks via the blockdev mapping in one sweep.  So the
 * additional (somewhat redundant) sync_blockdev() calls here are to make
 * sure that really happens.  Because if we call sync_inodes_sb(wait=1) with
 * outstanding dirty inodes, the writeback goes block-at-a-time within the
 * filesystem's write_inode().  This is extremely slow.
 */
void sync_inodes(int wait)
{
	struct super_block *sb;

	set_sb_syncing(0);
	while ((sb = get_super_to_sync()) != NULL) {
		sync_inodes_sb(sb, 0);
		sync_blockdev(sb->s_bdev);
		drop_super(sb);
	}
	if (wait) {
		set_sb_syncing(0);
		while ((sb = get_super_to_sync()) != NULL) {
			sync_inodes_sb(sb, 1);
			sync_blockdev(sb->s_bdev);
			drop_super(sb);
		}
	}
}

/**
 *	write_inode_now	-	write an inode to disk
 *	@inode: inode to write to disk
 *	@sync: whether the write should be synchronous or not
 *
 *	This function commits an inode to disk immediately if it is
 *	dirty. This is primarily needed by knfsd.
 */
 
void write_inode_now(struct inode *inode, int sync)
{
	struct writeback_control wbc = {
		.nr_to_write = LONG_MAX,
		.sync_mode = WB_SYNC_ALL,
	};

	spin_lock(&inode_lock);
	__writeback_single_inode(inode, &wbc);
	spin_unlock(&inode_lock);
	if (sync)
		wait_on_inode(inode);
}

EXPORT_SYMBOL(write_inode_now);

/**
 * generic_osync_inode - flush all dirty data for a given inode to disk
 * @inode: inode to write
 * @what:  what to write and wait upon
 *
 * This can be called by file_write functions for files which have the
 * O_SYNC flag set, to flush dirty writes to disk.
 *
 * @what is a bitmask, specifying which part of the inode's data should be
 * written and waited upon:
 *
 *    OSYNC_DATA:     i_mapping's dirty data
 *    OSYNC_METADATA: the buffers at i_mapping->private_list
 *    OSYNC_INODE:    the inode itself
 */

int generic_osync_inode(struct inode *inode, int what)
{
	int err = 0;
	int need_write_inode_now = 0;
	int err2;

	current->flags |= PF_SYNCWRITE;
	if (what & OSYNC_DATA)
		err = filemap_fdatawrite(inode->i_mapping);
	if (what & (OSYNC_METADATA|OSYNC_DATA)) {
		err2 = sync_mapping_buffers(inode->i_mapping);
		if (!err)
			err = err2;
	}
	if (what & OSYNC_DATA) {
		err2 = filemap_fdatawait(inode->i_mapping);
		if (!err)
			err = err2;
	}
	current->flags &= ~PF_SYNCWRITE;

	spin_lock(&inode_lock);
	if ((inode->i_state & I_DIRTY) &&
	    ((what & OSYNC_INODE) || (inode->i_state & I_DIRTY_DATASYNC)))
		need_write_inode_now = 1;
	spin_unlock(&inode_lock);

	if (need_write_inode_now)
		write_inode_now(inode, 1);
	else
		wait_on_inode(inode);

	return err;
}

EXPORT_SYMBOL(generic_osync_inode);

/**
 * writeback_acquire: attempt to get exclusive writeback access to a device
 * @bdi: the device's backing_dev_info structure
 *
 * It is a waste of resources to have more than one pdflush thread blocked on
 * a single request queue.  Exclusion at the request_queue level is obtained
 * via a flag in the request_queue's backing_dev_info.state.
 *
 * Non-request_queue-backed address_spaces will share default_backing_dev_info,
 * unless they implement their own.  Which is somewhat inefficient, as this
 * may prevent concurrent writeback against multiple devices.
 */
int writeback_acquire(struct backing_dev_info *bdi)
{
	return !test_and_set_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_in_progress: determine whether there is writeback in progress
 *                        against a backing device.
 * @bdi: the device's backing_dev_info structure.
 */
int writeback_in_progress(struct backing_dev_info *bdi)
{
	return test_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_release: relinquish exclusive writeback access against a device.
 * @bdi: the device's backing_dev_info structure
 */
void writeback_release(struct backing_dev_info *bdi)
{
	BUG_ON(!writeback_in_progress(bdi));
	clear_bit(BDI_pdflush, &bdi->state);
}
