/*
 * Implementation of the diskquota system for the LINUX operating
 * system. QUOTA is implemented using the BSD systemcall interface as
 * the means of communication with the user level. Currently only the
 * ext2-filesystem has support for diskquotas. Other filesystems may
 * be added in future time. This file contains the generic routines
 * called by the different filesystems on allocation of an inode or
 * block. These routines take care of the administration needed to
 * have a consistent diskquota tracking system. The ideas of both
 * user and group quotas are based on the Melbourne quota system as
 * used on BSD derived systems. The internal implementation is 
 * based on the LINUX inode-subsystem with added complexity of the
 * diskquota system. This implementation is not based on any BSD
 * kernel sourcecode.
 * 
 * Version: $Id: dquot.c,v 1.11 1997/01/06 06:53:02 davem Exp $
 * 
 * Author:  Marco van Wieringen <mvw@mcs.ow.nl> <mvw@tnix.net>
 * 
 * Fixes:   Dmitry Gorodchanin <begemot@bgm.rosprint.net>, 11 Feb 96
 *	    removed race conditions in dqput(), dqget() and iput(). 
 *          Andi Kleen removed all verify_area() calls, 31 Dec 96  
 *          Nick Kralevich <nickkral@cal.alumni.berkeley.edu>, 21 Jul 97
 *          Fixed a condition where user and group quotas could get mixed up.
 *
 *          Chris Rankin <rankinc@bellsouth.net>, 31 Dec 97, 2-4 Jan 98
 *          Fixed kernel API so that the user can get the quota for any
 *          group s/he belongs to. Also return useful error codes when
 *          turning quotas off, and fixed sync_dquot() so that all devices
 *          are synced when dev==NODEV. 
 *
 * (C) Copyright 1994, 1995 Marco van Wieringen 
 *
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

#include <asm/uaccess.h>

#define __DQUOT_VERSION__	"dquot_5.6.0"

static char quotamessage[MAX_QUOTA_MESSAGE];
static char *quotatypes[] = INITQFNAMES;

static int nr_dquots = 0, nr_free_dquots = 0;
static struct dquot *hash_table[NR_DQHASH];
static struct dquot *first_dquot;
static struct dqstats dqstats;

static struct wait_queue *dquot_wait = (struct wait_queue *)NULL;

extern void add_dquot_ref(kdev_t dev, short type);
extern void reset_dquot_ptrs(kdev_t dev, short type);

#ifndef min
#define min(a,b) ((a) < (b)) ? (a) : (b)
#endif

/*
 * Functions for management of the hashlist.
 */
static inline int const hashfn(kdev_t dev, unsigned int id, short type)
{
	return((HASHDEV(dev) ^ id) * (MAXQUOTAS - type)) % NR_DQHASH;
}

static inline struct dquot **const hash(kdev_t dev, unsigned int id, short type)
{
	return(hash_table + hashfn(dev, id, type));
}

static inline int has_quota_enabled(kdev_t dev, short type)
{
	struct vfsmount *vfsmnt;

	return((vfsmnt = lookup_vfsmnt(dev)) != (struct vfsmount *)NULL &&
	       (vfsmnt->mnt_quotas[type] != (struct file *)NULL));
}

static void insert_dquot_free(struct dquot *dquot)
{
	dquot->dq_next = first_dquot;
	dquot->dq_prev = first_dquot->dq_prev;
	dquot->dq_next->dq_prev = dquot;
	dquot->dq_prev->dq_next = dquot;
	first_dquot = dquot;
}

static void remove_dquot_free(struct dquot *dquot)
{
	if (first_dquot == dquot)
		first_dquot = first_dquot->dq_next;
	if (dquot->dq_next)
		dquot->dq_next->dq_prev = dquot->dq_prev;
	if (dquot->dq_prev)
		dquot->dq_prev->dq_next = dquot->dq_next;
	dquot->dq_next = dquot->dq_prev = NODQUOT;
}

