/*
 * Implementation of the diskquota system for the LINUX operating
 * system. QUOTA is implemented using the BSD system call interface as
 * the means of communication with the user level. Currently only the
 * ext2 filesystem has support for disk quotas. Other filesystems may
 * be added in the future. This file contains the generic routines
 * called by the different filesystems on allocation of an inode or
 * block. These routines take care of the administration needed to
 * have a consistent diskquota tracking system. The ideas of both
 * user and group quotas are based on the Melbourne quota system as
 * used on BSD derived systems. The internal implementation is 
 * based on one of the several variants of the LINUX inode-subsystem
 * with added complexity of the diskquota system.
 * 
 * Version: $Id: dquot.c,v 6.3 1996/11/17 18:35:34 mvw Exp mvw $
 * 
 * Author:	Marco van Wieringen <mvw@planets.elm.net>
 *
 * Fixes:   Dmitry Gorodchanin <pgmdsg@ibi.com>, 11 Feb 96
 *
 *		Revised list management to avoid races
 *		-- Bill Hawes, <whawes@star.net>, 9/98
 *
 * (C) Copyright 1994 - 1997 Marco van Wieringen 
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/malloc.h>
#include <linux/mount.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#define __DQUOT_VERSION__	"dquot_6.4.0"

int nr_dquots = 0, nr_free_dquots = 0;
int max_dquots = NR_DQUOTS;

static char quotamessage[MAX_QUOTA_MESSAGE];
static char *quotatypes[] = INITQFNAMES;

static kmem_cache_t *dquot_cachep;

/*
 * Dquot List Management:
 * The quota code uses three lists for dquot management: the inuse_list,
 * free_dquots, and dquot_hash[] array. A single dquot structure may be
 * on all three lists, depending on its current state.
 *
 * All dquots are placed on the inuse_list when first created, and this
 * list is used for the sync and invalidate operations, which must look
 * at every dquot.
 *
 * Unused dquots (dq_count == 0) are added to the free_dquots list when
 * freed, and this list is searched whenever we need an available dquot.
 * Dquots are removed from the list as soon as they are used again, and
 * nr_free_dquots gives the number of dquots on the list.
 *
 * Dquots with a specific identity (device, type and id) are placed on
 * one of the dquot_hash[] hash chains. The provides an efficient search
 * mechanism to lcoate a specific dquot.
 */

static struct dquot *inuse_list = NULL;
LIST_HEAD(free_dquots);
static struct dquot *dquot_hash[NR_DQHASH];
static int dquot_updating[NR_DQHASH];

static struct dqstats dqstats;
static struct wait_queue *dquot_wait = (struct wait_queue *)NULL,
                         *update_wait = (struct wait_queue *)NULL;

static inline char is_enabled(struct vfsmount *vfsmnt, short type)
{
	switch (type) {
		case USRQUOTA:
			return((vfsmnt->mnt_dquot.flags & DQUOT_USR_ENABLED) != 0);
		case GRPQUOTA:
			return((vfsmnt->mnt_dquot.flags & DQUOT_GRP_ENABLED) != 0);
	}
	return(0);
}

static inline char sb_has_quota_enabled(struct super_block *sb, short type)
{
	struct vfsmount *vfsmnt;

	return((vfsmnt = lookup_vfsmnt(sb->s_dev)) != (struct vfsmount *)NULL && is_enabled(vfsmnt, type));
}

static inline char dev_has_quota_enabled(kdev_t dev, short type)
{
	struct vfsmount *vfsmnt;

	return((vfsmnt = lookup_vfsmnt(dev)) != (struct vfsmount *)NULL && is_enabled(vfsmnt, type));
}

static inline int const hashfn(kdev_t dev, unsigned int id, short type)
{
	return((HASHDEV(dev) ^ id) * (MAXQUOTAS - type)) % NR_DQHASH;
}

static inline void insert_dquot_hash(struct dquot *dquot)
{
	struct dquot **htable;

	htable = &dquot_hash[hashfn(dquot->dq_dev, dquot->dq_id, dquot->dq_type)];
	if ((dquot->dq_hash_next = *htable) != NULL)
		(*htable)->dq_hash_pprev = &dquot->dq_hash_next;
	*htable = dquot;
	dquot->dq_hash_pprev = htable;
}

static inline void hash_dquot(struct dquot *dquot)
{
	insert_dquot_hash(dquot);
}

static inline void unhash_dquot(struct dquot *dquot)
{
	if (dquot->dq_hash_pprev) {
		if (dquot->dq_hash_next)
			dquot->dq_hash_next->dq_hash_pprev = dquot->dq_hash_pprev;
		*(dquot->dq_hash_pprev) = dquot->dq_hash_next;
		dquot->dq_hash_pprev = NULL;
	}
}

static inline struct dquot *find_dquot(unsigned int hashent, kdev_t dev, unsigned int id, short type)
{
	struct dquot *dquot;

	for (dquot = dquot_hash[hashent]; dquot; dquot = dquot->dq_hash_next)
		if (dquot->dq_dev == dev && dquot->dq_id == id && dquot->dq_type == type)
			break;
	return dquot;
}

/* Add a dquot to the head of the free list */
static inline void put_dquot_head(struct dquot *dquot)
{
	list_add(&dquot->dq_free, &free_dquots);
	nr_free_dquots++;
}

/* Add a dquot to the tail of the free list */
static inline void put_dquot_last(struct dquot *dquot)
{
	list_add(&dquot->dq_free, free_dquots.prev);
	nr_free_dquots++;
}

