/*
 *  linux/fs/buffer.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting an interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it.
 */

/* Start bdflush() with kernel_thread not syscall - Paul Gortmaker, 12/95 */

/* Removed a lot of unnecessary code and simplified things now that
 * the buffer cache isn't our primary cache - Andrew Tridgell 12/96
 */

/* Speed up hash, lru, and free list operations.  Use gfp() for allocating
 * hash table, use SLAB cache for buffer heads. -DaveM
 */

/* Added 32k buffer block sizes - these are required older ARM systems.
 * - RMK
 */

#include <linux/malloc.h>
#include <linux/locks.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/sysrq.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/quotaops.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#define NR_SIZES 7
static char buffersize_index[65] =
{-1,  0,  1, -1,  2, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1,
  4, -1, -1, -1, -1, -1, -1, -1, -1,-1, -1, -1, -1, -1, -1, -1,
  5, -1, -1, -1, -1, -1, -1, -1, -1,-1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1,-1, -1, -1, -1, -1, -1, -1,
  6};

#define BUFSIZE_INDEX(X) ((int) buffersize_index[(X)>>9])
#define MAX_BUF_PER_PAGE (PAGE_SIZE / 512)
#define NR_RESERVED (2*MAX_BUF_PER_PAGE)
#define MAX_UNUSED_BUFFERS NR_RESERVED+20 /* don't ever have more than this 
					     number of unused buffer heads */

/*
 * Hash table mask..
 */
static unsigned long bh_hash_mask = 0;

static int grow_buffers(int size);

static struct buffer_head ** hash_table;
static struct buffer_head * lru_list[NR_LIST] = {NULL, };
static struct buffer_head * free_list[NR_SIZES] = {NULL, };

static kmem_cache_t *bh_cachep;

static struct buffer_head * unused_list = NULL;
static struct buffer_head * reuse_list = NULL;
static struct wait_queue * buffer_wait = NULL;

static int nr_buffers = 0;
static int nr_buffers_type[NR_LIST] = {0,};
static int nr_buffer_heads = 0;
static int nr_unused_buffer_heads = 0;

/* This is used by some architectures to estimate available memory. */
int buffermem = 0;

/* Here is the parameter block for the bdflush process. If you add or
 * remove any of the parameters, make sure to update kernel/sysctl.c.
 */

#define N_PARAM 9

/* The dummy values in this structure are left in there for compatibility
 * with old programs that play with the /proc entries.
 */
union bdflush_param{
	struct {
		int nfract;  /* Percentage of buffer cache dirty to 
				activate bdflush */
		int ndirty;  /* Maximum number of dirty blocks to write out per
				wake-cycle */
		int nrefill; /* Number of clean buffers to try to obtain
				each time we call refill */
		int nref_dirt; /* Dirty buffer threshold for activating bdflush
				  when trying to refill buffers. */
		int dummy1;    /* unused */
		int age_buffer;  /* Time for normal buffer to age before 
				    we flush it */
		int age_super;  /* Time for superblock to age before we 
				   flush it */
		int dummy2;    /* unused */
		int dummy3;    /* unused */
	} b_un;
	unsigned int data[N_PARAM];
} bdf_prm = {{40, 500, 64, 256, 15, 30*HZ, 5*HZ, 1884, 2}};

/* These are the min and max parameter values that we will allow to be assigned */
int bdflush_min[N_PARAM] = {  0,  10,    5,   25,  0,   1*HZ,   1*HZ, 1, 1};
int bdflush_max[N_PARAM] = {100,5000, 2000, 2000,100, 600*HZ, 600*HZ, 2047, 5};

void wakeup_bdflush(int);

/*
 * Rewrote the wait-routines to use the "new" wait-queue functionality,
 * and getting rid of the cli-sti pairs. The wait-queue routines still
 * need cli-sti, but now it's just a couple of 386 instructions or so.
 *
 * Note that the real wait_on_buffer() is an inline function that checks
 * if 'b_wait' is set before calling this, so that the queues aren't set
 * up unnecessarily.
 */
void __wait_on_buffer(struct buffer_head * bh)
{
	struct task_struct *tsk = current;
	struct wait_queue wait;

	bh->b_count++;
	wait.task = tsk;
	add_wait_queue(&bh->b_wait, &wait);
repeat:
	tsk->state = TASK_UNINTERRUPTIBLE;
	run_task_queue(&tq_disk);
	if (buffer_locked(bh)) {
		schedule();
		goto repeat;
	}
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&bh->b_wait, &wait);
	bh->b_count--;
}

/* Call sync_buffers with wait!=0 to ensure that the call does not
 * return until all buffer writes have completed.  Sync() may return
 * before the writes have finished; fsync() may not.
 */

/* Godamity-damn.  Some buffers (bitmaps for filesystems)
 * spontaneously dirty themselves without ever brelse being called.
 * We will ultimately want to put these in a separate list, but for
 * now we search all of the lists for dirty buffers.
 */
static int sync_buffers(kdev_t dev, int wait)
{
	int i, retry, pass = 0, err = 0;
	struct buffer_head * bh, *next;

	/* One pass for no-wait, three for wait:
	 * 0) write out all dirty, unlocked buffers;
	 * 1) write out all dirty buffers, waiting if locked;
	 * 2) wait for completion by waiting for all buffers to unlock.
	 */
	do {
		retry = 0;
repeat:
		/* We search all lists as a failsafe mechanism, not because we expect
		 * there to be dirty buffers on any of the other lists.
		 */
		bh = lru_list[BUF_DIRTY];
		if (!bh)
			goto repeat2;
		for (i = nr_buffers_type[BUF_DIRTY]*2 ; i-- > 0 ; bh = next) {
			if (bh->b_list != BUF_DIRTY)
				goto repeat;
			next = bh->b_next_free;
			if (!lru_list[BUF_DIRTY])
				break;
			if (dev && bh->b_dev != dev)
				continue;
			if (buffer_locked(bh)) {
				/* Buffer is locked; skip it unless wait is
				 * requested AND pass > 0.
				 */
				if (!wait || !pass) {
					retry = 1;
					continue;
				}
				wait_on_buffer (bh);
				goto repeat;
			}

			/* If an unlocked buffer is not uptodate, there has
			 * been an IO error. Skip it.
			 */
			if (wait && buffer_req(bh) && !buffer_locked(bh) &&
			    !buffer_dirty(bh) && !buffer_uptodate(bh)) {
				err = -EIO;
				continue;
			}

			/* Don't write clean buffers.  Don't write ANY buffers
			 * on the third pass.
			 */
			if (!buffer_dirty(bh) || pass >= 2)
				continue;

			/* Don't bother about locked buffers.
			 *
			 * XXX We checked if it was locked above and there is no
			 * XXX way we could have slept in between. -DaveM
			 */
			if (buffer_locked(bh))
				continue;
			bh->b_count++;
			next->b_count++;
			bh->b_flushtime = 0;
			ll_rw_block(WRITE, 1, &bh);
			bh->b_count--;
			next->b_count--;
			retry = 1;
		}

    repeat2:
		bh = lru_list[BUF_LOCKED];
		if (!bh)
			break;
		for (i = nr_buffers_type[BUF_LOCKED]*2 ; i-- > 0 ; bh = next) {
			if (bh->b_list != BUF_LOCKED)
				goto repeat2;
			next = bh->b_next_free;
			if (!lru_list[BUF_LOCKED])
				break;
			if (dev && bh->b_dev != dev)
				continue;
			if (buffer_locked(bh)) {
				/* Buffer is locked; skip it unless wait is
				 * requested AND pass > 0.
				 */
				if (!wait || !pass) {
					retry = 1;
					continue;
				}
				wait_on_buffer (bh);
				goto repeat2;
			}
		}

		/* If we are waiting for the sync to succeed, and if any dirty
		 * blocks were written, then repeat; on the second pass, only
		 * wait for buffers being written (do not pass to write any
		 * more buffers on the second pass).
		 */
	} while (wait && retry && ++pass<=2);
	return err;
}