static void insert_dquot_hash(struct dquot *dquot)
{
	struct dquot **hash_ent;

	hash_ent = hash(dquot->dq_dev, dquot->dq_id, dquot->dq_type);
	dquot->dq_hash_next = *hash_ent;
	dquot->dq_hash_prev = NODQUOT;
	if (dquot->dq_hash_next)
		dquot->dq_hash_next->dq_hash_prev = dquot;
	*hash_ent = dquot;
}

static void remove_dquot_hash(struct dquot *dquot)
{
	struct dquot **hash_ent;

	hash_ent = hash(dquot->dq_dev, dquot->dq_id, dquot->dq_type);
	if (*hash_ent == dquot)
		*hash_ent = dquot->dq_hash_next;
	if (dquot->dq_hash_next)
		dquot->dq_hash_next->dq_hash_prev = dquot->dq_hash_prev;
	if (dquot->dq_hash_prev)
		dquot->dq_hash_prev->dq_hash_next = dquot->dq_hash_next;
	dquot->dq_hash_prev = dquot->dq_hash_next = NODQUOT;
}

static void put_last_free(struct dquot *dquot)
{
	remove_dquot_free(dquot);
	dquot->dq_prev = first_dquot->dq_prev;
	dquot->dq_prev->dq_next = dquot;
	dquot->dq_next = first_dquot;
	dquot->dq_next->dq_prev = dquot;
}

static void grow_dquots(void)
{
	struct dquot *dquot;
	int cnt;

	if (!(dquot = (struct dquot*) get_free_page(GFP_KERNEL)))
		return;
	dqstats.pages_allocated++;
	cnt = PAGE_SIZE / sizeof(struct dquot);
	nr_dquots += cnt;
	nr_free_dquots += cnt;
	if (!first_dquot) {
		dquot->dq_next = dquot->dq_prev = first_dquot = dquot++;
		cnt--;
	}
	for (; cnt; cnt--)
		insert_dquot_free(dquot++);
}

/*
 * Functions for locking and waiting on dquots.
 */