static inline void remove_free_dquot(struct dquot *dquot)
{
	/* sanity check */
	if (list_empty(&dquot->dq_free)) {
		printk("remove_free_dquot: dquot not on free list??\n");
	}
	list_del(&dquot->dq_free);
	INIT_LIST_HEAD(&dquot->dq_free);
	nr_free_dquots--;
}

static inline void put_inuse(struct dquot *dquot)
{
	if ((dquot->dq_next = inuse_list) != NULL)
		inuse_list->dq_pprev = &dquot->dq_next;
	inuse_list = dquot;
	dquot->dq_pprev = &inuse_list;
}

#if 0	/* currently not needed */
static inline void remove_inuse(struct dquot *dquot)
{
	if (dquot->dq_pprev) {
		if (dquot->dq_next)
			dquot->dq_next->dq_pprev = dquot->dq_pprev;
		*dquot->dq_pprev = dquot->dq_next;
		dquot->dq_pprev = NULL;
	}
}
#endif

static void __wait_on_dquot(struct dquot *dquot)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&dquot->dq_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (dquot->dq_flags & DQ_LOCKED) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&dquot->dq_wait, &wait);
	current->state = TASK_RUNNING;
}

static inline void wait_on_dquot(struct dquot *dquot)
{
	if (dquot->dq_flags & DQ_LOCKED)
		__wait_on_dquot(dquot);
}

static inline void lock_dquot(struct dquot *dquot)
{
	wait_on_dquot(dquot);
	dquot->dq_flags |= DQ_LOCKED;
}

static inline void unlock_dquot(struct dquot *dquot)
{
	dquot->dq_flags &= ~DQ_LOCKED;
	wake_up(&dquot->dq_wait);
}

static void write_dquot(struct dquot *dquot)
{
	short type = dquot->dq_type;
	struct file *filp = dquot->dq_mnt->mnt_dquot.files[type];
	mm_segment_t fs;
	loff_t offset;
	ssize_t ret;

	lock_dquot(dquot);
	down(&dquot->dq_mnt->mnt_dquot.semaphore);
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Note: clear the DQ_MOD flag unconditionally,
	 * so we don't loop forever on failure.
	 */
	dquot->dq_flags &= ~DQ_MOD;
	ret = 0;
	if (filp)
		ret = filp->f_op->write(filp, (char *)&dquot->dq_dqb, 
					sizeof(struct dqblk), &offset);
	if (ret != sizeof(struct dqblk))
		printk(KERN_WARNING "VFS: dquota write failed on dev %s\n",
			kdevname(dquot->dq_dev));

	up(&dquot->dq_mnt->mnt_dquot.semaphore);
	set_fs(fs);

	unlock_dquot(dquot);
	dqstats.writes++;
}

static void read_dquot(struct dquot *dquot)
{
	short type;
	struct file *filp;
	mm_segment_t fs;
	loff_t offset;

	type = dquot->dq_type;
	filp = dquot->dq_mnt->mnt_dquot.files[type];

	if (filp == (struct file *)NULL)
		return;

	lock_dquot(dquot);
	down(&dquot->dq_mnt->mnt_dquot.semaphore);
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);
	filp->f_op->read(filp, (char *)&dquot->dq_dqb, sizeof(struct dqblk), &offset);
	up(&dquot->dq_mnt->mnt_dquot.semaphore);
	set_fs(fs);

	if (dquot->dq_bhardlimit == 0 && dquot->dq_bsoftlimit == 0 &&
	    dquot->dq_ihardlimit == 0 && dquot->dq_isoftlimit == 0)
		dquot->dq_flags |= DQ_FAKE;
	unlock_dquot(dquot);
	dqstats.reads++;
}

/*
 * Unhash and selectively clear the dquot structure,
 * but preserve the use count, list pointers, and
 * wait queue.
 */
void clear_dquot(struct dquot *dquot)
{
	/* unhash it first */
        unhash_dquot(dquot);
        dquot->dq_mnt = NULL;
        dquot->dq_flags = 0;
        dquot->dq_referenced = 0;
        memset(&dquot->dq_dqb, 0, sizeof(struct dqblk));
}

void invalidate_dquots(kdev_t dev, short type)
{
	struct dquot *dquot, *next = inuse_list;
	int need_restart;

restart:
	need_restart = 0;
	while ((dquot = next) != NULL) {
		next = dquot->dq_next;
		if (dquot->dq_dev != dev)
			continue;
		if (dquot->dq_type != type)
			continue;
		if (dquot->dq_flags & DQ_LOCKED) {
			__wait_on_dquot(dquot);

			/* Set the flag for another pass. */
			need_restart = 1;
			/*
			 * Make sure it's still the same dquot.
			 */
			if (dquot->dq_dev != dev)
				continue;
			if (dquot->dq_type != type)
				continue;
		}
		clear_dquot(dquot);
	}
	/*
	 * If anything blocked, restart the operation
	 * to ensure we don't miss any dquots.
	 */ 
	if (need_restart)
		goto restart;
}