void sync_dev(kdev_t dev)
{
	sync_buffers(dev, 0);
	sync_supers(dev);
	sync_inodes(dev);
	sync_buffers(dev, 0);
	DQUOT_SYNC(dev);
	/*
	 * FIXME(eric) we need to sync the physical devices here.
	 * This is because some (scsi) controllers have huge amounts of
	 * cache onboard (hundreds of Mb), and we need to instruct
	 * them to commit all of the dirty memory to disk, and we should
	 * not return until this has happened.
	 *
	 * This would need to get implemented by going through the assorted
	 * layers so that each block major number can be synced, and this
	 * would call down into the upper and mid-layer scsi.
	 */
}

int fsync_dev(kdev_t dev)
{
	sync_buffers(dev, 0);
	sync_supers(dev);
	sync_inodes(dev);
	DQUOT_SYNC(dev);
	return sync_buffers(dev, 1);
}

asmlinkage int sys_sync(void)
{
	lock_kernel();
	fsync_dev(0);
	unlock_kernel();
	return 0;
}

/*
 *	filp may be NULL if called via the msync of a vma.
 */
 
int file_fsync(struct file *filp, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	struct super_block * sb;
	kdev_t dev;

	/* sync the inode to buffers */
	write_inode_now(inode);

	/* sync the superblock to buffers */
	sb = inode->i_sb;
	wait_on_super(sb);
	if (sb->s_op && sb->s_op->write_super)
		sb->s_op->write_super(sb);

	/* .. finally sync the buffers to disk */
	dev = inode->i_dev;
	return sync_buffers(dev, 1);
}

asmlinkage int sys_fsync(unsigned int fd)
{
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	int err;

	lock_kernel();
	err = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_dentry;
	if (!dentry)
		goto out_putf;

	inode = dentry->d_inode;
	if (!inode)
		goto out_putf;

	err = -EINVAL;
	if (!file->f_op || !file->f_op->fsync)
		goto out_putf;

	/* We need to protect against concurrent writers.. */
	down(&inode->i_sem);
	err = file->f_op->fsync(file, dentry);
	up(&inode->i_sem);

out_putf:
	fput(file);
out:
	unlock_kernel();
	return err;
}

asmlinkage int sys_fdatasync(unsigned int fd)
{
	struct file * file;
	struct dentry * dentry;
	struct inode * inode;
	int err;

	lock_kernel();
	err = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_dentry;
	if (!dentry)
		goto out_putf;

	inode = dentry->d_inode;
	if (!inode)
		goto out_putf;

	err = -EINVAL;
	if (!file->f_op || !file->f_op->fsync)
		goto out_putf;

	/* this needs further work, at the moment it is identical to fsync() */
	err = file->f_op->fsync(file, dentry);

out_putf:
	fput(file);
out:
	unlock_kernel();
	return err;
}

void invalidate_buffers(kdev_t dev)
{
	int i;
	int nlist;
	struct buffer_head * bh;

	for(nlist = 0; nlist < NR_LIST; nlist++) {
		bh = lru_list[nlist];
		for (i = nr_buffers_type[nlist]*2 ; --i > 0 ; bh = bh->b_next_free) {
			if (bh->b_dev != dev)
				continue;
			wait_on_buffer(bh);
			if (bh->b_dev != dev)
				continue;
			if (bh->b_count)
				continue;
			bh->b_flushtime = 0;
			clear_bit(BH_Protected, &bh->b_state);
			clear_bit(BH_Uptodate, &bh->b_state);
			clear_bit(BH_Dirty, &bh->b_state);
			clear_bit(BH_Req, &bh->b_state);
		}
	}
}

#define _hashfn(dev,block) (((unsigned)(HASHDEV(dev)^block)) & bh_hash_mask)
#define hash(dev,block) hash_table[_hashfn(dev,block)]

static inline void remove_from_hash_queue(struct buffer_head * bh)
{
	struct buffer_head **pprev = bh->b_pprev;
	if (pprev) {
		struct buffer_head * next = bh->b_next;
		if (next) {
			next->b_pprev = pprev;
			bh->b_next = NULL;
		}
		*pprev = next;
		bh->b_pprev = NULL;
	}
}