static void __wait_on_dquot(struct dquot *dquot)
{
	struct wait_queue wait = {current, NULL};

	add_wait_queue(&dquot->dq_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (dquot->dq_flags & DQ_LOCKED) {
		dquot->dq_flags |= DQ_WANT;
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
	if (dquot->dq_flags & DQ_WANT) {
		dquot->dq_flags &= ~DQ_WANT;
		wake_up(&dquot->dq_wait);
	}
}
/*
 * Note that we don't want to disturb any wait-queues when we discard
 * an dquot.
 *
 * FIXME: As soon as we have a nice solution for the inode problem we
 *		  can also fix this one. I.e. the volatile part.
 */
static void clear_dquot(struct dquot * dquot)
{
	struct wait_queue *wait;

	wait_on_dquot(dquot);
	remove_dquot_hash(dquot);
	remove_dquot_free(dquot);
	wait = ((volatile struct dquot *) dquot)->dq_wait;
	if (dquot->dq_count)
		nr_free_dquots++;
	memset(dquot, 0, sizeof(*dquot));
	((volatile struct dquot *) dquot)->dq_wait = wait;
	insert_dquot_free(dquot);
}

static void write_dquot(struct dquot *dquot)
{
	short type = dquot->dq_type;
	struct file *filp = dquot->dq_mnt->mnt_quotas[type];
	mm_segment_t fs;
	loff_t offset;

	if (!(dquot->dq_flags & DQ_MOD) || (filp == (struct file *)NULL))
		return;
	lock_dquot(dquot);
	down(&dquot->dq_mnt->mnt_sem);
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);

	if (filp->f_op->write(filp, (char *)&dquot->dq_dqb, sizeof(struct dqblk), &offset) == sizeof(struct dqblk))
		dquot->dq_flags &= ~DQ_MOD;

	up(&dquot->dq_mnt->mnt_sem);
	set_fs(fs);
	unlock_dquot(dquot);
	dqstats.writes++;
}

static void read_dquot(struct dquot *dquot)
{
	short type = dquot->dq_type;
	struct file *filp = dquot->dq_mnt->mnt_quotas[type];
	mm_segment_t fs;
	loff_t offset;

	if (filp == (struct file *)NULL)
		return;
	lock_dquot(dquot);
	down(&dquot->dq_mnt->mnt_sem);
	offset = dqoff(dquot->dq_id);
	fs = get_fs();
	set_fs(KERNEL_DS);
	filp->f_op->read(filp, (char *)&dquot->dq_dqb, sizeof(struct dqblk), &offset);
	up(&dquot->dq_mnt->mnt_sem);
	set_fs(fs);
	if (dquot->dq_bhardlimit == 0 && dquot->dq_bsoftlimit == 0 &&
	    dquot->dq_ihardlimit == 0 && dquot->dq_isoftlimit == 0)
		dquot->dq_flags |= DQ_FAKE;
	unlock_dquot(dquot);
	dqstats.reads++;
}

int sync_dquots(kdev_t dev, short type)
{
	struct dquot *dquot = first_dquot;
	int i;

	dqstats.syncs++;
	for (i = 0; i < nr_dquots * 2; i++, dquot = dquot->dq_next) {
		if ((dev != NODEV && dquot->dq_dev != dev) || dquot->dq_count == 0 )
			continue;
		if (type != -1 && dquot->dq_type != type)
			continue;
		wait_on_dquot(dquot);
		if (dquot->dq_flags & DQ_MOD)
			write_dquot(dquot);
	}
	return(0);
}

/*
 * Trash the cache for a certain type on a device.
 */
void invalidate_dquots(kdev_t dev, short type)
{
	struct dquot *dquot, *next;
	int cnt;

	next = first_dquot;
	for (cnt = nr_dquots ; cnt > 0 ; cnt--) {
		dquot = next;
		next = dquot->dq_next;
		if (dquot->dq_dev != dev || dquot->dq_type != type)
			continue;
		if (dquot->dq_flags & DQ_LOCKED) {
			printk("VFS: dquot busy on removed device %s\n", kdevname(dev));
			continue;
		}
		if (dquot->dq_flags & DQ_MOD)
			write_dquot(dquot);
		dqstats.drops++;
		clear_dquot(dquot);
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

static inline int need_print_warning(short type, struct dquot *dquot)
{
	switch (type) {
		case USRQUOTA:
			return(current->fsuid == dquot->dq_id);
		case GRPQUOTA:
			return(current->fsgid == dquot->dq_id);
	}
	return(0);
}

static int check_idq(struct dquot *dquot, short type, u_long short inodes)
{
	if (inodes <= 0 || dquot->dq_flags & DQ_FAKE)
		return(QUOTA_OK);
	if (dquot->dq_ihardlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_ihardlimit && !fsuser()) {
		if ((dquot->dq_flags & DQ_INODES) == 0 &&
                     need_print_warning(type, dquot)) {
			sprintf(quotamessage, "%s: write failed, %s file limit reached\r\n",
			        dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(current->tty, quotamessage);
			dquot->dq_flags |= DQ_INODES;
		}
		return(NO_QUOTA);
	}
	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime && CURRENT_TIME >= dquot->dq_itime && !fsuser()) {
                if (need_print_warning(type, dquot)) {
			sprintf(quotamessage, "%s: warning, %s file quota exceeded too long.\r\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(current->tty, quotamessage);
		}
		return(NO_QUOTA);
	}
	if (dquot->dq_isoftlimit &&
	   (dquot->dq_curinodes + inodes) > dquot->dq_isoftlimit &&
	    dquot->dq_itime == 0 && !fsuser()) {
                if (need_print_warning(type, dquot)) {
			sprintf(quotamessage, "%s: warning, %s file quota exceeded\r\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(current->tty, quotamessage);
		}
		dquot->dq_itime = CURRENT_TIME + dquot->dq_mnt->mnt_iexp[type];
	}
	return(QUOTA_OK);
}

static int check_bdq(struct dquot *dquot, short type, u_long blocks)
{
	if (blocks <= 0 || dquot->dq_flags & DQ_FAKE)
		return(QUOTA_OK);
	if (dquot->dq_bhardlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bhardlimit && !fsuser()) {
		if ((dquot->dq_flags & DQ_BLKS) == 0 &&
                     need_print_warning(type, dquot)) {
			sprintf(quotamessage, "%s: write failed, %s disk limit reached.\r\n",
			        dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(current->tty, quotamessage);
			dquot->dq_flags |= DQ_BLKS;
		}
		return(NO_QUOTA);
	}
	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime && CURRENT_TIME >= dquot->dq_btime && !fsuser()) {
                if (need_print_warning(type, dquot)) {
			sprintf(quotamessage, "%s: write failed, %s disk quota exceeded too long.\r\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(current->tty, quotamessage);
		}
		return(NO_QUOTA);
	}
	if (dquot->dq_bsoftlimit &&
	   (dquot->dq_curblocks + blocks) > dquot->dq_bsoftlimit &&
	    dquot->dq_btime == 0 && !fsuser()) {
                if (need_print_warning(type, dquot)) {
			sprintf(quotamessage, "%s: warning, %s disk quota exceeded\r\n",
		        	dquot->dq_mnt->mnt_dirname, quotatypes[type]);
			tty_write_message(current->tty, quotamessage);
		}
		dquot->dq_btime = CURRENT_TIME + dquot->dq_mnt->mnt_bexp[type];
	}
	return(QUOTA_OK);
}

static void dqput(struct dquot *dquot)
{
	if (!dquot)
		return;
	/*
	 * If the dq_mnt pointer isn't initialized this entry needs no
	 * checking and doesn't need to be written. It just an empty
	 * dquot that is put back into the freelist.
	 */
	if (dquot->dq_mnt != (struct vfsmount *)NULL) {
		dqstats.drops++;
		wait_on_dquot(dquot);
		if (!dquot->dq_count) {
			printk("VFS: dqput: trying to free free dquot\n");
			printk("VFS: device %s, dquot of %s %d\n", kdevname(dquot->dq_dev),
			       quotatypes[dquot->dq_type], dquot->dq_id);
			return;
		}
repeat:
		if (dquot->dq_count > 1) {
			dquot->dq_count--;
			return;
		}
		wake_up(&dquot_wait);
		if (dquot->dq_flags & DQ_MOD) {
			write_dquot(dquot);	/* we can sleep - so do again */
			wait_on_dquot(dquot);
			goto repeat;
		}
	}
	if (dquot->dq_count) {
		dquot->dq_count--;
		nr_free_dquots++;
	}
	return;
}

static struct dquot *get_empty_dquot(void)
{
	struct dquot *dquot, *best;
	int cnt;

	if (nr_dquots < NR_DQUOTS && nr_free_dquots < (nr_dquots >> 2))
		grow_dquots();

repeat:
	dquot = first_dquot;
	best = NODQUOT;
	for (cnt = 0; cnt < nr_dquots; dquot = dquot->dq_next, cnt++) {
		if (!dquot->dq_count) {
			if (!best)
				best = dquot;
			if (!(dquot->dq_flags & DQ_MOD) && !(dquot->dq_flags & DQ_LOCKED)) {
				best = dquot;
				break;
			}
		}
	}
	if (!best || best->dq_flags & DQ_MOD || best->dq_flags & DQ_LOCKED)
		if (nr_dquots < NR_DQUOTS) {
			grow_dquots();
			goto repeat;
		}
	dquot = best;
	if (!dquot) {
		printk("VFS: No free dquots - contact mvw@mcs.ow.org\n");
		sleep_on(&dquot_wait);
		goto repeat;
	}
	if (dquot->dq_flags & DQ_LOCKED) {
		wait_on_dquot(dquot);
		goto repeat;
	}
	if (dquot->dq_flags & DQ_MOD) {
		write_dquot(dquot);
		goto repeat;
	}
	if (dquot->dq_count)
		goto repeat;
	clear_dquot(dquot);
	dquot->dq_count = 1;
	nr_free_dquots--;
	if (nr_free_dquots < 0) {
		printk ("VFS: get_empty_dquot: bad free dquot count.\n");
		nr_free_dquots = 0;
	}
	return(dquot);
}

static struct dquot *dqget(kdev_t dev, unsigned int id, short type)
{
	struct dquot *dquot, *empty;
	struct vfsmount *vfsmnt;

	if ((vfsmnt = lookup_vfsmnt(dev)) == (struct vfsmount *)NULL ||
	    (vfsmnt->mnt_quotas[type] == (struct file *)0))
		return(NODQUOT);
	dqstats.lookups++;
	empty = get_empty_dquot();
repeat:
	dquot = *(hash(dev, id, type));
	while (dquot) {
		if (dquot->dq_dev != dev || dquot->dq_id != id ||
		    dquot->dq_type != type) {
			dquot = dquot->dq_hash_next;
			continue;
		}
		wait_on_dquot(dquot);
		if (dquot->dq_dev != dev || dquot->dq_id != id ||
		    dquot->dq_type != type)
			goto repeat;
		if (!dquot->dq_count)
			nr_free_dquots--;
		dquot->dq_count++;
		if (empty)
			dqput(empty);
		dqstats.cache_hits++;
		return(dquot);
	}
	if (!empty)
		return(NODQUOT);
	dquot = empty;
	dquot->dq_id = id;
	dquot->dq_type = type;
	dquot->dq_dev = dev;
	dquot->dq_mnt = vfsmnt;
	put_last_free(dquot);
	insert_dquot_hash(dquot);
	read_dquot(dquot);
	return(dquot);
}

/*
 * Initialize a dquot-struct with new quota info. This is used by the
 * systemcall interface functions.
 */ 
static int set_dqblk(kdev_t dev, int id, short type, int flags, struct dqblk *dqblk)
{
	struct dquot *dquot;
	struct dqblk dq_dqblk;

	if (dqblk == (struct dqblk *)NULL)
		return(-EFAULT);

	if (flags & QUOTA_SYSCALL) {
		if (copy_from_user(&dq_dqblk, dqblk, sizeof(struct dqblk)))
			return -EFAULT;	
	} else {
		memcpy(&dq_dqblk, dqblk, sizeof(struct dqblk));
	}
	if ((dquot = dqget(dev, id, type)) != NODQUOT) {
		lock_dquot(dquot);
		if (id > 0 && (flags & (SET_QUOTA|SET_QLIMIT))) {
			dquot->dq_bhardlimit = dq_dqblk.dqb_bhardlimit;
			dquot->dq_bsoftlimit = dq_dqblk.dqb_bsoftlimit;
			dquot->dq_ihardlimit = dq_dqblk.dqb_ihardlimit;
			dquot->dq_isoftlimit = dq_dqblk.dqb_isoftlimit;
		}
		if (flags & (SET_QUOTA|SET_USE)) {
			if (dquot->dq_isoftlimit &&
			    dquot->dq_curinodes < dquot->dq_isoftlimit &&
			    dq_dqblk.dqb_curinodes >= dquot->dq_isoftlimit)
				dquot->dq_itime = CURRENT_TIME + dquot->dq_mnt->mnt_iexp[type];
			dquot->dq_curinodes = dq_dqblk.dqb_curinodes;
			if (dquot->dq_curinodes < dquot->dq_isoftlimit)
				dquot->dq_flags &= ~DQ_INODES;
			if (dquot->dq_bsoftlimit &&
			    dquot->dq_curblocks < dquot->dq_bsoftlimit &&
			    dq_dqblk.dqb_curblocks >= dquot->dq_bsoftlimit)
				dquot->dq_btime = CURRENT_TIME + dquot->dq_mnt->mnt_bexp[type];
			dquot->dq_curblocks = dq_dqblk.dqb_curblocks;
			if (dquot->dq_curblocks < dquot->dq_bsoftlimit)
				dquot->dq_flags &= ~DQ_BLKS;
		}
		if (id == 0) {
			/* 
			 * Change in expiretimes, change them in dq_mnt.
			 */
			dquot->dq_mnt->mnt_bexp[type] = dquot->dq_btime = dq_dqblk.dqb_btime;
			dquot->dq_mnt->mnt_iexp[type] = dquot->dq_itime = dq_dqblk.dqb_itime;
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
	int error;

	if (has_quota_enabled(dev, type)) {
		if (dqblk == (struct dqblk *)NULL)
			return(-EFAULT);

		if ((dquot = dqget(dev, id, type)) != NODQUOT) {
			error = copy_to_user(dqblk, (char *)&dquot->dq_dqb, sizeof(struct dqblk));
			dqput(dquot);
			return error ? -EFAULT : 0;
		}
	}
	return(-ESRCH);
}

static int get_stats(caddr_t addr)
{
	dqstats.allocated_dquots = nr_dquots;
	dqstats.free_dquots = nr_free_dquots;
	return copy_to_user(addr, (caddr_t)&dqstats, sizeof(struct dqstats)) 
		? -EFAULT : 0; 
}

/*
 * Initialize pointer in an inode to the right dquots.
 */
void dquot_initialize(struct inode *inode, short type)
{
	unsigned int id = 0;
	short cnt;
	struct dquot *tmp;

	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)) {
		for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
			if (type != -1 && cnt != type)
				continue;
			if (!has_quota_enabled(inode->i_dev, cnt))
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

				tmp = dqget(inode->i_dev, id, cnt);
				/* We may sleep in dqget(), so check it again.
				 * 	Dmitry Gorodchanin 02/11/96
				 */
				if (inode->i_dquot[cnt] != NODQUOT) {
					dqput(tmp);
					continue;
				} 
				inode->i_dquot[cnt] = tmp;
				inode->i_flags |= S_WRITE;
			}
		}
	}
}

void dquot_drop(struct inode *inode)
{
	short cnt;
	struct dquot * tmp;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		/* We can sleep at dqput(). So we must do it this way.
		 * 	Dmitry Gorodchanin 02/11/96
		 */
		tmp = inode->i_dquot[cnt];
		inode->i_dquot[cnt] = NODQUOT;
		dqput(tmp);
	}
	inode->i_flags &= ~S_WRITE;
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
 * Externally referenced functions through dquot_operations.
 */
int dquot_alloc_block(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		if (check_bdq(inode->i_dquot[cnt], cnt, number))
			return(NO_QUOTA);
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_blocks(inode->i_dquot[cnt], number);
	}
	return(QUOTA_OK);
}

int dquot_alloc_inode(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		if (check_idq(inode->i_dquot[cnt], cnt, number))
			return(NO_QUOTA);
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_incr_inodes(inode->i_dquot[cnt], number);
	}
	return(QUOTA_OK);
}

void dquot_free_block(const struct inode *inode, unsigned long number)
{
	unsigned short cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt] == NODQUOT)
			continue;
		dquot_decr_blocks(inode->i_dquot[cnt], number);
	}
}

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
 */