int sync_dquots(kdev_t dev, short type)
{
	struct dquot *dquot, *next = inuse_list;
	int need_restart;

restart:
	need_restart = 0;
	while ((dquot = next) != NULL) {
		next = dquot->dq_next;
		if (dev && dquot->dq_dev != dev)
			continue;
                if (type != -1 && dquot->dq_type != type)
			continue;
		if (!(dquot->dq_flags & (DQ_LOCKED | DQ_MOD)))
			continue;

		wait_on_dquot(dquot);
		if (dquot->dq_flags & DQ_MOD)
			write_dquot(dquot);
		/* Set the flag for another pass. */
		need_restart = 1;
	}
	/*
	 * If anything blocked, restart the operation
	 * to ensure we don't miss any dquots.
	 */ 
	if (need_restart)
		goto restart;

	dqstats.syncs++;
	return(0);
}

void dqput(struct dquot *dquot)
{
	if (!dquot)
		return;
	if (!dquot->dq_count) {
		printk("VFS: dqput: trying to free free dquot\n");
		printk("VFS: device %s, dquot of %s %d\n",
			kdevname(dquot->dq_dev), quotatypes[dquot->dq_type],
			dquot->dq_id);
		return;
	}

	/*
	 * If the dq_mnt pointer isn't initialized this entry needs no
	 * checking and doesn't need to be written. It's just an empty
	 * dquot that is put back on to the freelist.
	 */
	if (dquot->dq_mnt != (struct vfsmount *)NULL) {
		dqstats.drops++;
we_slept:
		wait_on_dquot(dquot);
		if (dquot->dq_count > 1) {
			dquot->dq_count--;
			return;
		}
		if (dquot->dq_flags & DQ_MOD) {
			write_dquot(dquot);
			goto we_slept;
		}
	}

	/* sanity check */
	if (!list_empty(&dquot->dq_free)) {
		printk("dqput: dquot already on free list??\n");
	}
	if (--dquot->dq_count == 0) {
 		/* Place at end of LRU free queue */
		put_dquot_last(dquot);
		wake_up(&dquot_wait);
	}

	return;
}

static void grow_dquots(void)
{
	struct dquot *dquot;
	int cnt = 32;

	while (cnt > 0) {
		dquot = kmem_cache_alloc(dquot_cachep, SLAB_KERNEL);
		if(!dquot)
			return;

		nr_dquots++;
		memset((caddr_t)dquot, 0, sizeof(struct dquot));
		/* all dquots go on the inuse_list */
		put_inuse(dquot);
		put_dquot_head(dquot);
		cnt--;
	}
}

static struct dquot *find_best_candidate_weighted(void)
{
	struct list_head *tmp = &free_dquots;
	struct dquot *dquot, *best = NULL;
	unsigned long myscore, bestscore = ~0U;
	int limit = (nr_free_dquots > 128) ? nr_free_dquots >> 2 : 32;

	while ((tmp = tmp->next) != &free_dquots && --limit) {
		dquot = list_entry(tmp, struct dquot, dq_free);
		if (dquot->dq_flags & (DQ_LOCKED | DQ_MOD))
			continue;
		myscore = dquot->dq_referenced;
		if (myscore < bestscore) {
			bestscore = myscore;
			best = dquot;
		}
	}
	return best;
}

static inline struct dquot *find_best_free(void)
{
	struct list_head *tmp = &free_dquots;
	struct dquot *dquot;
	int limit = (nr_free_dquots > 1024) ? nr_free_dquots >> 5 : 32;

	while ((tmp = tmp->next) != &free_dquots && --limit) {
		dquot = list_entry(tmp, struct dquot, dq_free);
		if (dquot->dq_referenced == 0)
			return dquot;
	}
	return NULL;
}

struct dquot *get_empty_dquot(void)
{
	struct dquot *dquot;
	int count;

repeat:
	dquot = find_best_free();
	if (!dquot)
		goto pressure;
got_it:
	if (dquot->dq_flags & (DQ_LOCKED | DQ_MOD)) {
		wait_on_dquot(dquot);
		if (dquot->dq_flags & DQ_MOD)
			write_dquot(dquot);
		/*
		 * The dquot may be back in use now, so we
		 * must recheck the free list.
		 */
		goto repeat;
	}
	/* sanity check ... */
	if (dquot->dq_count != 0)
		printk(KERN_ERR "VFS: free dquot count=%d\n", dquot->dq_count);

	remove_free_dquot(dquot);
	dquot->dq_count = 1;
	/* unhash and selectively clear the structure */
	clear_dquot(dquot);
	return dquot;

pressure:
	if (nr_dquots < max_dquots) {
		grow_dquots();
		goto repeat;
	}

	dquot = find_best_candidate_weighted();
	if (dquot)
		goto got_it;
	/*
	 * Try pruning the dcache to free up some dquots ...
	 */
	count = select_dcache(128, 0);
	if (count) {
		printk(KERN_DEBUG "get_empty_dquot: pruning %d\n", count);
		prune_dcache(count);
		free_inode_memory(count);
		goto repeat;
	}

	printk("VFS: No free dquots, contact mvw@planets.elm.net\n");
	sleep_on(&dquot_wait);
	goto repeat;
}

struct dquot *dqget(kdev_t dev, unsigned int id, short type)
{
	unsigned int hashent = hashfn(dev, id, type);
	struct dquot *dquot, *empty = NULL;
	struct vfsmount *vfsmnt;