static inline void remove_from_lru_list(struct buffer_head * bh)
{
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("VFS: LRU block list corrupted");
	if (bh->b_dev == B_FREE)
		panic("LRU list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;

	if (lru_list[bh->b_list] == bh)
		 lru_list[bh->b_list] = bh->b_next_free;
	if (lru_list[bh->b_list] == bh)
		 lru_list[bh->b_list] = NULL;
	bh->b_next_free = bh->b_prev_free = NULL;
}

static inline void remove_from_free_list(struct buffer_head * bh)
{
	int isize = BUFSIZE_INDEX(bh->b_size);
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("VFS: Free block list corrupted");
	if(bh->b_dev != B_FREE)
		panic("Free list corrupted");
	if(!free_list[isize])
		panic("Free list empty");
	if(bh->b_next_free == bh)
		 free_list[isize] = NULL;
	else {
		bh->b_prev_free->b_next_free = bh->b_next_free;
		bh->b_next_free->b_prev_free = bh->b_prev_free;
		if (free_list[isize] == bh)
			 free_list[isize] = bh->b_next_free;
	}
	bh->b_next_free = bh->b_prev_free = NULL;
}

static void remove_from_queues(struct buffer_head * bh)
{
	if(bh->b_dev == B_FREE) {
		remove_from_free_list(bh); /* Free list entries should not be
					      in the hash queue */
		return;
	}
	nr_buffers_type[bh->b_list]--;
	remove_from_hash_queue(bh);
	remove_from_lru_list(bh);
}

static inline void put_last_lru(struct buffer_head * bh)
{
	if (bh) {
		struct buffer_head **bhp = &lru_list[bh->b_list];

		if (bh == *bhp) {
			*bhp = bh->b_next_free;
			return;
		}

		if(bh->b_dev == B_FREE)
			panic("Wrong block for lru list");

		/* Add to back of free list. */
		remove_from_lru_list(bh);
		if(!*bhp) {
			*bhp = bh;
			(*bhp)->b_prev_free = bh;
		}

		bh->b_next_free = *bhp;
		bh->b_prev_free = (*bhp)->b_prev_free;
		(*bhp)->b_prev_free->b_next_free = bh;
		(*bhp)->b_prev_free = bh;
	}
}

static inline void put_last_free(struct buffer_head * bh)
{
	if (bh) {
		struct buffer_head **bhp = &free_list[BUFSIZE_INDEX(bh->b_size)];

		bh->b_dev = B_FREE;  /* So it is obvious we are on the free list. */

		/* Add to back of free list. */
		if(!*bhp) {
			*bhp = bh;
			bh->b_prev_free = bh;
		}

		bh->b_next_free = *bhp;
		bh->b_prev_free = (*bhp)->b_prev_free;
		(*bhp)->b_prev_free->b_next_free = bh;
		(*bhp)->b_prev_free = bh;
	}
}

static void insert_into_queues(struct buffer_head * bh)
{
	/* put at end of free list */
	if(bh->b_dev == B_FREE) {
		put_last_free(bh);
	} else {
		struct buffer_head **bhp = &lru_list[bh->b_list];

		if(!*bhp) {
			*bhp = bh;
			bh->b_prev_free = bh;
		}

		if (bh->b_next_free)
			panic("VFS: buffer LRU pointers corrupted");

		bh->b_next_free = *bhp;
		bh->b_prev_free = (*bhp)->b_prev_free;
		(*bhp)->b_prev_free->b_next_free = bh;
		(*bhp)->b_prev_free = bh;

		nr_buffers_type[bh->b_list]++;

		/* Put the buffer in new hash-queue if it has a device. */
		bh->b_next = NULL;
		bh->b_pprev = NULL;
		if (bh->b_dev) {
			struct buffer_head **bhp = &hash(bh->b_dev, bh->b_blocknr);
			struct buffer_head *next = *bhp;

			if (next) {
				bh->b_next = next;
				next->b_pprev = &bh->b_next;
			}
			*bhp = bh;
			bh->b_pprev = bhp;
		}
	}
}

struct buffer_head * find_buffer(kdev_t dev, int block, int size)
{		
	struct buffer_head * next;

	next = hash(dev,block);
	for (;;) {
		struct buffer_head *tmp = next;
		if (!next)
			break;
		next = tmp->b_next;
		if (tmp->b_blocknr != block || tmp->b_size != size || tmp->b_dev != dev)
			continue;
		next = tmp;
		break;
	}
	return next;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are reading them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
struct buffer_head * get_hash_table(kdev_t dev, int block, int size)
{
	struct buffer_head * bh;
	bh = find_buffer(dev,block,size);
	if (bh)
		bh->b_count++;
	return bh;
}

unsigned int get_hardblocksize(kdev_t dev)
{
	/*
	 * Get the hard sector size for the given device.  If we don't know
	 * what it is, return 0.
	 */
	if (hardsect_size[MAJOR(dev)] != NULL) {
		int blksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
		if (blksize != 0)
			return blksize;
	}

	/*
	 * We don't know what the hardware sector size for this device is.
	 * Return 0 indicating that we don't know.
	 */
	return 0;
}

void set_blocksize(kdev_t dev, int size)
{
	extern int *blksize_size[];
	int i, nlist;
	struct buffer_head * bh, *bhnext;

	if (!blksize_size[MAJOR(dev)])
		return;

	/* Size must be a power of two, and between 512 and PAGE_SIZE */
	if (size > PAGE_SIZE || size < 512 || (size & (size-1)))
		panic("Invalid blocksize passed to set_blocksize");

	if (blksize_size[MAJOR(dev)][MINOR(dev)] == 0 && size == BLOCK_SIZE) {
		blksize_size[MAJOR(dev)][MINOR(dev)] = size;
		return;
	}
	if (blksize_size[MAJOR(dev)][MINOR(dev)] == size)
		return;
	sync_buffers(dev, 2);
	blksize_size[MAJOR(dev)][MINOR(dev)] = size;

	/* We need to be quite careful how we do this - we are moving entries
	 * around on the free list, and we can get in a loop if we are not careful.
	 */
	for(nlist = 0; nlist < NR_LIST; nlist++) {
		bh = lru_list[nlist];
		for (i = nr_buffers_type[nlist]*2 ; --i > 0 ; bh = bhnext) {
			if(!bh)
				break;

			bhnext = bh->b_next_free; 
			if (bh->b_dev != dev)
				 continue;
			if (bh->b_size == size)
				 continue;
			bhnext->b_count++;
			wait_on_buffer(bh);
			bhnext->b_count--;
			if (bh->b_dev == dev && bh->b_size != size) {
				clear_bit(BH_Dirty, &bh->b_state);
				clear_bit(BH_Uptodate, &bh->b_state);
				clear_bit(BH_Req, &bh->b_state);
				bh->b_flushtime = 0;
			}
			remove_from_hash_queue(bh);
		}
	}
}

/*
 * We used to try various strange things. Let's not.
 */
static void refill_freelist(int size)
{
	if (!grow_buffers(size)) {
		wakeup_bdflush(1);
		current->policy |= SCHED_YIELD;
		schedule();
	}
}

void init_buffer(struct buffer_head *bh, kdev_t dev, int block,
		 bh_end_io_t *handler, void *dev_id)
{
	bh->b_count = 1;
	bh->b_list = BUF_CLEAN;
	bh->b_flushtime = 0;
	bh->b_dev = dev;
	bh->b_blocknr = block;
	bh->b_end_io = handler;
	bh->b_dev_id = dev_id;
}

static void end_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	mark_buffer_uptodate(bh, uptodate);
	unlock_buffer(bh);
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algorithm is changed: hopefully better, and an elusive bug removed.
 *
 * 14.02.92: changed it to sync dirty buffers a bit: better performance
 * when the filesystem starts to get full of dirty blocks (I hope).
 */
struct buffer_head * getblk(kdev_t dev, int block, int size)
{
	struct buffer_head * bh;
	int isize;

repeat:
	bh = get_hash_table(dev, block, size);
	if (bh) {
		if (!buffer_dirty(bh)) {
			if (buffer_uptodate(bh))
				 put_last_lru(bh);
			bh->b_flushtime = 0;
		}
		return bh;
	}

	isize = BUFSIZE_INDEX(size);
get_free:
	bh = free_list[isize];
	if (!bh)
		goto refill;
	remove_from_free_list(bh);

	/* OK, FINALLY we know that this buffer is the only one of its kind,
	 * and that it's unused (b_count=0), unlocked, and clean.
	 */
	init_buffer(bh, dev, block, end_buffer_io_sync, NULL);
	bh->b_state=0;
	insert_into_queues(bh);
	return bh;

	/*
	 * If we block while refilling the free list, somebody may
	 * create the buffer first ... search the hashes again.
	 */
refill:
	refill_freelist(size);
	if (!find_buffer(dev,block,size))
		goto get_free;
	goto repeat;
}

void set_writetime(struct buffer_head * buf, int flag)
{
	int newtime;

	if (buffer_dirty(buf)) {
		/* Move buffer to dirty list if jiffies is clear. */
		newtime = jiffies + (flag ? bdf_prm.b_un.age_super : 
				     bdf_prm.b_un.age_buffer);
		if(!buf->b_flushtime || buf->b_flushtime > newtime)
			 buf->b_flushtime = newtime;
	} else {
		buf->b_flushtime = 0;
	}
}


/*
 * Put a buffer into the appropriate list, without side-effects.
 */
static inline void file_buffer(struct buffer_head *bh, int list)
{
	remove_from_queues(bh);
	bh->b_list = list;
	insert_into_queues(bh);
}

/*
 * A buffer may need to be moved from one buffer list to another
 * (e.g. in case it is not shared any more). Handle this.
 */
void refile_buffer(struct buffer_head * buf)
{
	int dispose;

	if(buf->b_dev == B_FREE) {
		printk("Attempt to refile free buffer\n");
		return;
	}
	if (buffer_dirty(buf))
		dispose = BUF_DIRTY;
	else if (buffer_locked(buf))
		dispose = BUF_LOCKED;
	else
		dispose = BUF_CLEAN;
	if(dispose != buf->b_list) {
		file_buffer(buf, dispose);
		if(dispose == BUF_DIRTY) {
			int too_many = (nr_buffers * bdf_prm.b_un.nfract/100);

			/* This buffer is dirty, maybe we need to start flushing.
			 * If too high a percentage of the buffers are dirty...
			 */
			if (nr_buffers_type[BUF_DIRTY] > too_many)
				wakeup_bdflush(0);

			/* If this is a loop device, and
			 * more than half of the buffers are dirty...
			 * (Prevents no-free-buffers deadlock with loop device.)
			 */
			if (MAJOR(buf->b_dev) == LOOP_MAJOR &&
			    nr_buffers_type[BUF_DIRTY]*2>nr_buffers)
				wakeup_bdflush(1);
		}
	}
}

/*
 * Release a buffer head
 */
void __brelse(struct buffer_head * buf)
{
	/* If dirty, mark the time this buffer should be written back. */
	set_writetime(buf, 0);
	refile_buffer(buf);

	if (buf->b_count) {
		buf->b_count--;
		return;
	}
	printk("VFS: brelse: Trying to free free buffer\n");
}

/*
 * bforget() is like brelse(), except it removes the buffer
 * from the hash-queues (so that it won't be re-used if it's
 * shared).
 */
void __bforget(struct buffer_head * buf)
{
	mark_buffer_clean(buf);
	clear_bit(BH_Protected, &buf->b_state);
	remove_from_hash_queue(buf);
	buf->b_dev = NODEV;
	refile_buffer(buf);
	if (!--buf->b_count)
		return;
	printk("VFS: forgot an in-use buffer! (count=%d)\n",
		buf->b_count);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */
struct buffer_head * bread(kdev_t dev, int block, int size)
{
	struct buffer_head * bh = getblk(dev, block, size);

	if (bh) {
		touch_buffer(bh);
		if (buffer_uptodate(bh))
			return bh;
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		if (buffer_uptodate(bh))
			return bh;
		brelse(bh);
		return NULL;
	}
	printk("VFS: bread: impossible error\n");
	return NULL;
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */

#define NBUF 16

struct buffer_head * breada(kdev_t dev, int block, int bufsize,
	unsigned int pos, unsigned int filesize)
{
	struct buffer_head * bhlist[NBUF];
	unsigned int blocks;
	struct buffer_head * bh;
	int index;
	int i, j;

	if (pos >= filesize)
		return NULL;

	if (block < 0 || !(bh = getblk(dev,block,bufsize)))
		return NULL;

	index = BUFSIZE_INDEX(bh->b_size);

	touch_buffer(bh);
	if (buffer_uptodate(bh))
		return(bh);   
	else ll_rw_block(READ, 1, &bh);

	blocks = (filesize - pos) >> (9+index);

	if (blocks < (read_ahead[MAJOR(dev)] >> index))
		blocks = read_ahead[MAJOR(dev)] >> index;
	if (blocks > NBUF) 
		blocks = NBUF;

/*	if (blocks) printk("breada (new) %d blocks\n",blocks); */


	bhlist[0] = bh;
	j = 1;
	for(i=1; i<blocks; i++) {
		bh = getblk(dev,block+i,bufsize);
		if (buffer_uptodate(bh)) {
			brelse(bh);
			break;
		}
		else bhlist[j++] = bh;
	}

	/* Request the read for these buffers, and then release them. */
	if (j>1)  
		ll_rw_block(READA, (j-1), bhlist+1); 
	for(i=1; i<j; i++)
		brelse(bhlist[i]);

	/* Wait for this buffer, and then continue on. */
	bh = bhlist[0];
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	return NULL;
}

/*
 * Note: the caller should wake up the buffer_wait list if needed.
 */
static void put_unused_buffer_head(struct buffer_head * bh)
{
	if (nr_unused_buffer_heads >= MAX_UNUSED_BUFFERS) {
		nr_buffer_heads--;
		kmem_cache_free(bh_cachep, bh);
		return;
	}

	memset(bh,0,sizeof(*bh));
	nr_unused_buffer_heads++;
	bh->b_next_free = unused_list;
	unused_list = bh;
}

/* 
 * We can't put completed temporary IO buffer_heads directly onto the
 * unused_list when they become unlocked, since the device driver
 * end_request routines still expect access to the buffer_head's
 * fields after the final unlock.  So, the device driver puts them on
 * the reuse_list instead once IO completes, and we recover these to
 * the unused_list here.
 *
 * Note that we don't do a wakeup here, but return a flag indicating
 * whether we got any buffer heads. A task ready to sleep can check
 * the returned value, and any tasks already sleeping will have been
 * awakened when the buffer heads were added to the reuse list.
 */
static inline int recover_reusable_buffer_heads(void)
{
	struct buffer_head *head = xchg(&reuse_list, NULL);
	int found = 0;
	
	if (head) {
		do {
			struct buffer_head *bh = head;
			head = head->b_next_free;
			put_unused_buffer_head(bh);
		} while (head);
		found = 1;
	}
	return found;
}

/*
 * Reserve NR_RESERVED buffer heads for async IO requests to avoid
 * no-buffer-head deadlock.  Return NULL on failure; waiting for
 * buffer heads is now handled in create_buffers().
 */ 
static struct buffer_head * get_unused_buffer_head(int async)
{
	struct buffer_head * bh;

	recover_reusable_buffer_heads();
	if (nr_unused_buffer_heads > NR_RESERVED) {
		bh = unused_list;
		unused_list = bh->b_next_free;
		nr_unused_buffer_heads--;
		return bh;
	}

	/* This is critical.  We can't swap out pages to get
	 * more buffer heads, because the swap-out may need
	 * more buffer-heads itself.  Thus SLAB_ATOMIC.
	 */
	if((bh = kmem_cache_alloc(bh_cachep, SLAB_ATOMIC)) != NULL) {
		memset(bh, 0, sizeof(*bh));
		nr_buffer_heads++;
		return bh;
	}

	/*
	 * If we need an async buffer, use the reserved buffer heads.
	 */
	if (async && unused_list) {
		bh = unused_list;
		unused_list = bh->b_next_free;
		nr_unused_buffer_heads--;
		return bh;
	}

#if 0
	/*
	 * (Pending further analysis ...)
	 * Ordinary (non-async) requests can use a different memory priority
	 * to free up pages. Any swapping thus generated will use async
	 * buffer heads.
	 */
	if(!async &&
	   (bh = kmem_cache_alloc(bh_cachep, SLAB_KERNEL)) != NULL) {
		memset(bh, 0, sizeof(*bh));
		nr_buffer_heads++;
		return bh;
	}
#endif

	return NULL;
}

/*
 * Create the appropriate buffers when given a page for data area and
 * the size of each buffer.. Use the bh->b_this_page linked list to
 * follow the buffers created.  Return NULL if unable to create more
 * buffers.
 * The async flag is used to differentiate async IO (paging, swapping)
 * from ordinary buffer allocations, and only async requests are allowed
 * to sleep waiting for buffer heads. 
 */
static struct buffer_head * create_buffers(unsigned long page, 
						unsigned long size, int async)
{
	struct wait_queue wait = { current, NULL };
	struct buffer_head *bh, *head;
	long offset;

try_again:
	head = NULL;
	offset = PAGE_SIZE;
	while ((offset -= size) >= 0) {
		bh = get_unused_buffer_head(async);
		if (!bh)
			goto no_grow;

		bh->b_dev = B_FREE;  /* Flag as unused */
		bh->b_this_page = head;
		head = bh;

		bh->b_state = 0;
		bh->b_next_free = NULL;
		bh->b_count = 0;
		bh->b_size = size;

		bh->b_data = (char *) (page+offset);
		bh->b_list = 0;
	}
	return head;
/*
 * In case anything failed, we just free everything we got.
 */
no_grow:
	if (head) {
		do {
			bh = head;
			head = head->b_this_page;
			put_unused_buffer_head(bh);
		} while (head);

		/* Wake up any waiters ... */
		wake_up(&buffer_wait);
	}

	/*
	 * Return failure for non-async IO requests.  Async IO requests
	 * are not allowed to fail, so we have to wait until buffer heads
	 * become available.  But we don't want tasks sleeping with 
	 * partially complete buffers, so all were released above.
	 */
	if (!async)
		return NULL;

	/* We're _really_ low on memory. Now we just
	 * wait for old buffer heads to become free due to
	 * finishing IO.  Since this is an async request and
	 * the reserve list is empty, we're sure there are 
	 * async buffer heads in use.
	 */
	run_task_queue(&tq_disk);

	/* 
	 * Set our state for sleeping, then check again for buffer heads.
	 * This ensures we won't miss a wake_up from an interrupt.
	 */
	add_wait_queue(&buffer_wait, &wait);
	current->state = TASK_UNINTERRUPTIBLE;
	if (!recover_reusable_buffer_heads())
		schedule();
	remove_wait_queue(&buffer_wait, &wait);
	current->state = TASK_RUNNING;
	goto try_again;
}

/* Run the hooks that have to be done when a page I/O has completed. */
static inline void after_unlock_page (struct page * page)
{
	if (test_and_clear_bit(PG_decr_after, &page->flags)) {
		atomic_dec(&nr_async_pages);
#ifdef DEBUG_SWAP
		printk ("DebugVM: Finished IO on page %p, nr_async_pages %d\n",
			(char *) page_address(page), 
			atomic_read(&nr_async_pages));
#endif
	}
	if (test_and_clear_bit(PG_swap_unlock_after, &page->flags))
		swap_after_unlock_page(page->offset);
	if (test_and_clear_bit(PG_free_after, &page->flags))
		__free_page(page);
}

/*
 * Free all temporary buffers belonging to a page.
 * This needs to be called with interrupts disabled.
 */
static inline void free_async_buffers (struct buffer_head * bh)
{
	struct buffer_head *tmp, *tail;

	/*
	 * Link all the buffers into the b_next_free list,
	 * so we only have to do one xchg() operation ...
	 */
	tail = bh;
	while ((tmp = tail->b_this_page) != bh) {
		tail->b_next_free = tmp;
		tail = tmp;
	};

	/* Update the reuse list */
	tail->b_next_free = xchg(&reuse_list, NULL);
	reuse_list = bh;

	/* Wake up any waiters ... */
	wake_up(&buffer_wait);
}

static void end_buffer_io_async(struct buffer_head * bh, int uptodate)
{
	unsigned long flags;
	struct buffer_head *tmp;
	struct page *page;

	mark_buffer_uptodate(bh, uptodate);
	unlock_buffer(bh);

	/* This is a temporary buffer used for page I/O. */
	page = mem_map + MAP_NR(bh->b_data);
	if (!PageLocked(page))
		goto not_locked;
	if (bh->b_count != 1)
		goto bad_count;

	if (!test_bit(BH_Uptodate, &bh->b_state))
		set_bit(PG_error, &page->flags);

	/*
	 * Be _very_ careful from here on. Bad things can happen if
	 * two buffer heads end IO at almost the same time and both
	 * decide that the page is now completely done.
	 *
	 * Async buffer_heads are here only as labels for IO, and get
	 * thrown away once the IO for this page is complete.  IO is
	 * deemed complete once all buffers have been visited
	 * (b_count==0) and are now unlocked. We must make sure that
	 * only the _last_ buffer that decrements its count is the one
	 * that free's the page..
	 */
	save_flags(flags);
	cli();
	bh->b_count--;
	tmp = bh;
	do {
		if (tmp->b_count)
			goto still_busy;
		tmp = tmp->b_this_page;
	} while (tmp != bh);

	/* OK, the async IO on this page is complete. */
	free_async_buffers(bh);
	restore_flags(flags);
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	after_unlock_page(page);
	return;

still_busy:
	restore_flags(flags);
	return;

not_locked:
	printk ("Whoops: end_buffer_io_async: async io complete on unlocked page\n");
	return;

bad_count:
	printk ("Whoops: end_buffer_io_async: b_count != 1 on async io.\n");
	return;
}

/*
 * Start I/O on a page.
 * This function expects the page to be locked and may return before I/O is complete.
 * You then have to check page->locked, page->uptodate, and maybe wait on page->wait.
 */
int brw_page(int rw, struct page *page, kdev_t dev, int b[], int size, int bmap)
{
	struct buffer_head *bh, *prev, *next, *arr[MAX_BUF_PER_PAGE];
	int block, nr;

	if (!PageLocked(page))
		panic("brw_page: page not locked for I/O");
	clear_bit(PG_uptodate, &page->flags);
	clear_bit(PG_error, &page->flags);
	/*
	 * Allocate async buffer heads pointing to this page, just for I/O.
	 * They do _not_ show up in the buffer hash table!
	 * They are _not_ registered in page->buffers either!
	 */
	bh = create_buffers(page_address(page), size, 1);
	if (!bh) {
		/* WSH: exit here leaves page->count incremented */
		clear_bit(PG_locked, &page->flags);
		wake_up(&page->wait);
		return -ENOMEM;
	}
	nr = 0;
	next = bh;
	do {
		struct buffer_head * tmp;
		block = *(b++);

		init_buffer(next, dev, block, end_buffer_io_async, NULL);
		set_bit(BH_Uptodate, &next->b_state);

		/*
		 * When we use bmap, we define block zero to represent
		 * a hole.  ll_rw_page, however, may legitimately
		 * access block zero, and we need to distinguish the
		 * two cases.
		 */
		if (bmap && !block) {
			memset(next->b_data, 0, size);
			next->b_count--;
			continue;
		}
		tmp = get_hash_table(dev, block, size);
		if (tmp) {
			if (!buffer_uptodate(tmp)) {
				if (rw == READ)
					ll_rw_block(READ, 1, &tmp);
				wait_on_buffer(tmp);
			}
			if (rw == READ) 
				memcpy(next->b_data, tmp->b_data, size);
			else {
				memcpy(tmp->b_data, next->b_data, size);
				mark_buffer_dirty(tmp, 0);
			}
			brelse(tmp);
			next->b_count--;
			continue;
		}
		if (rw == READ)
			clear_bit(BH_Uptodate, &next->b_state);
		else
			set_bit(BH_Dirty, &next->b_state);
		arr[nr++] = next;
	} while (prev = next, (next = next->b_this_page) != NULL);
	prev->b_this_page = bh;
	
	if (nr) {
		ll_rw_block(rw, nr, arr);
		/* The rest of the work is done in mark_buffer_uptodate()
		 * and unlock_buffer(). */
	} else {
		unsigned long flags;
		clear_bit(PG_locked, &page->flags);
		set_bit(PG_uptodate, &page->flags);
		wake_up(&page->wait);
		save_flags(flags);
		cli();
		free_async_buffers(bh);
		restore_flags(flags);
		after_unlock_page(page);
	}
	++current->maj_flt;
	return 0;
}

/*
 * This is called by end_request() when I/O has completed.
 */
void mark_buffer_uptodate(struct buffer_head * bh, int on)
{
	if (on) {
		struct buffer_head *tmp = bh;
		set_bit(BH_Uptodate, &bh->b_state);
		/* If a page has buffers and all these buffers are uptodate,
		 * then the page is uptodate. */
		do {
			if (!test_bit(BH_Uptodate, &tmp->b_state))
				return;
			tmp=tmp->b_this_page;
		} while (tmp && tmp != bh);
		set_bit(PG_uptodate, &mem_map[MAP_NR(bh->b_data)].flags);
		return;
	}
	clear_bit(BH_Uptodate, &bh->b_state);
}

/*
 * Generic "readpage" function for block devices that have the normal
 * bmap functionality. This is most of the block device filesystems.
 * Reads the page asynchronously --- the unlock_buffer() and
 * mark_buffer_uptodate() functions propagate buffer state into the
 * page struct once IO has completed.
 */
int generic_readpage(struct file * file, struct page * page)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	unsigned long block;
	int *p, nr[PAGE_SIZE/512];
	int i;

	atomic_inc(&page->count);
	set_bit(PG_locked, &page->flags);
	set_bit(PG_free_after, &page->flags);
	
	i = PAGE_SIZE >> inode->i_sb->s_blocksize_bits;
	block = page->offset >> inode->i_sb->s_blocksize_bits;
	p = nr;
	do {
		*p = inode->i_op->bmap(inode, block);
		i--;
		block++;
		p++;
	} while (i > 0);

	/* IO start */
	brw_page(READ, page, inode->i_dev, nr, inode->i_sb->s_blocksize, 1);
	return 0;
}

/*
 * Try to increase the number of buffers available: the size argument
 * is used to determine what kind of buffers we want.
 */
static int grow_buffers(int size)
{
	unsigned long page;
	struct buffer_head *bh, *tmp;
	struct buffer_head * insert_point;
	int isize;

	if ((size & 511) || (size > PAGE_SIZE)) {
		printk("VFS: grow_buffers: size = %d\n",size);
		return 0;
	}

	if (!(page = __get_free_page(GFP_BUFFER)))
		return 0;
	bh = create_buffers(page, size, 0);
	if (!bh) {
		free_page(page);
		return 0;
	}

	isize = BUFSIZE_INDEX(size);
	insert_point = free_list[isize];

	tmp = bh;
	while (1) {
		if (insert_point) {
			tmp->b_next_free = insert_point->b_next_free;
			tmp->b_prev_free = insert_point;
			insert_point->b_next_free->b_prev_free = tmp;
			insert_point->b_next_free = tmp;
		} else {
			tmp->b_prev_free = tmp;
			tmp->b_next_free = tmp;
		}
		insert_point = tmp;
		++nr_buffers;
		if (tmp->b_this_page)
			tmp = tmp->b_this_page;
		else
			break;
	}
	tmp->b_this_page = bh;
	free_list[isize] = bh;
	mem_map[MAP_NR(page)].buffers = bh;
	buffermem += PAGE_SIZE;
	return 1;
}

/*
 * Can the buffer be thrown out?
 */
#define BUFFER_BUSY_BITS	((1<<BH_Dirty) | (1<<BH_Lock) | (1<<BH_Protected))
#define buffer_busy(bh)		((bh)->b_count || ((bh)->b_state & BUFFER_BUSY_BITS))

/*
 * try_to_free_buffers() checks if all the buffers on this particular page
 * are unused, and free's the page if so.
 *
 * Wake up bdflush() if this fails - if we're running low on memory due
 * to dirty buffers, we need to flush them out as quickly as possible.
 */
int try_to_free_buffers(struct page * page_map)
{
	struct buffer_head * tmp, * bh = page_map->buffers;

	tmp = bh;
	do {
		struct buffer_head * p = tmp;

		tmp = tmp->b_this_page;
		if (!buffer_busy(p))
			continue;

		wakeup_bdflush(0);
		return 0;
	} while (tmp != bh);

	tmp = bh;
	do {
		struct buffer_head * p = tmp;
		tmp = tmp->b_this_page;
		nr_buffers--;
		remove_from_queues(p);
		put_unused_buffer_head(p);
	} while (tmp != bh);

	/* Wake up anyone waiting for buffer heads */
	wake_up(&buffer_wait);

	/* And free the page */
	buffermem -= PAGE_SIZE;
	page_map->buffers = NULL;
	__free_page(page_map);
	return 1;
}

/* ================== Debugging =================== */

void show_buffers(void)
{
	struct buffer_head * bh;
	int found = 0, locked = 0, dirty = 0, used = 0, lastused = 0;
	int protected = 0;
	int nlist;
	static char *buf_types[NR_LIST] = {"CLEAN","LOCKED","DIRTY"};

	printk("Buffer memory:   %6dkB\n",buffermem>>10);
	printk("Buffer heads:    %6d\n",nr_buffer_heads);
	printk("Buffer blocks:   %6d\n",nr_buffers);

	for(nlist = 0; nlist < NR_LIST; nlist++) {
	  found = locked = dirty = used = lastused = protected = 0;
	  bh = lru_list[nlist];
	  if(!bh) continue;

	  do {
		found++;
		if (buffer_locked(bh))
			locked++;
		if (buffer_protected(bh))
			protected++;
		if (buffer_dirty(bh))
			dirty++;
		if (bh->b_count)
			used++, lastused = found;
		bh = bh->b_next_free;
	  } while (bh != lru_list[nlist]);
	  printk("%8s: %d buffers, %d used (last=%d), "
		 "%d locked, %d protected, %d dirty\n",
		 buf_types[nlist], found, used, lastused,
		 locked, protected, dirty);
	};
}


/* ===================== Init ======================= */

/*
 * allocate the hash table and init the free list
 * Use gfp() for the hash table to decrease TLB misses, use
 * SLAB cache for buffer heads.
 */
void __init buffer_init(void)
{
	int order = 5;		/* Currently maximum order.. */
	unsigned int nr_hash;

	nr_hash = (1UL << order) * PAGE_SIZE / sizeof(struct buffer_head *);
	hash_table = (struct buffer_head **) __get_free_pages(GFP_ATOMIC, order);
	
	if (!hash_table)
		panic("Failed to allocate buffer hash table\n");
	memset(hash_table, 0, nr_hash * sizeof(struct buffer_head *));
	bh_hash_mask = nr_hash-1;

	bh_cachep = kmem_cache_create("buffer_head",
				      sizeof(struct buffer_head),
				      0,
				      SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!bh_cachep)
		panic("Cannot create buffer head SLAB cache\n");
	/*
	 * Allocate the reserved buffer heads.
	 */
	while (nr_buffer_heads < NR_RESERVED) {
		struct buffer_head * bh;

		bh = kmem_cache_alloc(bh_cachep, SLAB_ATOMIC);
		if (!bh)
			break;
		put_unused_buffer_head(bh);
		nr_buffer_heads++;
	}

	lru_list[BUF_CLEAN] = 0;
	grow_buffers(BLOCK_SIZE);
}


/* ====================== bdflush support =================== */

/* This is a simple kernel daemon, whose job it is to provide a dynamic
 * response to dirty buffers.  Once this process is activated, we write back
 * a limited number of buffers to the disks and then go back to sleep again.
 */
static struct wait_queue * bdflush_wait = NULL;
static struct wait_queue * bdflush_done = NULL;
struct task_struct *bdflush_tsk = 0;

void wakeup_bdflush(int wait)
{
	if (current == bdflush_tsk)
		return;
	wake_up(&bdflush_wait);
	if (wait) {
		run_task_queue(&tq_disk);
		sleep_on(&bdflush_done);
	}
}


/* 
 * Here we attempt to write back old buffers.  We also try to flush inodes 
 * and supers as well, since this function is essentially "update", and 
 * otherwise there would be no way of ensuring that these quantities ever 
 * get written back.  Ideally, we would have a timestamp on the inodes
 * and superblocks so that we could write back only the old ones as well
 */

static int sync_old_buffers(void)
{
	int i;
	int ndirty, nwritten;
	int nlist;
	int ncount;
	struct buffer_head * bh, *next;

	sync_supers(0);
	sync_inodes(0);

	ncount = 0;
#ifdef DEBUG
	for(nlist = 0; nlist < NR_LIST; nlist++)
#else
	for(nlist = BUF_DIRTY; nlist <= BUF_DIRTY; nlist++)
#endif
	{
		ndirty = 0;
		nwritten = 0;
	repeat:

		bh = lru_list[nlist];
		if(bh) 
			 for (i = nr_buffers_type[nlist]; i-- > 0; bh = next) {
				 /* We may have stalled while waiting for I/O to complete. */
				 if(bh->b_list != nlist) goto repeat;
				 next = bh->b_next_free;
				 if(!lru_list[nlist]) {
					 printk("Dirty list empty %d\n", i);
					 break;
				 }
				 
				 /* Clean buffer on dirty list?  Refile it */
				 if (nlist == BUF_DIRTY && !buffer_dirty(bh) && !buffer_locked(bh))
				  {
					  refile_buffer(bh);
					  continue;
				  }
				 
				 if (buffer_locked(bh) || !buffer_dirty(bh))
					  continue;
				 ndirty++;
				 if(time_before(jiffies, bh->b_flushtime))
					continue;
				 nwritten++;
				 next->b_count++;
				 bh->b_count++;
				 bh->b_flushtime = 0;
#ifdef DEBUG
				 if(nlist != BUF_DIRTY) ncount++;
#endif
				 ll_rw_block(WRITE, 1, &bh);
				 bh->b_count--;
				 next->b_count--;
			 }
	}
	run_task_queue(&tq_disk);
#ifdef DEBUG
	if (ncount) printk("sync_old_buffers: %d dirty buffers not on dirty list\n", ncount);
	printk("Wrote %d/%d buffers\n", nwritten, ndirty);
#endif
	run_task_queue(&tq_disk);
	return 0;
}


/* This is the interface to bdflush.  As we get more sophisticated, we can
 * pass tuning parameters to this "process", to adjust how it behaves. 
 * We would want to verify each parameter, however, to make sure that it 
 * is reasonable. */

asmlinkage int sys_bdflush(int func, long data)
{
	int i, error = -EPERM;

	lock_kernel();
	if (!capable(CAP_SYS_ADMIN))
		goto out;

	if (func == 1) {
		 error = sync_old_buffers();
		 goto out;
	}

	/* Basically func 1 means read param 1, 2 means write param 1, etc */
	if (func >= 2) {
		i = (func-2) >> 1;
		error = -EINVAL;
		if (i < 0 || i >= N_PARAM)
			goto out;
		if((func & 1) == 0) {
			error = put_user(bdf_prm.data[i], (int*)data);
			goto out;
		}
		if (data < bdflush_min[i] || data > bdflush_max[i])
			goto out;
		bdf_prm.data[i] = data;
		error = 0;
		goto out;
	};

	/* Having func 0 used to launch the actual bdflush and then never
	 * return (unless explicitly killed). We return zero here to 
	 * remain semi-compatible with present update(8) programs.
	 */
	error = 0;
out:
	unlock_kernel();
	return error;
}

/* This is the actual bdflush daemon itself. It used to be started from
 * the syscall above, but now we launch it ourselves internally with
 * kernel_thread(...)  directly after the first thread in init/main.c */

/* To prevent deadlocks for a loop device:
 * 1) Do non-blocking writes to loop (avoids deadlock with running
 *	out of request blocks).
 * 2) But do a blocking write if the only dirty buffers are loop buffers
 *	(otherwise we go into an infinite busy-loop).
 * 3) Quit writing loop blocks if a freelist went low (avoids deadlock
 *	with running out of free buffers for loop's "real" device).
*/
int bdflush(void * unused) 
{
	int i;
	int ndirty;
	int nlist;
	int ncount;
	struct buffer_head * bh, *next;
	int major;
	int wrta_cmd = WRITEA;	/* non-blocking write for LOOP */

	/*
	 *	We have a bare-bones task_struct, and really should fill
	 *	in a few more things so "top" and /proc/2/{exe,root,cwd}
	 *	display semi-sane things. Not real crucial though...  
	 */

	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "kflushd");
	bdflush_tsk = current;

	/*
	 *	As a kernel thread we want to tamper with system buffers
	 *	and other internals and thus be subject to the SMP locking
	 *	rules. (On a uniprocessor box this does nothing).
	 */
	lock_kernel();
		 
	for (;;) {
#ifdef DEBUG
		printk("bdflush() activated...");
#endif

		CHECK_EMERGENCY_SYNC

		ncount = 0;
#ifdef DEBUG
		for(nlist = 0; nlist < NR_LIST; nlist++)
#else
		for(nlist = BUF_DIRTY; nlist <= BUF_DIRTY; nlist++)
#endif
		 {
			 ndirty = 0;
		 repeat:

			 bh = lru_list[nlist];
			 if(bh) 
				  for (i = nr_buffers_type[nlist]; i-- > 0 && ndirty < bdf_prm.b_un.ndirty; 
				       bh = next) {
					  /* We may have stalled while waiting for I/O to complete. */
					  if(bh->b_list != nlist) goto repeat;
					  next = bh->b_next_free;
					  if(!lru_list[nlist]) {
						  printk("Dirty list empty %d\n", i);
						  break;
					  }
					  
					  /* Clean buffer on dirty list?  Refile it */
					  if (nlist == BUF_DIRTY && !buffer_dirty(bh) && !buffer_locked(bh))
					   {
						   refile_buffer(bh);
						   continue;
					   }
					  
					  if (buffer_locked(bh) || !buffer_dirty(bh))
						   continue;
					  major = MAJOR(bh->b_dev);
					  /* Should we write back buffers that are shared or not??
					     currently dirty buffers are not shared, so it does not matter */
					  next->b_count++;
					  bh->b_count++;
					  ndirty++;
					  bh->b_flushtime = 0;
					  if (major == LOOP_MAJOR) {
						  ll_rw_block(wrta_cmd,1, &bh);
						  wrta_cmd = WRITEA;
						  if (buffer_dirty(bh))
							  --ndirty;
					  }
					  else
					  ll_rw_block(WRITE, 1, &bh);
#ifdef DEBUG
					  if(nlist != BUF_DIRTY) ncount++;
#endif
					  bh->b_count--;
					  next->b_count--;
				  }
		 }
#ifdef DEBUG
		if (ncount) printk("sys_bdflush: %d dirty buffers not on dirty list\n", ncount);
		printk("sleeping again.\n");
#endif
		/* If we didn't write anything, but there are still
		 * dirty buffers, then make the next write to a
		 * loop device to be a blocking write.
		 * This lets us block--which we _must_ do! */
		if (ndirty == 0 && nr_buffers_type[BUF_DIRTY] > 0 && wrta_cmd != WRITE) {
			wrta_cmd = WRITE;
			continue;
		}
		run_task_queue(&tq_disk);
		wake_up(&bdflush_done);
		
		/* If there are still a lot of dirty buffers around, skip the sleep
		   and flush some more */
		if(ndirty == 0 || nr_buffers_type[BUF_DIRTY] <= nr_buffers * bdf_prm.b_un.nfract/100) {
			spin_lock_irq(&current->sigmask_lock);
			flush_signals(current);
			spin_unlock_irq(&current->sigmask_lock);

			interruptible_sleep_on(&bdflush_wait);
		}
	}
}