int dquot_transfer(struct inode *inode, struct iattr *iattr, char direction)
{
	unsigned long blocks;
	struct dquot *transfer_from[MAXQUOTAS];
	struct dquot *transfer_to[MAXQUOTAS];
	short cnt, disc;

	/*
	 * Find out if this filesystems uses i_blocks.
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

		if (!has_quota_enabled(inode->i_dev, cnt))
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

		if (check_idq(transfer_to[cnt], cnt, 1) == NO_QUOTA ||
		    check_bdq(transfer_to[cnt], cnt, blocks) == NO_QUOTA) {
			for (disc = 0; disc <= cnt; disc++) {
				dqput(transfer_from[disc]);
				dqput(transfer_to[disc]);
			}
			return(NO_QUOTA);
		}
	}

	/*
	 * Finally perform the needed transfer from transfer_from to transfer_to.
	 * And release any pointer to dquots not needed anymore.
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
			dqput(transfer_from[cnt]);
			dqput(inode->i_dquot[cnt]);
			inode->i_dquot[cnt] = transfer_to[cnt];
		} else {
			dqput(transfer_from[cnt]);
			dqput(transfer_to[cnt]);
		}
	}
	return(QUOTA_OK);
}

void dquot_init(void)
{
	printk(KERN_NOTICE "VFS: Diskquotas version %s initialized\r\n",
	       __DQUOT_VERSION__);
	memset(hash_table, 0, sizeof(hash_table));
	memset((caddr_t)&dqstats, 0, sizeof(dqstats));
	first_dquot = NODQUOT;
}

/*
 * Definitions of diskquota operations.
 */
struct dquot_operations dquot_operations = {
	dquot_initialize,
	dquot_drop,
	dquot_alloc_block,
	dquot_alloc_inode,
	dquot_free_block,
	dquot_free_inode,
	dquot_transfer
};

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int quota_off(kdev_t dev, short type)
{
	struct vfsmount *vfsmnt;
	short cnt;

	if ( !(vfsmnt = lookup_vfsmnt(dev)) )
		return -ENODEV;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (vfsmnt->mnt_quotas[cnt] == (struct file *)NULL)
		{
			if(type == -1)
				continue;
			return -ESRCH;
		}
		vfsmnt->mnt_sb->dq_op = (struct dquot_operations *)NULL;
		reset_dquot_ptrs(dev, cnt);
		invalidate_dquots(dev, cnt);
		fput(vfsmnt->mnt_quotas[cnt]);
		vfsmnt->mnt_quotas[cnt] = (struct file *)NULL;
		vfsmnt->mnt_iexp[cnt] = vfsmnt->mnt_bexp[cnt] = (time_t)NULL;
	}
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
	if (vfsmnt == NULL)
		return -ENODEV;

	if (vfsmnt->mnt_quotas[type] != NULL)
		return -EBUSY;

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

	filp = get_empty_filp();
	if (filp != NULL) {
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
					vfsmnt->mnt_quotas[type] = filp;
					dquot = dqget(dev, 0, type);
					vfsmnt->mnt_iexp[type] = (dquot) ? dquot->dq_itime : MAX_IQ_TIME;
					vfsmnt->mnt_bexp[type] = (dquot) ? dquot->dq_btime : MAX_DQ_TIME;
					dqput(dquot);
					vfsmnt->mnt_sb->dq_op = &dquot_operations;
					add_dquot_ref(dev, type);
					return 0;
				}
				put_write_access(inode);
			}
		} else
			error = -EIO;
		put_filp(filp);
	} else
		error = -EMFILE;
	dput(dentry);
	return error;
}

/*
 * Ok this is the systemcall interface, this communicates with
 * the userlevel programs. Currently this only supports diskquota
 * calls. Maybe we need to add the process quotas etc in the future.
 * But we probably better use rlimits for that.
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
			     (type == GRPQUOTA && in_group_p(id))) && !fsuser())
				goto out;
			break;
		default:
			if (!fsuser())
				goto out;
	}

	ret = -EINVAL;
	dev = NODEV;
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
		default:
			goto out;
	}

	flags |= QUOTA_SYSCALL;
	if (has_quota_enabled(dev, type))
		ret = set_dqblk(dev, id, type, flags, (struct dqblk *) addr);
	else
		ret = -ESRCH;
out:
	unlock_kernel();
	return ret;
}