        if ((vfsmnt = lookup_vfsmnt(dev)) == (struct vfsmount *)NULL || is_enabled(vfsmnt, type) == 0)
                return(NODQUOT);

we_slept:
	if ((dquot = find_dquot(hashent, dev, id, type)) == NULL) {
		if (empty == NULL) {
			dquot_updating[hashent]++;
			empty = get_empty_dquot();
			if (!--dquot_updating[hashent])
				wake_up(&update_wait);
			goto we_slept;
		}
		dquot = empty;
        	dquot->dq_id = id;
        	dquot->dq_type = type;
        	dquot->dq_dev = dev;
        	dquot->dq_mnt = vfsmnt;
		/* hash it first so it can be found */
		hash_dquot(dquot);
        	read_dquot(dquot);
	} else {
		if (!dquot->dq_count++) {
			remove_free_dquot(dquot);
		} else
			dqstats.cache_hits++;
		wait_on_dquot(dquot);
		if (empty)
			dqput(empty);
	}

	while (dquot_updating[hashent])
		sleep_on(&update_wait);

	dquot->dq_referenced++;
	dqstats.lookups++;

	return dquot;
}

static void add_dquot_ref(kdev_t dev, short type)
{
	struct super_block *sb = get_super(dev);
	struct file *filp;
	struct inode *inode;

	if (!sb || !sb->dq_op)
		return;	/* nothing to do */

	for (filp = inuse_filps; filp; filp = filp->f_next) {
		if (!filp->f_dentry)
			continue;
		if (filp->f_dentry->d_sb != sb)
			continue;
		inode = filp->f_dentry->d_inode;
		if (!inode)
			continue;
		/* N.B. race problem -- filp could become unused */
		if (filp->f_mode & FMODE_WRITE) {
			sb->dq_op->initialize(inode, type);
			inode->i_flags |= S_QUOTA;
		}
	}
}

static void reset_dquot_ptrs(kdev_t dev, short type)
{
	struct super_block *sb = get_super(dev);
	struct file *filp;
	struct inode *inode;
	struct dquot *dquot;
	int cnt;

	if (!sb || !sb->dq_op)
		return;	/* nothing to do */

restart:
	/* free any quota for unused dentries */
	shrink_dcache_sb(sb);

	for (filp = inuse_filps; filp; filp = filp->f_next) {
		if (!filp->f_dentry)
			continue;
		if (filp->f_dentry->d_sb != sb)
			continue;
		inode = filp->f_dentry->d_inode;
		if (!inode)
			continue;
		/*
		 * Note: we restart after each blocking operation,
		 * as the inuse_filps list may have changed.
		 */
		if (IS_QUOTAINIT(inode)) {
			dquot = inode->i_dquot[type];
			inode->i_dquot[type] = NODQUOT;
			/* any other quota in use? */
			for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
				if (inode->i_dquot[cnt] != NODQUOT)
					goto put_it;
			}
			inode->i_flags &= ~S_QUOTA;
		put_it:
			if (dquot != NODQUOT) {
				dqput(dquot);
				/* we may have blocked ... */
				goto restart;
			}
		}
	}
}

static inline void dquot_incr_inodes(struct dquot *dquot, unsigned long number)
{
	lock_dquot(dquot);
	dquot->dq_curinodes += number;
	dquot->dq_flags |= DQ_MOD;
	unlock_dquot(dquot);
}

static inline void dquot_incr_blocks(struct dquot *dquot, unsigned long number)
{
	lock_dquot(dquot);
	dquot->dq_curblocks += number;
	dquot->dq_flags |= DQ_MOD;
	unlock_dquot(dquot);
}

static inline void dquot_decr_inodes(struct dquot *dquot, unsigned long number)
{
	lock_dquot(dquot);
	if (dquot->dq_curinodes > number)
		dquot->dq_curinodes -= number;
	else
		dquot->dq_curinodes = 0;
	if (dquot->dq_curinodes < dquot->dq_isoftlimit)
		dquot->dq_itime = (time_t) 0;
	dquot->dq_flags &= ~DQ_INODES;
	dquot->dq_flags |= DQ_MOD;
	unlock_dquot(dquot);
}

static inline void dquot_decr_blocks(struct dquot *dquot, unsigned long number)
{
	lock_dquot(dquot);
	if (dquot->dq_curblocks > number)
		dquot->dq_curblocks -= number;
	else
		dquot->dq_curblocks = 0;
	if (dquot->dq_curblocks < dquot->dq_bsoftlimit)
		dquot->dq_btime = (time_t) 0;
	dquot->dq_flags &= ~DQ_BLKS;
	dquot->dq_flags |= DQ_MOD;
	unlock_dquot(dquot);
}

static inline char need_print_warning(short type, uid_t initiator, struct dquot *dquot)
{
	switch (type) {
		case USRQUOTA:
			return(initiator == dquot->dq_id);
		case GRPQUOTA:
			return(initiator == dquot->dq_id);
	}
	return(0);
}

static inline char ignore_hardlimit(struct dquot *dquot, uid_t initiator)
{
	return(initiator == 0 && dquot->dq_mnt->mnt_dquot.rsquash[dquot->dq_type] == 0);
}

static int check_idq(struct dquot *dquot, short type, u_long short inodes, uid_t initiator, 
			struct tty_struct *tty)
{
	if (inodes <= 0 || dquot->dq_flags & DQ_FAKE)
		return(QUOTA_OK);

	if (dquot->dq_ihardlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_ihardlimit &&
            !ignore_hardlimit(dquot, initiator)) {
		if ((dquot->dq_flags & DQ_INODES) == 0 &&
                     need_print_warning(type, initiator, dquot)) {
			sprintf(quotamessage, "%s: write failed, %s file limit reached\n",
			        dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(tty, quotamessage);
			dquot->dq_flags |= DQ_INODES;
		}
		return(NO_QUOTA);
	}

	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime && CURRENT_TIME >= dquot->dq_itime &&
            !ignore_hardlimit(dquot, initiator)) {
                if (need_print_warning(type, initiator, dquot)) {
			sprintf(quotamessage, "%s: warning, %s file quota exceeded too long.\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(tty, quotamessage);
		}
		return(NO_QUOTA);
	}

	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime == 0) {
                if (need_print_warning(type, initiator, dquot)) {
			sprintf(quotamessage, "%s: warning, %s file quota exceeded\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(tty, quotamessage);
		}
		dquot->dq_itime = CURRENT_TIME + dquot->dq_mnt->mnt_dquot.inode_expire[type];
	}

	return(QUOTA_OK);
}

static int check_bdq(struct dquot *dquot, short type, u_long blocks, uid_t initiator, 
			struct tty_struct *tty, char warn)
{
	if (blocks <= 0 || dquot->dq_flags & DQ_FAKE)
		return(QUOTA_OK);

	if (dquot->dq_bhardlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bhardlimit &&
            !ignore_hardlimit(dquot, initiator)) {
		if (warn && (dquot->dq_flags & DQ_BLKS) == 0 &&
                     need_print_warning(type, initiator, dquot)) {
			sprintf(quotamessage, "%s: write failed, %s disk limit reached.\n",
			        dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(tty, quotamessage);
			dquot->dq_flags |= DQ_BLKS;
		}
		return(NO_QUOTA);
	}

	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime && CURRENT_TIME >= dquot->dq_btime &&
            !ignore_hardlimit(dquot, initiator)) {
                if (warn && need_print_warning(type, initiator, dquot)) {
			sprintf(quotamessage, "%s: write failed, %s disk quota exceeded too long.\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(tty, quotamessage);
		}
		return(NO_QUOTA);
	}

	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime == 0) {
                if (warn && need_print_warning(type, initiator, dquot)) {
			sprintf(quotamessage, "%s: warning, %s disk quota exceeded\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(tty, quotamessage);
		}
		dquot->dq_btime = CURRENT_TIME + dquot->dq_mnt->mnt_dquot.block_expire[type];
	}

	return(QUOTA_OK);
}

/*
 * Initialize a dquot-struct with new quota info. This is used by the
 * system call interface functions.
 */ 
static int set_dqblk(kdev_t dev, int id, short type, int flags, struct dqblk *dqblk)
{
	struct dquot *dquot;
	int error = -EFAULT;
	struct dqblk dq_dqblk;

	if (dqblk == (struct dqblk *)NULL)
		return error;

	if (flags & QUOTA_SYSCALL) {
		if (copy_from_user(&dq_dqblk, dqblk, sizeof(struct dqblk)))
			return(error);
	} else
		memcpy((caddr_t)&dq_dqblk, (caddr_t)dqblk, sizeof(struct dqblk));

	if ((dquot = dqget(dev, id, type)) != NODQUOT) {
		lock_dquot(dquot);

		if (id > 0 && ((flags & SET_QUOTA) || (flags & SET_QLIMIT))) {
			dquot->dq_bhardlimit = dq_dqblk.dqb_bhardlimit;
			dquot->dq_bsoftlimit = dq_dqblk.dqb_bsoftlimit;
			dquot->dq_ihardlimit = dq_dqblk.dqb_ihardlimit;
			dquot->dq_isoftlimit = dq_dqblk.dqb_isoftlimit;
		}

		if ((flags & SET_QUOTA) || (flags & SET_USE)) {
			if (dquot->dq_isoftlimit &&
			    dquot->dq_curinodes < dquot->dq_isoftlimit &&
			    dq_dqblk.dqb_curinodes >= dquot->dq_isoftlimit)
				dquot->dq_itime = CURRENT_TIME + dquot->dq_mnt->mnt_dquot.inode_expire[type];
			dquot->dq_curinodes = dq_dqblk.dqb_curinodes;
			if (dquot->dq_curinodes < dquot->dq_isoftlimit)
				dquot->dq_flags &= ~DQ_INODES;
			if (dquot->dq_bsoftlimit &&
			    dquot->dq_curblocks < dquot->dq_bsoftlimit &&
			    dq_dqblk.dqb_curblocks >= dquot->dq_bsoftlimit)
				dquot->dq_btime = CURRENT_TIME + dquot->dq_mnt->mnt_dquot.block_expire[type];
			dquot->dq_curblocks = dq_dqblk.dqb_curblocks;
			if (dquot->dq_curblocks < dquot->dq_bsoftlimit)
				dquot->dq_flags &= ~DQ_BLKS;
		}

		if (id == 0) {
			dquot->dq_mnt->mnt_dquot.block_expire[type] = dquot->dq_btime = dq_dqblk.dqb_btime;
			dquot->dq_mnt->mnt_dquot.inode_expire[type] = dquot->dq_itime = dq_dqblk.dqb_itime;
		}

		if (dq_dqblk.dqb_bhardlimit == 0 && dq_dqblk.dqb_bsoftlimit == 0 &&
		    dq_dqblk.dqb_ihardlimit == 0 && dq_dqblk.dqb_isoftlimit == 0)
			dquot->dq_flags |= DQ_FAKE;
		else
			dquot->dq_flags &= ~DQ_FAKE;

		dquot->dq_flags |= DQ_MOD;
		unlock_dquot(dquot);
		dqput(dquot);
	}
	return(0);
}

static int get_quota(kdev_t dev, int id, short type, struct dqblk *dqblk)
{
	struct dquot *dquot;
	int error = -ESRCH;

	if (!dev_has_quota_enabled(dev, type))
		goto out;
	dquot = dqget(dev, id, type);
	if (dquot == NODQUOT)
		goto out;

	error = -EFAULT;
	if (dqblk && !copy_to_user(dqblk, &dquot->dq_dqb, sizeof(struct dqblk)))
		error = 0;
	dqput(dquot);
out:
	return error;
}

static int get_stats(caddr_t addr)
{
	int error = -EFAULT;
	struct dqstats stats;

	dqstats.allocated_dquots = nr_dquots;
	dqstats.free_dquots = nr_free_dquots;

	/* make a copy, in case we page-fault in user space */
	memcpy(&stats, &dqstats, sizeof(struct dqstats));
	if (!copy_to_user(addr, &stats, sizeof(struct dqstats)))
		error = 0;
	return error;
}

static int quota_root_squash(kdev_t dev, short type, int *addr)
{
	struct vfsmount *vfsmnt;
	int new_value, error;

	if ((vfsmnt = lookup_vfsmnt(dev)) == (struct vfsmount *)NULL)
		return(-ENODEV);

	error = -EFAULT;
	if (!copy_from_user(&new_value, addr, sizeof(int))) {
		vfsmnt->mnt_dquot.rsquash[type] = new_value;
		error = 0;
	}
	return error;
}

/*
 * This is a simple algorithm that calculates the size of a file in blocks.
 * This is only used on filesystems that do not have an i_blocks count.
 */
static u_long isize_to_blocks(size_t isize, size_t blksize)
{
	u_long blocks;
	u_long indirect;

	if (!blksize)
		blksize = BLOCK_SIZE;
	blocks = (isize / blksize) + ((isize % blksize) ? 1 : 0);
	if (blocks > 10) {
		indirect = ((blocks - 11) >> 8) + 1; /* single indirect blocks */
		if (blocks > (10 + 256)) {
			indirect += ((blocks - 267) >> 16) + 1; /* double indirect blocks */
			if (blocks > (10 + 256 + (256 << 8)))
				indirect++; /* triple indirect blocks */
		}
		blocks += indirect;
	}
	return(blocks);
}

/*
 * Externally referenced functions through dquot_operations in inode.
 *
 * Note: this is a blocking operation.
 */
void dquot_initialize(struct inode *inode, short type)
{
	struct dquot *dquot;
	unsigned int id = 0;
	short cnt;

	if (S_ISREG(inode->i_mode) ||
            S_ISDIR(inode->i_mode) ||
            S_ISLNK(inode->i_mode)) {
		for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
			if (type != -1 && cnt != type)
				continue;

			if (!sb_has_quota_enabled(inode->i_sb, cnt))
				continue;

			if (inode->i_dquot[cnt] == NODQUOT) {
				switch (cnt) {
					case USRQUOTA:
						id = inode->i_uid;
						break;
					case GRPQUOTA:
						id = inode->i_gid;
						break;
				}
				dquot = dqget(inode->i_dev, id, cnt);
				if (inode->i_dquot[cnt] != NODQUOT) {
					dqput(dquot);
					continue;
				} 
				inode->i_dquot[cnt] = dquot;
				inode->i_flags |= S_QUOTA;
			}
		}
	}
}

/*
 * Release all quota for the specified inode.
 *
 * Note: this is a blocking operation.
 */
void dquot_drop(struct inode *inode)
{
	struct dquot *dquot;
	short cnt;

	inode->i_flags &= ~S_QUOTA;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot = inode->i_dquot[cnt];
		inode->i_dquot[cnt] = NODQUOT;
		dqput(dquot);
	}
}

/*
 * Note: this is a blocking operation.
 */
int dquot_alloc_block(const struct inode *inode, unsigned long number, uid_t initiator, 
			char warn)
{
	unsigned short cnt;
	struct tty_struct *tty = current->tty;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		if (check_bdq(inode->i_dquot[cnt], cnt, number, initiator, tty, warn))
			return(NO_QUOTA);
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_blocks(inode->i_dquot[cnt], number);
	}

	return(QUOTA_OK);
}

/*
 * Note: this is a blocking operation.
 */
int dquot_alloc_inode(const struct inode *inode, unsigned long number, uid_t initiator)
{
	unsigned short cnt;
	struct tty_struct *tty = current->tty;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		if (check_idq(inode->i_dquot[cnt], cnt, number, initiator, tty))
			return(NO_QUOTA);
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_inodes(inode->i_dquot[cnt], number);
	}

	return(QUOTA_OK);
}

/*
 * Note: this is a blocking operation.
 */
void dquot_free_block(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_decr_blocks(inode->i_dquot[cnt], number);
	}
}

/*
 * Note: this is a blocking operation.
 */
void dquot_free_inode(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_decr_inodes(inode->i_dquot[cnt], number);
	}
}

/*
 * Transfer the number of inode and blocks from one diskquota to an other.
 *
 * Note: this is a blocking operation.
 */
int dquot_transfer(struct inode *inode, struct iattr *iattr, char direction, uid_t initiator)
{
	unsigned long blocks;
	struct dquot *transfer_from[MAXQUOTAS];
	struct dquot *transfer_to[MAXQUOTAS];
	struct tty_struct *tty = current->tty;
	short cnt, disc;

	/*
	 * Find out if this filesystem uses i_blocks.
	 */
	if (inode->i_blksize == 0)
		blocks = isize_to_blocks(inode->i_size, BLOCK_SIZE);
	else
		blocks = (inode->i_blocks / 2);

	/*
	 * Build the transfer_from and transfer_to lists and check quotas to see
	 * if operation is permitted.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		transfer_from[cnt] = NODQUOT;
		transfer_to[cnt] = NODQUOT;

		if (!sb_has_quota_enabled(inode->i_sb, cnt))
			continue;

		switch (cnt) {
			case USRQUOTA:
				if (inode->i_uid == iattr->ia_uid)
					continue;
				transfer_from[cnt] = dqget(inode->i_dev, (direction) ? iattr->ia_uid : inode->i_uid, cnt);
				transfer_to[cnt] = dqget(inode->i_dev, (direction) ? inode->i_uid : iattr->ia_uid, cnt);
				break;
			case GRPQUOTA:
				if (inode->i_gid == iattr->ia_gid)
					continue;
				transfer_from[cnt] = dqget(inode->i_dev, (direction) ? iattr->ia_gid : inode->i_gid, cnt);
				transfer_to[cnt] = dqget(inode->i_dev, (direction) ? inode->i_gid : iattr->ia_gid, cnt);
				break;
		}

		if (check_idq(transfer_to[cnt], cnt, 1, initiator, tty) == NO_QUOTA ||
		    check_bdq(transfer_to[cnt], cnt, blocks, initiator, tty, 0) == NO_QUOTA) {
			for (disc = 0; disc <= cnt; disc++) {
				dqput(transfer_from[disc]);
				dqput(transfer_to[disc]);
			}
			return(NO_QUOTA);
		}
	}

	/*
	 * Finally perform the needed transfer from transfer_from to transfer_to,
	 * and release any pointers to dquots not needed anymore.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		/*
		 * Skip changes for same uid or gid or for non-existing quota-type.
		 */
		if (transfer_from[cnt] == NODQUOT && transfer_to[cnt] == NODQUOT)
			continue;

		if (transfer_from[cnt] != NODQUOT) {
			dquot_decr_inodes(transfer_from[cnt], 1);
			dquot_decr_blocks(transfer_from[cnt], blocks);
		}

		if (transfer_to[cnt] != NODQUOT) {
			dquot_incr_inodes(transfer_to[cnt], 1);
			dquot_incr_blocks(transfer_to[cnt], blocks);
		}

		if (inode->i_dquot[cnt] != NODQUOT) {
			struct dquot *temp = inode->i_dquot[cnt];
			inode->i_dquot[cnt] = transfer_to[cnt];
			dqput(temp);
			dqput(transfer_from[cnt]);
		} else {
			dqput(transfer_from[cnt]);
			dqput(transfer_to[cnt]);
		}
	}

	return(QUOTA_OK);
}


void __init dquot_init_hash(void)
{
	printk(KERN_NOTICE "VFS: Diskquotas version %s initialized\n", __DQUOT_VERSION__);

	dquot_cachep = kmem_cache_create("dquot", sizeof(struct dquot),
					 sizeof(unsigned long) * 4,
					 SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!dquot_cachep)
		panic("Cannot create dquot SLAB cache\n");

	memset(dquot_hash, 0, sizeof(dquot_hash));
	memset((caddr_t)&dqstats, 0, sizeof(dqstats));
}

/*
 * Definitions of diskquota operations.
 */
struct dquot_operations dquot_operations = {
	dquot_initialize,		/* mandatory */
	dquot_drop,			/* mandatory */
	dquot_alloc_block,
	dquot_alloc_inode,
	dquot_free_block,
	dquot_free_inode,
	dquot_transfer
};

static inline void set_enable_flags(struct vfsmount *vfsmnt, short type)
{
	switch (type) {
		case USRQUOTA:
			vfsmnt->mnt_dquot.flags |= DQUOT_USR_ENABLED;
			break;
		case GRPQUOTA:
			vfsmnt->mnt_dquot.flags |= DQUOT_GRP_ENABLED;
			break;
	}
}

static inline void reset_enable_flags(struct vfsmount *vfsmnt, short type)
{
	switch (type) {
		case USRQUOTA:
			vfsmnt->mnt_dquot.flags &= ~DQUOT_USR_ENABLED;
			break;
		case GRPQUOTA:
			vfsmnt->mnt_dquot.flags &= ~DQUOT_GRP_ENABLED;
			break;
	}
}

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int quota_off(kdev_t dev, short type)
{
	struct vfsmount *vfsmnt;
	struct file *filp;
	short cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;

		vfsmnt = lookup_vfsmnt(dev);
		if (!vfsmnt)
			goto out;
                if (!vfsmnt->mnt_sb)
			goto out;
		if (!is_enabled(vfsmnt, cnt))
			continue;
		reset_enable_flags(vfsmnt, cnt);

		/* Note: these are blocking operations */
		reset_dquot_ptrs(dev, cnt);
		invalidate_dquots(dev, cnt);

		filp = vfsmnt->mnt_dquot.files[cnt];
		vfsmnt->mnt_dquot.files[cnt] = (struct file *)NULL;
		vfsmnt->mnt_dquot.inode_expire[cnt] = 0;
		vfsmnt->mnt_dquot.block_expire[cnt] = 0;
		fput(filp);
	}

	/*
	 * Check whether any quota is still enabled,
	 * and if not clear the dq_op pointer.
	 */
	vfsmnt = lookup_vfsmnt(dev);
	if (vfsmnt && vfsmnt->mnt_sb) {
		int enabled = 0;
		for (cnt = 0; cnt < MAXQUOTAS; cnt++)
			enabled |= is_enabled(vfsmnt, cnt);
		if (!enabled)
			vfsmnt->mnt_sb->dq_op = NULL;
	}

out:
	return(0);
}

int quota_on(kdev_t dev, short type, char *path)
{
	struct file *filp = NULL;
	struct dentry *dentry;
	struct vfsmount *vfsmnt;
	struct inode *inode;
	struct dquot *dquot;
	char *tmp;
	int error;

	vfsmnt = lookup_vfsmnt(dev);
	if (vfsmnt == (struct vfsmount *)NULL)
		return -ENODEV;

	if (is_enabled(vfsmnt, type))
		return(-EBUSY);

	tmp = getname(path);
	error = PTR_ERR(tmp);
	if (IS_ERR(tmp))
		return error;

	dentry = open_namei(tmp, O_RDWR, 0600);
	putname(tmp);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return error;
	inode = dentry->d_inode;

	if (!S_ISREG(inode->i_mode)) {
		dput(dentry);
		return -EACCES;
	}

	if (inode->i_size == 0 || (inode->i_size % sizeof(struct dqblk)) != 0) {
		dput(dentry);
		return(-EINVAL);
	}

	filp = get_empty_filp();
	if (filp != (struct file *)NULL) {
		filp->f_mode = (O_RDWR + 1) & O_ACCMODE;
		filp->f_flags = O_RDWR;
		filp->f_dentry = dentry;
		filp->f_pos = 0;
		filp->f_reada = 0;
		filp->f_op = inode->i_op->default_file_ops;
		if (filp->f_op->read || filp->f_op->write) {
			error = get_write_access(inode);
			if (!error) {
				if (filp->f_op && filp->f_op->open)
					error = filp->f_op->open(inode, filp);
				if (!error) {
					set_enable_flags(vfsmnt, type);
					vfsmnt->mnt_dquot.files[type] = filp;

					dquot = dqget(dev, 0, type);
					vfsmnt->mnt_dquot.inode_expire[type] = (dquot) ? dquot->dq_itime : MAX_IQ_TIME;
					vfsmnt->mnt_dquot.block_expire[type] = (dquot) ? dquot->dq_btime : MAX_DQ_TIME;
					dqput(dquot);

					vfsmnt->mnt_sb->dq_op = &dquot_operations;
					add_dquot_ref(dev, type);

					return(0);
				}
				put_write_access(inode);
			}
		} else
			error = -EIO;
		put_filp(filp);
	} else
		error = -EMFILE;

	dput(dentry);

	return(error);
}

/*
 * This is the system call interface. This communicates with
 * the user-level programs. Currently this only supports diskquota
 * calls. Maybe we need to add the process quotas etc. in the future,
 * but we probably should use rlimits for that.
 */
asmlinkage int sys_quotactl(int cmd, const char *special, int id, caddr_t addr)
{
	int cmds = 0, type = 0, flags = 0;
	kdev_t dev;
	int ret = -EINVAL;

	lock_kernel();
	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

	if ((u_int) type >= MAXQUOTAS)
		goto out;
	ret = -EPERM;
	switch (cmds) {
		case Q_SYNC:
		case Q_GETSTATS:
			break;
		case Q_GETQUOTA:
			if (((type == USRQUOTA && current->uid != id) ||
			     (type == GRPQUOTA && current->gid != id)) &&
			    !capable(CAP_SYS_RESOURCE))
				goto out;
			break;
		default:
			if (!capable(CAP_SYS_RESOURCE))
				goto out;
	}

	ret = -EINVAL;
	dev = 0;
	if (special != NULL || (cmds != Q_SYNC && cmds != Q_GETSTATS)) {
		mode_t mode;
		struct dentry * dentry;

		dentry = namei(special);
		if (IS_ERR(dentry))
			goto out;

		dev = dentry->d_inode->i_rdev;
		mode = dentry->d_inode->i_mode;
		dput(dentry);

		ret = -ENOTBLK;
		if (!S_ISBLK(mode))
			goto out;
	}

	ret = -EINVAL;
	switch (cmds) {
		case Q_QUOTAON:
			ret = quota_on(dev, type, (char *) addr);
			goto out;
		case Q_QUOTAOFF:
			ret = quota_off(dev, type);
			goto out;
		case Q_GETQUOTA:
			ret = get_quota(dev, id, type, (struct dqblk *) addr);
			goto out;
		case Q_SETQUOTA:
			flags |= SET_QUOTA;
			break;
		case Q_SETUSE:
			flags |= SET_USE;
			break;
		case Q_SETQLIM:
			flags |= SET_QLIMIT;
			break;
		case Q_SYNC:
			ret = sync_dquots(dev, type);
			goto out;
		case Q_GETSTATS:
			ret = get_stats(addr);
			goto out;
		case Q_RSQUASH:
			ret = quota_root_squash(dev, type, (int *) addr);
			goto out;
		default:
			goto out;
	}

	flags |= QUOTA_SYSCALL;

	ret = -ESRCH;
	if (dev_has_quota_enabled(dev, type))
		ret = set_dqblk(dev, id, type, flags, (struct dqblk *) addr);
out:
	unlock_kernel();
	return ret;
}
