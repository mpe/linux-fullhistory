/*
 * raid5.c : Multiple Devices driver for Linux
 *	   Copyright (C) 1996, 1997 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *	   Copyright (C) 1999, 2000 Ingo Molnar
 *
 * RAID-5 management functions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/locks.h>
#include <linux/malloc.h>
#include <linux/raid/raid5.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

static mdk_personality_t raid5_personality;

/*
 * Stripe cache
 */

#define NR_STRIPES		128
#define HASH_PAGES		1
#define HASH_PAGES_ORDER	0
#define NR_HASH			(HASH_PAGES * PAGE_SIZE / sizeof(struct stripe_head *))
#define HASH_MASK		(NR_HASH - 1)
#define stripe_hash(conf, sect, size)	((conf)->stripe_hashtbl[((sect) / (size >> 9)) & HASH_MASK])

/*
 * The following can be used to debug the driver
 */
#define RAID5_DEBUG	0
#define RAID5_PARANOIA	1
#if RAID5_PARANOIA && CONFIG_SMP
# define CHECK_DEVLOCK() if (!spin_is_locked(&conf->device_lock)) BUG()
# define CHECK_SHLOCK(sh) if (!stripe_locked(sh)) BUG()
#else
# define CHECK_DEVLOCK()
# define CHECK_SHLOCK(unused)
#endif

#if RAID5_DEBUG
#define PRINTK(x...) printk(x)
#define inline
#define __inline__
#else
#define PRINTK(x...) do { } while (0)
#endif

static void print_raid5_conf (raid5_conf_t *conf);

static inline int stripe_locked(struct stripe_head *sh)
{
	return test_bit(STRIPE_LOCKED, &sh->state);
}

static void __unlock_stripe(struct stripe_head *sh)
{
	if (!md_test_and_clear_bit(STRIPE_LOCKED, &sh->state))
		BUG();
	PRINTK("unlocking stripe %lu\n", sh->sector);
	wake_up(&sh->wait);
}

static void finish_unlock_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	sh->cmd = STRIPE_NONE;
	sh->phase = PHASE_COMPLETE;
	atomic_dec(&conf->nr_pending_stripes);
	atomic_inc(&conf->nr_cached_stripes);
	__unlock_stripe(sh);
	atomic_dec(&sh->count);
	wake_up(&conf->wait_for_stripe);
}

static void remove_hash(raid5_conf_t *conf, struct stripe_head *sh)
{
	PRINTK("remove_hash(), stripe %lu\n", sh->sector);

	CHECK_DEVLOCK();
	CHECK_SHLOCK(sh);
	if (sh->hash_pprev) {
		if (sh->hash_next)
			sh->hash_next->hash_pprev = sh->hash_pprev;
		*sh->hash_pprev = sh->hash_next;
		sh->hash_pprev = NULL;
		atomic_dec(&conf->nr_hashed_stripes);
	}
}

static void lock_get_bh (struct buffer_head *bh)
{
	while (md_test_and_set_bit(BH_Lock, &bh->b_state))
		__wait_on_buffer(bh);
	atomic_inc(&bh->b_count);
}

static __inline__ void insert_hash(raid5_conf_t *conf, struct stripe_head *sh)
{
	struct stripe_head **shp = &stripe_hash(conf, sh->sector, sh->size);

	PRINTK("insert_hash(), stripe %lu, nr_hashed_stripes %d\n",
			sh->sector, atomic_read(&conf->nr_hashed_stripes));

	CHECK_DEVLOCK();
	CHECK_SHLOCK(sh);
	if ((sh->hash_next = *shp) != NULL)
		(*shp)->hash_pprev = &sh->hash_next;
	*shp = sh;
	sh->hash_pprev = shp;
	atomic_inc(&conf->nr_hashed_stripes);
}

static struct buffer_head *get_free_buffer(struct stripe_head *sh, int b_size)
{
	struct buffer_head *bh;
	unsigned long flags;

	CHECK_SHLOCK(sh);
	md_spin_lock_irqsave(&sh->stripe_lock, flags);
	bh = sh->buffer_pool;
	if (!bh)
		goto out_unlock;
	sh->buffer_pool = bh->b_next;
	bh->b_size = b_size;
	if (atomic_read(&bh->b_count))
		BUG();
out_unlock:
	md_spin_unlock_irqrestore(&sh->stripe_lock, flags);

	return bh;
}

static struct buffer_head *get_free_bh(struct stripe_head *sh)
{
	struct buffer_head *bh;
	unsigned long flags;

	CHECK_SHLOCK(sh);
	md_spin_lock_irqsave(&sh->stripe_lock, flags);
	bh = sh->bh_pool;
	if (!bh)
		goto out_unlock;
	sh->bh_pool = bh->b_next;
	if (atomic_read(&bh->b_count))
		BUG();
out_unlock:
	md_spin_unlock_irqrestore(&sh->stripe_lock, flags);

	return bh;
}

static void put_free_buffer(struct stripe_head *sh, struct buffer_head *bh)
{
	unsigned long flags;

	if (atomic_read(&bh->b_count))
		BUG();
	CHECK_SHLOCK(sh);
	md_spin_lock_irqsave(&sh->stripe_lock, flags);
	bh->b_next = sh->buffer_pool;
	sh->buffer_pool = bh;
	md_spin_unlock_irqrestore(&sh->stripe_lock, flags);
}

static void put_free_bh(struct stripe_head *sh, struct buffer_head *bh)
{
	unsigned long flags;

	if (atomic_read(&bh->b_count))
		BUG();
	CHECK_SHLOCK(sh);
	md_spin_lock_irqsave(&sh->stripe_lock, flags);
	bh->b_next = sh->bh_pool;
	sh->bh_pool = bh;
	md_spin_unlock_irqrestore(&sh->stripe_lock, flags);
}

static struct stripe_head *get_free_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;

	md_spin_lock_irq(&conf->device_lock);
	sh = conf->free_sh_list;
	if (!sh)
		goto out;
	conf->free_sh_list = sh->free_next;
	atomic_dec(&conf->nr_free_sh);
	if (!atomic_read(&conf->nr_free_sh) && conf->free_sh_list)
		BUG();
	if (sh->hash_pprev || md_atomic_read(&sh->nr_pending) ||
					 atomic_read(&sh->count))
		BUG();
out:
	md_spin_unlock_irq(&conf->device_lock);
	return sh;
}

static void __put_free_stripe (raid5_conf_t *conf, struct stripe_head *sh)
{
	if (atomic_read(&sh->count) != 0)
		BUG();
	CHECK_DEVLOCK();
	CHECK_SHLOCK(sh);
	clear_bit(STRIPE_LOCKED, &sh->state);
	sh->free_next = conf->free_sh_list;
	conf->free_sh_list = sh;
	atomic_inc(&conf->nr_free_sh);
}

static void shrink_buffers(struct stripe_head *sh, int num)
{
	struct buffer_head *bh;

	while (num--) {
		bh = get_free_buffer(sh, -1);
		if (!bh)
			return;
		free_page((unsigned long) bh->b_data);
		kfree(bh);
	}
}

static void shrink_bh(struct stripe_head *sh, int num)
{
	struct buffer_head *bh;

	while (num--) {
		bh = get_free_bh(sh);
		if (!bh)
			return;
		kfree(bh);
	}
}

static int grow_raid5_buffers(struct stripe_head *sh, int num, int b_size, int priority)
{
	struct buffer_head *bh;

	while (num--) {
		struct page *page;
		bh = kmalloc(sizeof(struct buffer_head), priority);
		if (!bh)
			return 1;
		memset(bh, 0, sizeof (struct buffer_head));
		init_waitqueue_head(&bh->b_wait);
		page = alloc_page(priority);
		bh->b_data = (char *) page_address(page);
		if (!bh->b_data) {
			kfree(bh);
			return 1;
		}
		bh->b_size = b_size;
		atomic_set(&bh->b_count, 0);
		bh->b_page = page;
		put_free_buffer(sh, bh);
	}
	return 0;
}

static int grow_bh(struct stripe_head *sh, int num, int priority)
{
	struct buffer_head *bh;

	while (num--) {
		bh = kmalloc(sizeof(struct buffer_head), priority);
		if (!bh)
			return 1;
		memset(bh, 0, sizeof (struct buffer_head));
		init_waitqueue_head(&bh->b_wait);
		put_free_bh(sh, bh);
	}
	return 0;
}

static void raid5_free_buffer(struct stripe_head *sh, struct buffer_head *bh)
{
	put_free_buffer(sh, bh);
}

static void raid5_free_bh(struct stripe_head *sh, struct buffer_head *bh)
{
	put_free_bh(sh, bh);
}

static void raid5_free_old_bh(struct stripe_head *sh, int i)
{
	CHECK_SHLOCK(sh);
	if (!sh->bh_old[i])
		BUG();
	raid5_free_buffer(sh, sh->bh_old[i]);
	sh->bh_old[i] = NULL;
}

static void raid5_update_old_bh(struct stripe_head *sh, int i)
{
	CHECK_SHLOCK(sh);
	PRINTK("stripe %lu, idx %d, updating cache copy\n", sh->sector, i);
	if (!sh->bh_copy[i])
		BUG();
	if (sh->bh_old[i])
		raid5_free_old_bh(sh, i);
	sh->bh_old[i] = sh->bh_copy[i];
	sh->bh_copy[i] = NULL;
}

static void free_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	int disks = conf->raid_disks, j;

	if (atomic_read(&sh->count) != 0)
		BUG();
	CHECK_DEVLOCK();
	CHECK_SHLOCK(sh);
	PRINTK("free_stripe called, stripe %lu\n", sh->sector);
	if (sh->phase != PHASE_COMPLETE || atomic_read(&sh->count)) {
		PRINTK("raid5: free_stripe(), sector %lu, phase %d, count %d\n", sh->sector, sh->phase, atomic_read(&sh->count));
		return;
	}
	for (j = 0; j < disks; j++) {
		if (sh->bh_old[j])
			raid5_free_old_bh(sh, j);
		if (sh->bh_new[j] || sh->bh_copy[j])
			BUG();
	}
	remove_hash(conf, sh);
	__put_free_stripe(conf, sh);
}

static int shrink_stripe_cache(raid5_conf_t *conf, int nr)
{
	struct stripe_head *sh;
	int i, count = 0;

	PRINTK("shrink_stripe_cache called, %d/%d, clock %d\n", nr, atomic_read(&conf->nr_hashed_stripes), conf->clock);
	md_spin_lock_irq(&conf->device_lock);
	for (i = 0; i < NR_HASH; i++) {
		sh = conf->stripe_hashtbl[(i + conf->clock) & HASH_MASK];
		for (; sh; sh = sh->hash_next) {
			if (sh->phase != PHASE_COMPLETE)
				continue;
			if (atomic_read(&sh->count))
				continue;
			/*
			 * Try to lock this stripe:
			 */
			if (md_test_and_set_bit(STRIPE_LOCKED, &sh->state))
				continue;
			free_stripe(sh);
			if (++count == nr) {
				conf->clock = (i + conf->clock) & HASH_MASK;
				goto out;
			}
		}
	}
out:
	md_spin_unlock_irq(&conf->device_lock);
	PRINTK("shrink completed, nr_hashed_stripes %d, nr_pending_strips %d\n",
		atomic_read(&conf->nr_hashed_stripes),
		atomic_read(&conf->nr_pending_stripes));
	return count;
}

void __wait_lock_stripe(struct stripe_head *sh)
{
	MD_DECLARE_WAITQUEUE(wait, current);

	PRINTK("wait_lock_stripe %lu\n", sh->sector);
	if (!atomic_read(&sh->count))
		BUG();
	add_wait_queue(&sh->wait, &wait);
repeat:
	set_current_state(TASK_UNINTERRUPTIBLE);
	if (md_test_and_set_bit(STRIPE_LOCKED, &sh->state)) {
		schedule();
		goto repeat;
	}
	PRINTK("wait_lock_stripe %lu done\n", sh->sector);
	remove_wait_queue(&sh->wait, &wait);
	current->state = TASK_RUNNING;
}

static struct stripe_head *__find_stripe(raid5_conf_t *conf, unsigned long sector, int size)
{
	struct stripe_head *sh;

	PRINTK("__find_stripe, sector %lu\n", sector);
	for (sh = stripe_hash(conf, sector, size); sh; sh = sh->hash_next) {
		if (sh->sector == sector && sh->raid_conf == conf) {
			if (sh->size != size)
				BUG();
			return sh;
		}
	}
	PRINTK("__stripe %lu not in cache\n", sector);
	return NULL;
}

static inline struct stripe_head *alloc_stripe(raid5_conf_t *conf, unsigned long sector, int size)
{
	struct stripe_head *sh;
	struct buffer_head *buffer_pool, *bh_pool;
	MD_DECLARE_WAITQUEUE(wait, current);

	PRINTK("alloc_stripe called\n");

	
	while ((sh = get_free_stripe(conf)) == NULL) {
		int cnt;
		add_wait_queue(&conf->wait_for_stripe, &wait);
		set_current_state(TASK_UNINTERRUPTIBLE);
		cnt = shrink_stripe_cache(conf, conf->max_nr_stripes / 8);
		sh = get_free_stripe(conf);
		if (!sh && cnt < (conf->max_nr_stripes/8)) {
			md_wakeup_thread(conf->thread);
			PRINTK("waiting for some stripes to complete - %d %d\n", cnt, conf->max_nr_stripes/8);
			schedule();
		}
		remove_wait_queue(&conf->wait_for_stripe, &wait);
		current->state = TASK_RUNNING;
		if (sh)
			break;
	}

	buffer_pool = sh->buffer_pool;
	bh_pool = sh->bh_pool;
	memset(sh, 0, sizeof(*sh));
	sh->stripe_lock = MD_SPIN_LOCK_UNLOCKED;
	md_init_waitqueue_head(&sh->wait);
	sh->buffer_pool = buffer_pool;
	sh->bh_pool = bh_pool;
	sh->phase = PHASE_COMPLETE;
	sh->cmd = STRIPE_NONE;
	sh->raid_conf = conf;
	sh->sector = sector;
	sh->size = size;
	atomic_inc(&conf->nr_cached_stripes);

	return sh;
}

static struct stripe_head *get_lock_stripe(raid5_conf_t *conf, unsigned long sector, int size)
{
	struct stripe_head *sh, *new = NULL;

	PRINTK("get_stripe, sector %lu\n", sector);

	/*
	 * Do this in set_blocksize()!
	 */
	if (conf->buffer_size != size) {
		PRINTK("switching size, %d --> %d\n", conf->buffer_size, size);
		shrink_stripe_cache(conf, conf->max_nr_stripes);
		conf->buffer_size = size;
	}

repeat:
	md_spin_lock_irq(&conf->device_lock);
	sh = __find_stripe(conf, sector, size);
	if (!sh) {
		if (!new) {
			md_spin_unlock_irq(&conf->device_lock);
			new = alloc_stripe(conf, sector, size);
			goto repeat;
		}
		sh = new;
		new = NULL;
		if (md_test_and_set_bit(STRIPE_LOCKED, &sh->state))
			BUG();
		insert_hash(conf, sh);
		atomic_inc(&sh->count);
		md_spin_unlock_irq(&conf->device_lock);
	} else {
		atomic_inc(&sh->count);
		if (new) {
			if (md_test_and_set_bit(STRIPE_LOCKED, &new->state))
				BUG();
			__put_free_stripe(conf, new);
		}
		md_spin_unlock_irq(&conf->device_lock);
		PRINTK("get_stripe, waiting, sector %lu\n", sector);
		if (md_test_and_set_bit(STRIPE_LOCKED, &sh->state))
			__wait_lock_stripe(sh);
	}
	return sh;
}

static int grow_stripes(raid5_conf_t *conf, int num, int priority)
{
	struct stripe_head *sh;

	while (num--) {
		sh = kmalloc(sizeof(struct stripe_head), priority);
		if (!sh)
			return 1;
		memset(sh, 0, sizeof(*sh));
		sh->raid_conf = conf;
		sh->stripe_lock = MD_SPIN_LOCK_UNLOCKED;
		md_init_waitqueue_head(&sh->wait);

		if (md_test_and_set_bit(STRIPE_LOCKED, &sh->state))
			BUG();
		if (grow_raid5_buffers(sh, 2 * conf->raid_disks, PAGE_SIZE, priority)) {
			shrink_buffers(sh, 2 * conf->raid_disks);
			kfree(sh);
			return 1;
		}
		if (grow_bh(sh, conf->raid_disks, priority)) {
			shrink_buffers(sh, 2 * conf->raid_disks);
			shrink_bh(sh, conf->raid_disks);
			kfree(sh);
			return 1;
		}
		md_spin_lock_irq(&conf->device_lock);
		__put_free_stripe(conf, sh);
		atomic_inc(&conf->nr_stripes);
		md_spin_unlock_irq(&conf->device_lock);
	}
	return 0;
}

static void shrink_stripes(raid5_conf_t *conf, int num)
{
	struct stripe_head *sh;

	while (num--) {
		sh = get_free_stripe(conf);
		if (!sh)
			break;
		if (md_test_and_set_bit(STRIPE_LOCKED, &sh->state))
			BUG();
		shrink_buffers(sh, conf->raid_disks * 2);
		shrink_bh(sh, conf->raid_disks);
		kfree(sh);
		atomic_dec(&conf->nr_stripes);
	}
}


static struct buffer_head *raid5_alloc_buffer(struct stripe_head *sh, int b_size)
{
	struct buffer_head *bh;

	bh = get_free_buffer(sh, b_size);
	if (!bh)
		BUG();
	return bh;
}

static struct buffer_head *raid5_alloc_bh(struct stripe_head *sh)
{
	struct buffer_head *bh;

	bh = get_free_bh(sh);
	if (!bh)
		BUG();
	return bh;
}

static void raid5_end_buffer_io (struct stripe_head *sh, int i, int uptodate)
{
	struct buffer_head *bh = sh->bh_new[i];

	PRINTK("raid5_end_buffer_io %lu, uptodate: %d.\n", bh->b_blocknr, uptodate);
	sh->bh_new[i] = NULL;
	raid5_free_bh(sh, sh->bh_req[i]);
	sh->bh_req[i] = NULL;
	PRINTK("calling %p->end_io: %p.\n", bh, bh->b_end_io);
	bh->b_end_io(bh, uptodate);
	if (!uptodate)
		printk(KERN_ALERT "raid5: %s: unrecoverable I/O error for "
			"block %lu\n",
			partition_name(mddev_to_kdev(sh->raid_conf->mddev)),
			bh->b_blocknr);
}

static inline void raid5_mark_buffer_uptodate (struct buffer_head *bh, int uptodate)
{
	if (uptodate)
		set_bit(BH_Uptodate, &bh->b_state);
	else
		clear_bit(BH_Uptodate, &bh->b_state);
}

static void raid5_end_request (struct buffer_head * bh, int uptodate)
{
 	struct stripe_head *sh = bh->b_private;
	raid5_conf_t *conf = sh->raid_conf;
	int disks = conf->raid_disks, i;
	unsigned long flags;

	PRINTK("end_request %lu, nr_pending %d, uptodate: %d, (caller: %p,%p,%p,%p).\n", sh->sector, atomic_read(&sh->nr_pending), uptodate, __builtin_return_address(0),__builtin_return_address(1),__builtin_return_address(2), __builtin_return_address(3));
	md_spin_lock_irqsave(&sh->stripe_lock, flags);
	raid5_mark_buffer_uptodate(bh, uptodate);
	if (!uptodate)
		md_error(mddev_to_kdev(conf->mddev), bh->b_dev);
	if (conf->failed_disks) {
		for (i = 0; i < disks; i++) {
			if (conf->disks[i].operational)
				continue;
			if (bh != sh->bh_old[i] && bh != sh->bh_req[i] && bh != sh->bh_copy[i])
				continue;
			if (bh->b_dev != conf->disks[i].dev)
				continue;
			set_bit(STRIPE_ERROR, &sh->state);
		}
	}
	md_spin_unlock_irqrestore(&sh->stripe_lock, flags);

	if (atomic_dec_and_test(&sh->nr_pending)) {
		atomic_inc(&conf->nr_handle);
		md_wakeup_thread(conf->thread);
	}
}

static void raid5_build_block (struct stripe_head *sh, struct buffer_head *bh, int i)
{
	raid5_conf_t *conf = sh->raid_conf;
	char *b_data;
	struct page *b_page;
	int block = sh->sector / (sh->size >> 9);

	b_data = bh->b_data;
	b_page = bh->b_page;
	memset (bh, 0, sizeof (struct buffer_head));
	init_waitqueue_head(&bh->b_wait);
	init_buffer(bh, raid5_end_request, sh);
	bh->b_dev = conf->disks[i].dev;
	bh->b_blocknr = block;

	bh->b_data = b_data;
	bh->b_page = b_page;

	bh->b_rdev	= conf->disks[i].dev;
	bh->b_rsector	= sh->sector;

	bh->b_state	= (1 << BH_Req) | (1 << BH_Mapped);
	bh->b_size	= sh->size;
	bh->b_list	= BUF_LOCKED;
}

static int raid5_error (mddev_t *mddev, kdev_t dev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	mdp_super_t *sb = mddev->sb;
	struct disk_info *disk;
	int i;

	PRINTK("raid5_error called\n");
	conf->resync_parity = 0;
	for (i = 0, disk = conf->disks; i < conf->raid_disks; i++, disk++) {
		if (disk->dev == dev && disk->operational) {
			disk->operational = 0;
			mark_disk_faulty(sb->disks+disk->number);
			mark_disk_nonsync(sb->disks+disk->number);
			mark_disk_inactive(sb->disks+disk->number);
			sb->active_disks--;
			sb->working_disks--;
			sb->failed_disks++;
			mddev->sb_dirty = 1;
			conf->working_disks--;
			conf->failed_disks++;
			md_wakeup_thread(conf->thread);
			printk (KERN_ALERT
				"raid5: Disk failure on %s, disabling device."
				" Operation continuing on %d devices\n",
				partition_name (dev), conf->working_disks);
			return 0;
		}
	}
	/*
	 * handle errors in spares (during reconstruction)
	 */
	if (conf->spare) {
		disk = conf->spare;
		if (disk->dev == dev) {
			printk (KERN_ALERT
				"raid5: Disk failure on spare %s\n",
				partition_name (dev));
			if (!conf->spare->operational) {
				MD_BUG();
				return -EIO;
			}
			disk->operational = 0;
			disk->write_only = 0;
			conf->spare = NULL;
			mark_disk_faulty(sb->disks+disk->number);
			mark_disk_nonsync(sb->disks+disk->number);
			mark_disk_inactive(sb->disks+disk->number);
			sb->spare_disks--;
			sb->working_disks--;
			sb->failed_disks++;

			return 0;
		}
	}
	MD_BUG();
	return -EIO;
}	

/*
 * Input: a 'big' sector number,
 * Output: index of the data and parity disk, and the sector # in them.
 */
static unsigned long raid5_compute_sector(int r_sector, unsigned int raid_disks,
			unsigned int data_disks, unsigned int * dd_idx,
			unsigned int * pd_idx, raid5_conf_t *conf)
{
	unsigned int stripe;
	int chunk_number, chunk_offset;
	unsigned long new_sector;
	int sectors_per_chunk = conf->chunk_size >> 9;

	/* First compute the information on this sector */

	/*
	 * Compute the chunk number and the sector offset inside the chunk
	 */
	chunk_number = r_sector / sectors_per_chunk;
	chunk_offset = r_sector % sectors_per_chunk;

	/*
	 * Compute the stripe number
	 */
	stripe = chunk_number / data_disks;

	/*
	 * Compute the data disk and parity disk indexes inside the stripe
	 */
	*dd_idx = chunk_number % data_disks;

	/*
	 * Select the parity disk based on the user selected algorithm.
	 */
	if (conf->level == 4)
		*pd_idx = data_disks;
	else switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			*pd_idx = stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			*pd_idx = stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		default:
			printk ("raid5: unsupported algorithm %d\n", conf->algorithm);
	}

	/*
	 * Finally, compute the new sector number
	 */
	new_sector = stripe * sectors_per_chunk + chunk_offset;
	return new_sector;
}

static unsigned long compute_blocknr(struct stripe_head *sh, int i)
{
	raid5_conf_t *conf = sh->raid_conf;
	int raid_disks = conf->raid_disks, data_disks = raid_disks - 1;
	unsigned long new_sector = sh->sector, check;
	int sectors_per_chunk = conf->chunk_size >> 9;
	unsigned long stripe = new_sector / sectors_per_chunk;
	int chunk_offset = new_sector % sectors_per_chunk;
	int chunk_number, dummy1, dummy2, dd_idx = i;
	unsigned long r_sector, blocknr;

	switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (i < sh->pd_idx)
				i += raid_disks;
			i -= (sh->pd_idx + 1);
			break;
		default:
			printk ("raid5: unsupported algorithm %d\n", conf->algorithm);
	}

	chunk_number = stripe * data_disks + i;
	r_sector = chunk_number * sectors_per_chunk + chunk_offset;
	blocknr = r_sector / (sh->size >> 9);

	check = raid5_compute_sector (r_sector, raid_disks, data_disks, &dummy1, &dummy2, conf);
	if (check != sh->sector || dummy1 != dd_idx || dummy2 != sh->pd_idx) {
		printk("compute_blocknr: map not correct\n");
		return 0;
	}
	return blocknr;
}

static void compute_block(struct stripe_head *sh, int dd_idx)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i, count, disks = conf->raid_disks;
	struct buffer_head *bh_ptr[MAX_XOR_BLOCKS];

	PRINTK("compute_block, stripe %lu, idx %d\n", sh->sector, dd_idx);

	if (sh->bh_old[dd_idx] == NULL)
		sh->bh_old[dd_idx] = raid5_alloc_buffer(sh, sh->size);
	raid5_build_block(sh, sh->bh_old[dd_idx], dd_idx);

	memset(sh->bh_old[dd_idx]->b_data, 0, sh->size);
	bh_ptr[0] = sh->bh_old[dd_idx];
	count = 1;
	for (i = 0; i < disks; i++) {
		if (i == dd_idx)
			continue;
		if (sh->bh_old[i]) {
			bh_ptr[count++] = sh->bh_old[i];
		} else {
			printk("compute_block() %d, stripe %lu, %d not present\n", dd_idx, sh->sector, i);
		}
		if (count == MAX_XOR_BLOCKS) {
			xor_block(count, &bh_ptr[0]);
			count = 1;
		}
	}
	if (count != 1)
		xor_block(count, &bh_ptr[0]);
	raid5_mark_buffer_uptodate(sh->bh_old[dd_idx], 1);
}

static void compute_parity(struct stripe_head *sh, int method)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i, pd_idx = sh->pd_idx, disks = conf->raid_disks, count;
	struct buffer_head *bh_ptr[MAX_XOR_BLOCKS];

	PRINTK("compute_parity, stripe %lu, method %d\n", sh->sector, method);
	for (i = 0; i < disks; i++) {
		if (i == pd_idx || !sh->bh_new[i])
			continue;
		if (!sh->bh_copy[i])
			sh->bh_copy[i] = raid5_alloc_buffer(sh, sh->size);
		raid5_build_block(sh, sh->bh_copy[i], i);
		if (atomic_set_buffer_clean(sh->bh_new[i]))
			atomic_set_buffer_dirty(sh->bh_copy[i]);
		memcpy(sh->bh_copy[i]->b_data, sh->bh_new[i]->b_data, sh->size);
	}
	if (sh->bh_copy[pd_idx] == NULL) {
		sh->bh_copy[pd_idx] = raid5_alloc_buffer(sh, sh->size);
		atomic_set_buffer_dirty(sh->bh_copy[pd_idx]);
	}
	raid5_build_block(sh, sh->bh_copy[pd_idx], sh->pd_idx);

	if (method == RECONSTRUCT_WRITE) {
		memset(sh->bh_copy[pd_idx]->b_data, 0, sh->size);
		bh_ptr[0] = sh->bh_copy[pd_idx];
		count = 1;
		for (i = 0; i < disks; i++) {
			if (i == sh->pd_idx)
				continue;
			if (sh->bh_new[i]) {
				bh_ptr[count++] = sh->bh_copy[i];
			} else if (sh->bh_old[i]) {
				bh_ptr[count++] = sh->bh_old[i];
			}
			if (count == MAX_XOR_BLOCKS) {
				xor_block(count, &bh_ptr[0]);
				count = 1;
			}
		}
		if (count != 1) {
			xor_block(count, &bh_ptr[0]);
		}
	} else if (method == READ_MODIFY_WRITE) {
		memcpy(sh->bh_copy[pd_idx]->b_data, sh->bh_old[pd_idx]->b_data, sh->size);
		bh_ptr[0] = sh->bh_copy[pd_idx];
		count = 1;
		for (i = 0; i < disks; i++) {
			if (i == sh->pd_idx)
				continue;
			if (sh->bh_new[i] && sh->bh_old[i]) {
				bh_ptr[count++] = sh->bh_copy[i];
				bh_ptr[count++] = sh->bh_old[i];
			}
			if (count >= (MAX_XOR_BLOCKS - 1)) {
				xor_block(count, &bh_ptr[0]);
				count = 1;
			}
		}
		if (count != 1) {
			xor_block(count, &bh_ptr[0]);
		}
	}
	raid5_mark_buffer_uptodate(sh->bh_copy[pd_idx], 1);
}

static void add_stripe_bh (struct stripe_head *sh, struct buffer_head *bh, int dd_idx, int rw)
{
	raid5_conf_t *conf = sh->raid_conf;
	struct buffer_head *bh_req;

	PRINTK("adding bh b#%lu to stripe s#%lu\n", bh->b_blocknr, sh->sector);
	CHECK_SHLOCK(sh);
	if (sh->bh_new[dd_idx])
		BUG();

	bh_req = raid5_alloc_bh(sh);
	raid5_build_block(sh, bh_req, dd_idx);
	bh_req->b_data = bh->b_data;
	bh_req->b_page = bh->b_page;

	md_spin_lock_irq(&conf->device_lock);
	if (sh->phase == PHASE_COMPLETE && sh->cmd == STRIPE_NONE) {
		PRINTK("stripe s#%lu => PHASE_BEGIN (%s)\n", sh->sector, rw == READ ? "read" : "write");
		sh->phase = PHASE_BEGIN;
		sh->cmd = (rw == READ) ? STRIPE_READ : STRIPE_WRITE;
		atomic_inc(&conf->nr_pending_stripes);
		atomic_inc(&conf->nr_handle);
		PRINTK("# of pending stripes: %u, # of handle: %u\n", atomic_read(&conf->nr_pending_stripes), atomic_read(&conf->nr_handle));
	}
	sh->bh_new[dd_idx] = bh;
	sh->bh_req[dd_idx] = bh_req;
	sh->cmd_new[dd_idx] = rw;
	sh->new[dd_idx] = 1;
	md_spin_unlock_irq(&conf->device_lock);

	PRINTK("added bh b#%lu to stripe s#%lu, disk %d.\n", bh->b_blocknr, sh->sector, dd_idx);
}

static void complete_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	int disks = conf->raid_disks;
	int i, new = 0;
	
	PRINTK("complete_stripe %lu\n", sh->sector);
	for (i = 0; i < disks; i++) {
		if (sh->cmd == STRIPE_SYNC && sh->bh_copy[i])
			raid5_update_old_bh(sh, i);
		if (sh->cmd == STRIPE_WRITE && i == sh->pd_idx)
			raid5_update_old_bh(sh, i);
		if (sh->bh_new[i]) {
			PRINTK("stripe %lu finishes new bh, sh->new == %d\n", sh->sector, sh->new[i]);
			if (!sh->new[i]) {
#if 0
				if (sh->cmd == STRIPE_WRITE) {
					if (memcmp(sh->bh_new[i]->b_data, sh->bh_copy[i]->b_data, sh->size)) {
						printk("copy differs, %s, sector %lu ",
							test_bit(BH_Dirty, &sh->bh_new[i]->b_state) ? "dirty" : "clean",
							sh->sector);
					} else if (test_bit(BH_Dirty, &sh->bh_new[i]->b_state))
						printk("sector %lu dirty\n", sh->sector);
				}
#endif
				if (sh->cmd == STRIPE_WRITE)
					raid5_update_old_bh(sh, i);
				raid5_end_buffer_io(sh, i, 1);
				continue;
			} else
				new++;
		}
		if (new && sh->cmd == STRIPE_WRITE)
			printk("raid5: bug, completed STRIPE_WRITE with new == %d\n", new);
	}
	if (sh->cmd == STRIPE_SYNC)
		md_done_sync(conf->mddev, (sh->size>>10) - sh->sync_redone,1);
	if (!new)
		finish_unlock_stripe(sh);
	else {
		PRINTK("stripe %lu, new == %d\n", sh->sector, new);
		sh->phase = PHASE_BEGIN;
	}
}


static int is_stripe_allclean(struct stripe_head *sh, int disks)
{
	int i;

	return 0;
	for (i = 0; i < disks; i++) {
		if (sh->bh_new[i])
			if (test_bit(BH_Dirty, &sh->bh_new[i]))
				return 0;
		if (sh->bh_old[i])
			if (test_bit(BH_Dirty, &sh->bh_old[i]))
				return 0;
	}
	return 1;
}

static void handle_stripe_write (mddev_t *mddev , raid5_conf_t *conf,
	struct stripe_head *sh, int nr_write, int * operational, int disks,
	int parity, int parity_failed, int nr_cache, int nr_cache_other,
	int nr_failed_other, int nr_cache_overwrite, int nr_failed_overwrite)
{
	int i, allclean;
	request_queue_t *q;
	unsigned int block;
	struct buffer_head *bh;
	int method1 = INT_MAX, method2 = INT_MAX;

	/*
	 * Attempt to add entries :-)
	 */
	if (nr_write != disks - 1) {
		for (i = 0; i < disks; i++) {
			if (i == sh->pd_idx)
				continue;
			if (sh->bh_new[i])
				continue;
			block = (int) compute_blocknr(sh, i);
			bh = get_hash_table(mddev_to_kdev(mddev), block, sh->size);
			if (!bh)
				continue;
			if (buffer_dirty(bh) && !md_test_and_set_bit(BH_Lock, &bh->b_state)) {
				PRINTK("Whee.. sector %lu, index %d (%d) found in the buffer cache!\n", sh->sector, i, block);
				add_stripe_bh(sh, bh, i, WRITE);
				sh->new[i] = 0;
				nr_write++;
				if (sh->bh_old[i]) {
					nr_cache_overwrite++;
					nr_cache_other--;
				} else
					if (!operational[i]) {
						nr_failed_overwrite++;
						nr_failed_other--;
					}
			}
			atomic_dec(&bh->b_count);
		}
	}
	PRINTK("handle_stripe() -- begin writing, stripe %lu\n", sh->sector);
	/*
	 * Writing, need to update parity buffer.
	 *
	 * Compute the number of I/O requests in the "reconstruct
	 * write" and "read modify write" methods.
	 */
	if (!nr_failed_other)
		method1 = (disks - 1) - (nr_write + nr_cache_other);
	if (!nr_failed_overwrite && !parity_failed)
		method2 = nr_write - nr_cache_overwrite + (1 - parity);

	if (method1 == INT_MAX && method2 == INT_MAX)
		BUG();
	PRINTK("handle_stripe(), sector %lu, nr_write %d, method1 %d, method2 %d\n", sh->sector, nr_write, method1, method2);

	if (!method1 || !method2) {
		allclean = is_stripe_allclean(sh, disks);
		sh->phase = PHASE_WRITE;
		compute_parity(sh, method1 <= method2 ? RECONSTRUCT_WRITE : READ_MODIFY_WRITE);

		for (i = 0; i < disks; i++) {
			if (!operational[i] && !conf->spare && !conf->resync_parity)
				continue;
			bh = sh->bh_copy[i];
			if (i != sh->pd_idx && ((bh == NULL) ^ (sh->bh_new[i] == NULL)))
				printk("raid5: bug: bh == %p, bh_new[%d] == %p\n", bh, i, sh->bh_new[i]);
			if (i == sh->pd_idx && !bh)
				printk("raid5: bug: bh == NULL, i == pd_idx == %d\n", i);
			if (bh) {
				PRINTK("making request for buffer %d\n", i);
				lock_get_bh(bh);
				if (!operational[i] && !conf->resync_parity) {
					PRINTK("writing spare %d\n", i);
					atomic_inc(&sh->nr_pending);
					bh->b_dev = bh->b_rdev = conf->spare->dev;
					q = blk_get_queue(bh->b_rdev);
					generic_make_request(q, WRITERAW, bh);
				} else {
#if 0
					atomic_inc(&sh->nr_pending);
					bh->b_dev = bh->b_rdev = conf->disks[i].dev;
					q = blk_get_queue(bh->b_rdev);
					generic_make_request(q, WRITERAW, bh);
#else
					if (!allclean || (i==sh->pd_idx)) {
						PRINTK("writing dirty %d\n", i);
						atomic_inc(&sh->nr_pending);
						bh->b_dev = bh->b_rdev = conf->disks[i].dev;
						q = blk_get_queue(bh->b_rdev);
						generic_make_request(q, WRITERAW, bh);
					} else {
						PRINTK("not writing clean %d\n", i);
						raid5_end_request(bh, 1);
						sh->new[i] = 0;
					}
#endif
				}
				atomic_dec(&bh->b_count);
			}
		}
		PRINTK("handle_stripe() %lu, writing back %d buffers\n", sh->sector, md_atomic_read(&sh->nr_pending));
		return;
	}

	if (method1 < method2) {
		sh->write_method = RECONSTRUCT_WRITE;
		for (i = 0; i < disks; i++) {
			if (i == sh->pd_idx)
				continue;
			if (sh->bh_new[i] || sh->bh_old[i])
				continue;
			sh->bh_old[i] = raid5_alloc_buffer(sh, sh->size);
			raid5_build_block(sh, sh->bh_old[i], i);
		}
	} else {
		sh->write_method = READ_MODIFY_WRITE;
		for (i = 0; i < disks; i++) {
			if (sh->bh_old[i])
				continue;
			if (!sh->bh_new[i] && i != sh->pd_idx)
				continue;
			sh->bh_old[i] = raid5_alloc_buffer(sh, sh->size);
			raid5_build_block(sh, sh->bh_old[i], i);
		}
	}
	sh->phase = PHASE_READ_OLD;
	for (i = 0; i < disks; i++) {
		if (!sh->bh_old[i])
			continue;
		if (test_bit(BH_Uptodate, &sh->bh_old[i]->b_state))
			continue;
		lock_get_bh(sh->bh_old[i]);
		atomic_inc(&sh->nr_pending);
		sh->bh_old[i]->b_dev = sh->bh_old[i]->b_rdev = conf->disks[i].dev;
		q = blk_get_queue(sh->bh_old[i]->b_rdev);
		generic_make_request(q, READ, sh->bh_old[i]);
		atomic_dec(&sh->bh_old[i]->b_count);
	}
	PRINTK("handle_stripe() %lu, reading %d old buffers\n", sh->sector, md_atomic_read(&sh->nr_pending));
}

/*
 * Reading
 */
static void handle_stripe_read (mddev_t *mddev , raid5_conf_t *conf,
	struct stripe_head *sh, int nr_read, int * operational, int disks,
	int parity, int parity_failed, int nr_cache, int nr_cache_other,
	int nr_failed_other, int nr_cache_overwrite, int nr_failed_overwrite)
{
	int i;
	request_queue_t *q;
	int method1 = INT_MAX;

	method1 = nr_read - nr_cache_overwrite;

	PRINTK("handle_stripe(), sector %lu, nr_read %d, nr_cache %d, method1 %d\n", sh->sector, nr_read, nr_cache, method1);

	if (!method1 || (method1 == 1 && nr_cache == disks - 1)) {
		PRINTK("read %lu completed from cache\n", sh->sector);
		for (i = 0; i < disks; i++) {
			if (!sh->bh_new[i])
				continue;
			if (!sh->bh_old[i])
				compute_block(sh, i);
			memcpy(sh->bh_new[i]->b_data, sh->bh_old[i]->b_data, sh->size);
		}
		complete_stripe(sh);
		return;
	}
	if (nr_failed_overwrite) {
		sh->phase = PHASE_READ_OLD;
		for (i = 0; i < disks; i++) {
			if (sh->bh_old[i])
				continue;
			if (!operational[i])
				continue;
			sh->bh_old[i] = raid5_alloc_buffer(sh, sh->size);
			raid5_build_block(sh, sh->bh_old[i], i);
			lock_get_bh(sh->bh_old[i]);
			atomic_inc(&sh->nr_pending);
			sh->bh_old[i]->b_dev = sh->bh_old[i]->b_rdev = conf->disks[i].dev;
			q = blk_get_queue(sh->bh_old[i]->b_rdev);
			generic_make_request(q, READ, sh->bh_old[i]);
			atomic_dec(&sh->bh_old[i]->b_count);
		}
		PRINTK("handle_stripe() %lu, phase READ_OLD, pending %d buffers\n", sh->sector, md_atomic_read(&sh->nr_pending));
		return;
	}
	sh->phase = PHASE_READ;
	for (i = 0; i < disks; i++) {
		if (!sh->bh_new[i])
			continue;
		if (sh->bh_old[i]) {
			memcpy(sh->bh_new[i]->b_data, sh->bh_old[i]->b_data, sh->size);
			continue;
		}
#if RAID5_PARANOIA
		if (sh->bh_req[i] == NULL || test_bit(BH_Lock, &sh->bh_req[i]->b_state)) {
			int j;
			printk("req %d is NULL! or locked \n", i);
			for (j=0; j<disks; j++) {
				printk("%d: new=%p old=%p req=%p new=%d cmd=%d\n",
					j, sh->bh_new[j], sh->bh_old[j], sh->bh_req[j],
					sh->new[j], sh->cmd_new[j]);
			}

		}
#endif
		lock_get_bh(sh->bh_req[i]);
		atomic_inc(&sh->nr_pending);
		sh->bh_req[i]->b_dev = sh->bh_req[i]->b_rdev = conf->disks[i].dev;
		q = blk_get_queue(sh->bh_req[i]->b_rdev);
		generic_make_request(q, READ, sh->bh_req[i]);
		atomic_dec(&sh->bh_req[i]->b_count);
	}
	PRINTK("handle_stripe() %lu, phase READ, pending %d\n", sh->sector, md_atomic_read(&sh->nr_pending));
}

/*
 * Syncing
 */
static void handle_stripe_sync (mddev_t *mddev , raid5_conf_t *conf,
	struct stripe_head *sh, int * operational, int disks,
	int parity, int parity_failed, int nr_cache, int nr_cache_other,
	int nr_failed_other, int nr_cache_overwrite, int nr_failed_overwrite)
{
	request_queue_t *q;
	struct buffer_head *bh;
	int i, pd_idx;
	
	/* firstly, we want to have data from all non-failed drives
	 * in bh_old
	 */
	PRINTK("handle_stripe_sync: sec=%lu disks=%d nr_cache=%d\n", sh->sector, disks, nr_cache);	
	if ((nr_cache < disks-1) || ((nr_cache == disks-1) && !(parity_failed+nr_failed_other+nr_failed_overwrite))
		) {
		sh->phase = PHASE_READ_OLD;
		for (i = 0; i < disks; i++) {
			if (sh->bh_old[i])
				continue;
			if (!conf->disks[i].operational)
				continue;

			bh = raid5_alloc_buffer(sh, sh->size);
			sh->bh_old[i] = bh;
			raid5_build_block(sh, bh, i);
			lock_get_bh(bh);
			atomic_inc(&sh->nr_pending);
			bh->b_dev = bh->b_rdev = conf->disks[i].dev;
			q = blk_get_queue(bh->b_rdev);
			generic_make_request(q, READ, bh);
			md_sync_acct(bh->b_rdev, bh->b_size/512);
			atomic_dec(&sh->bh_old[i]->b_count);
		}
		PRINTK("handle_stripe_sync() %lu, phase READ_OLD, pending %d buffers\n", sh->sector, md_atomic_read(&sh->nr_pending));

		return;
	}
	/* now, if there is a failed drive, rebuild and write to spare */
	if (nr_cache == disks-1) {
		sh->phase = PHASE_WRITE;
		/* we can generate the missing block, which will be on the failed drive */
		for (i=0; i<disks; i++) {
			if (operational[i])
				continue;
			compute_block(sh, i);
			if (conf->spare) {
				bh = sh->bh_copy[i];
				if (bh) {
					memcpy(bh->b_data, sh->bh_old[i]->b_data, sh->size);
					set_bit(BH_Uptodate, &bh->b_state);
				} else {
					bh = sh->bh_old[i];
					sh->bh_old[i] = NULL;
					sh->bh_copy[i] = bh;
				}
				atomic_inc(&sh->nr_pending);
				lock_get_bh(bh);
				bh->b_dev = bh->b_rdev = conf->spare->dev;
				q = blk_get_queue(bh->b_rdev);
				generic_make_request(q, WRITERAW, bh);
				md_sync_acct(bh->b_rdev, bh->b_size/512);
				atomic_dec(&bh->b_count);
		PRINTK("handle_stripe_sync() %lu, phase WRITE, pending %d buffers\n", sh->sector, md_atomic_read(&sh->nr_pending));
			}
			break;
		}
		return;
	}

	/* nr_cache == disks:
	 * check parity and compute/write if needed
	 */
	
	compute_parity(sh, RECONSTRUCT_WRITE);
	pd_idx = sh->pd_idx;
	if (!memcmp(sh->bh_copy[pd_idx]->b_data, sh->bh_old[pd_idx]->b_data, sh->size)) {
		/* the parity is correct - Yay! */
		complete_stripe(sh);
	} else {
		sh->phase = PHASE_WRITE;
		bh = sh->bh_copy[pd_idx];
		atomic_set_buffer_dirty(bh);
		lock_get_bh(bh);
		atomic_inc(&sh->nr_pending);
		bh->b_dev = bh->b_rdev = conf->disks[pd_idx].dev;
		q = blk_get_queue(bh->b_rdev);
		generic_make_request(q, WRITERAW, bh);
		md_sync_acct(bh->b_rdev, bh->b_size/512);
		atomic_dec(&bh->b_count);
		PRINTK("handle_stripe_sync() %lu phase WRITE, pending %d buffers\n",
			sh->sector, md_atomic_read(&sh->nr_pending));
	}
}

/*
 * handle_stripe() is our main logic routine. Note that:
 *
 * 1.	lock_stripe() should be used whenever we can't accept additonal
 *	buffers, either during short sleeping in handle_stripe() or
 *	during io operations.
 *
 * 2.	We should be careful to set sh->nr_pending whenever we sleep,
 *	to prevent re-entry of handle_stripe() for the same sh.
 *
 * 3.	conf->failed_disks and disk->operational can be changed
 *	from an interrupt. This complicates things a bit, but it allows
 *	us to stop issuing requests for a failed drive as soon as possible.
 */
static void handle_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	mddev_t *mddev = conf->mddev;
	int disks = conf->raid_disks;
	int i, nr_read = 0, nr_write = 0, parity = 0;
	int nr_cache = 0, nr_cache_other = 0, nr_cache_overwrite = 0;
	int nr_failed_other = 0, nr_failed_overwrite = 0, parity_failed = 0;
	int operational[MD_SB_DISKS], failed_disks = conf->failed_disks;

	PRINTK("handle_stripe(), stripe %lu\n", sh->sector);
	if (!stripe_locked(sh))
		BUG();
	if (md_atomic_read(&sh->nr_pending))
		BUG();
	if (sh->phase == PHASE_COMPLETE)
		BUG();

	atomic_dec(&conf->nr_handle);

	if (md_test_and_clear_bit(STRIPE_ERROR, &sh->state)) {
		printk("raid5: restarting stripe %lu\n", sh->sector);
		sh->phase = PHASE_BEGIN;
	}

	if ((sh->cmd == STRIPE_WRITE && sh->phase == PHASE_WRITE) ||
		(sh->cmd == STRIPE_READ && sh->phase == PHASE_READ) ||
		(sh->cmd == STRIPE_SYNC && sh->phase == PHASE_WRITE)
		) {
		/*
		 * Completed
		 */
		complete_stripe(sh);
		if (sh->phase == PHASE_COMPLETE)
			return;
	}

	md_spin_lock_irq(&conf->device_lock);
	for (i = 0; i < disks; i++) {
		operational[i] = conf->disks[i].operational;
	if (i == sh->pd_idx && conf->resync_parity)
			operational[i] = 0;
	}
	failed_disks = conf->failed_disks;
	md_spin_unlock_irq(&conf->device_lock);

	/*
	 * Make this one more graceful?
	 */
	if (failed_disks > 1) {
		for (i = 0; i < disks; i++) {
			if (sh->bh_new[i]) {
				raid5_end_buffer_io(sh, i, 0);
				continue;
			}
		}
		if (sh->cmd == STRIPE_SYNC)
			md_done_sync(conf->mddev, (sh->size>>10) - sh->sync_redone,1);
		finish_unlock_stripe(sh);
		return;
	}

	PRINTK("=== stripe index START ===\n");
	for (i = 0; i < disks; i++) {
		PRINTK("disk %d, ", i);
		if (sh->bh_old[i]) {
			nr_cache++;
			PRINTK(" (old cached, %d)", nr_cache);
		}
		if (i == sh->pd_idx) {
			PRINTK(" PARITY.");
			if (sh->bh_old[i]) {
				PRINTK(" CACHED.");
				parity = 1;
			} else {
				PRINTK(" UNCACHED.");
				if (!operational[i]) {
					PRINTK(" FAILED.");
					parity_failed = 1;
				}
			}
			PRINTK("\n");
			continue;
		}
		if (!sh->bh_new[i]) {
			PRINTK(" (no new data block) ");
			if (sh->bh_old[i]) {
				PRINTK(" (but old block cached) ");
				nr_cache_other++;
			} else {
				if (!operational[i]) {
					PRINTK(" (because failed disk) ");
					nr_failed_other++;
				} else
					PRINTK(" (no old block either) ");
			}
			PRINTK("\n");
			continue;
		}
		sh->new[i] = 0;
		if (sh->cmd_new[i] == READ) {
			nr_read++;
			PRINTK(" (new READ %d)", nr_read);
		}
		if (sh->cmd_new[i] == WRITE) {
			nr_write++;
			PRINTK(" (new WRITE %d)", nr_write);
		}
		if (sh->bh_old[i]) {
			nr_cache_overwrite++;
			PRINTK(" (overwriting old %d)", nr_cache_overwrite);
		} else {
			if (!operational[i]) {
				nr_failed_overwrite++;
				PRINTK(" (overwriting failed %d)", nr_failed_overwrite);
			}
		}
		PRINTK("\n");
	}
	PRINTK("=== stripe index END ===\n");

	if (nr_write && nr_read)
		BUG();

	if (nr_write)
		handle_stripe_write(
			mddev, conf, sh, nr_write, operational, disks,
			parity, parity_failed, nr_cache, nr_cache_other,
			nr_failed_other, nr_cache_overwrite,
			nr_failed_overwrite
		);
	else if (nr_read)
		handle_stripe_read(
			mddev, conf, sh, nr_read, operational, disks,
			parity, parity_failed, nr_cache, nr_cache_other,
			nr_failed_other, nr_cache_overwrite,
			nr_failed_overwrite
		);
	else if (sh->cmd == STRIPE_SYNC)
		handle_stripe_sync(
			mddev, conf, sh, operational, disks,
			parity, parity_failed, nr_cache, nr_cache_other,
			nr_failed_other, nr_cache_overwrite, nr_failed_overwrite
		);
}


static int raid5_make_request (mddev_t *mddev, int rw, struct buffer_head * bh)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	const unsigned int raid_disks = conf->raid_disks;
	const unsigned int data_disks = raid_disks - 1;
	unsigned int dd_idx, pd_idx;
	unsigned long new_sector;

	struct stripe_head *sh;

	if (rw == READA)
		rw = READ;

	new_sector = raid5_compute_sector(bh->b_rsector,
			raid_disks, data_disks, &dd_idx, &pd_idx, conf);

	PRINTK("raid5_make_request, sector %lu\n", new_sector);
	sh = get_lock_stripe(conf, new_sector, bh->b_size);
#if 0
	if ((rw == READ && sh->cmd == STRIPE_WRITE) || (rw == WRITE && sh->cmd == STRIPE_READ)) {
		PRINTK("raid5: lock contention, rw == %d, sh->cmd == %d\n", rw, sh->cmd);
		lock_stripe(sh);
		if (!md_atomic_read(&sh->nr_pending))
			handle_stripe(sh);
		goto repeat;
	}
#endif
	sh->pd_idx = pd_idx;
	if (sh->phase != PHASE_COMPLETE && sh->phase != PHASE_BEGIN)
		PRINTK("stripe %lu catching the bus!\n", sh->sector);
	if (sh->bh_new[dd_idx])
		BUG();
	add_stripe_bh(sh, bh, dd_idx, rw);

	md_wakeup_thread(conf->thread);
	return 0;
}

/*
 * Determine correct block size for this device.
 */
unsigned int device_bsize (kdev_t dev)
{
	unsigned int i, correct_size;

	correct_size = BLOCK_SIZE;
	if (blksize_size[MAJOR(dev)]) {
		i = blksize_size[MAJOR(dev)][MINOR(dev)];
		if (i)
			correct_size = i;
	}

	return correct_size;
}

static int raid5_sync_request (mddev_t *mddev, unsigned long block_nr)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	struct stripe_head *sh;
	int sectors_per_chunk = conf->chunk_size >> 9;
	unsigned long stripe = (block_nr<<2)/sectors_per_chunk;
	int chunk_offset = (block_nr<<2) % sectors_per_chunk;
	int dd_idx, pd_idx;
	unsigned long first_sector;
	int raid_disks = conf->raid_disks;
	int data_disks = raid_disks-1;
	int redone = 0;
	int bufsize;

	if (!conf->buffer_size)
		conf->buffer_size = /* device_bsize(mddev_to_kdev(mddev))*/ PAGE_SIZE;
	bufsize = conf->buffer_size;
	/* Hmm... race on buffer_size ?? */
	redone = block_nr% (bufsize>>10);
	block_nr -= redone;
	sh = get_lock_stripe(conf, block_nr<<1, bufsize);
	first_sector = raid5_compute_sector(stripe*data_disks*sectors_per_chunk
		+ chunk_offset, raid_disks, data_disks, &dd_idx, &pd_idx, conf);
	sh->pd_idx = pd_idx;
	sh->cmd = STRIPE_SYNC;
	sh->phase = PHASE_BEGIN;
	sh->sync_redone = redone;
	atomic_inc(&conf->nr_pending_stripes);
	atomic_inc(&conf->nr_handle);
	md_wakeup_thread(conf->thread);
	return (bufsize>>10)-redone;
}

/*
 * This is our raid5 kernel thread.
 *
 * We scan the hash table for stripes which can be handled now.
 * During the scan, completed stripes are saved for us by the interrupt
 * handler, so that they will not have to wait for our next wakeup.
 */
static void raid5d (void *data)
{
	struct stripe_head *sh;
	raid5_conf_t *conf = data;
	mddev_t *mddev = conf->mddev;
	int i, handled;

	PRINTK("+++ raid5d active\n");

	handled = 0;
	md_spin_lock_irq(&conf->device_lock);
	clear_bit(THREAD_WAKEUP, &conf->thread->flags);
repeat_pass:
	if (mddev->sb_dirty) {
		md_spin_unlock_irq(&conf->device_lock);
		mddev->sb_dirty = 0;
		md_update_sb(mddev);
		md_spin_lock_irq(&conf->device_lock);
	}
	for (i = 0; i < NR_HASH; i++) {
repeat:
		sh = conf->stripe_hashtbl[i];
		for (; sh; sh = sh->hash_next) {
			if (sh->raid_conf != conf)
				continue;
			if (sh->phase == PHASE_COMPLETE)
				continue;
			if (md_atomic_read(&sh->nr_pending))
				continue;
			md_spin_unlock_irq(&conf->device_lock);
			if (!atomic_read(&sh->count))
				BUG();

			handled++;
			handle_stripe(sh);
			md_spin_lock_irq(&conf->device_lock);
			goto repeat;
		}
	}
	if (conf) {
		PRINTK("%d stripes handled, nr_handle %d\n", handled, md_atomic_read(&conf->nr_handle));
		if (test_and_clear_bit(THREAD_WAKEUP, &conf->thread->flags) &&
				md_atomic_read(&conf->nr_handle))
			goto repeat_pass;
	}
	md_spin_unlock_irq(&conf->device_lock);

	PRINTK("--- raid5d inactive\n");
}

/*
 * Private kernel thread for parity reconstruction after an unclean
 * shutdown. Reconstruction on spare drives in case of a failed drive
 * is done by the generic mdsyncd.
 */
static void raid5syncd (void *data)
{
	raid5_conf_t *conf = data;
	mddev_t *mddev = conf->mddev;

	if (!conf->resync_parity)
		return;
	if (conf->resync_parity == 2)
		return;
	down(&mddev->recovery_sem);
	if (md_do_sync(mddev,NULL)) {
		up(&mddev->recovery_sem);
		printk("raid5: resync aborted!\n");
		return;
	}
	conf->resync_parity = 0;
	up(&mddev->recovery_sem);
	printk("raid5: resync finished.\n");
}

static int __check_consistency (mddev_t *mddev, int row)
{
	raid5_conf_t *conf = mddev->private;
	kdev_t dev;
	struct buffer_head *bh[MD_SB_DISKS], *tmp = NULL;
	int i, ret = 0, nr = 0, count;
	struct buffer_head *bh_ptr[MAX_XOR_BLOCKS];

	if (conf->working_disks != conf->raid_disks)
		goto out;
	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
	tmp->b_size = 4096;
	tmp->b_page = alloc_page(GFP_KERNEL);
	tmp->b_data = (char *)page_address(tmp->b_page);
	if (!tmp->b_data)
		goto out;
	md_clear_page(tmp->b_data);
	memset(bh, 0, MD_SB_DISKS * sizeof(struct buffer_head *));
	for (i = 0; i < conf->raid_disks; i++) {
		dev = conf->disks[i].dev;
		set_blocksize(dev, 4096);
		bh[i] = bread(dev, row / 4, 4096);
		if (!bh[i])
			break;
		nr++;
	}
	if (nr == conf->raid_disks) {
		bh_ptr[0] = tmp;
		count = 1;
		for (i = 1; i < nr; i++) {
			bh_ptr[count++] = bh[i];
			if (count == MAX_XOR_BLOCKS) {
				xor_block(count, &bh_ptr[0]);
				count = 1;
			}
		}
		if (count != 1) {
			xor_block(count, &bh_ptr[0]);
		}
		if (memcmp(tmp->b_data, bh[0]->b_data, 4096))
			ret = 1;
	}
	for (i = 0; i < conf->raid_disks; i++) {
		dev = conf->disks[i].dev;
		if (bh[i]) {
			bforget(bh[i]);
			bh[i] = NULL;
		}
		fsync_dev(dev);
		invalidate_buffers(dev);
	}
	free_page((unsigned long) tmp->b_data);
out:
	if (tmp)
		kfree(tmp);
	return ret;
}

static int check_consistency (mddev_t *mddev)
{
	if (__check_consistency(mddev, 0))
/*
 * We are not checking this currently, as it's legitimate to have
 * an inconsistent array, at creation time.
 */
		return 0;

	return 0;
}

static int raid5_run (mddev_t *mddev)
{
	raid5_conf_t *conf;
	int i, j, raid_disk, memory;
	mdp_super_t *sb = mddev->sb;
	mdp_disk_t *desc;
	mdk_rdev_t *rdev;
	struct disk_info *disk;
	struct md_list_head *tmp;
	int start_recovery = 0;

	MOD_INC_USE_COUNT;

	if (sb->level != 5 && sb->level != 4) {
		printk("raid5: md%d: raid level not set to 4/5 (%d)\n", mdidx(mddev), sb->level);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

	mddev->private = kmalloc (sizeof (raid5_conf_t), GFP_KERNEL);
	if ((conf = mddev->private) == NULL)
		goto abort;
	memset (conf, 0, sizeof (*conf));
	conf->mddev = mddev;

	if ((conf->stripe_hashtbl = (struct stripe_head **) md__get_free_pages(GFP_ATOMIC, HASH_PAGES_ORDER)) == NULL)
		goto abort;
	memset(conf->stripe_hashtbl, 0, HASH_PAGES * PAGE_SIZE);

	conf->device_lock = MD_SPIN_LOCK_UNLOCKED;
	md_init_waitqueue_head(&conf->wait_for_stripe);
	PRINTK("raid5_run(md%d) called.\n", mdidx(mddev));

	ITERATE_RDEV(mddev,rdev,tmp) {
		/*
		 * This is important -- we are using the descriptor on
		 * the disk only to get a pointer to the descriptor on
		 * the main superblock, which might be more recent.
		 */
		desc = sb->disks + rdev->desc_nr;
		raid_disk = desc->raid_disk;
		disk = conf->disks + raid_disk;

		if (disk_faulty(desc)) {
			printk(KERN_ERR "raid5: disabled device %s (errors detected)\n", partition_name(rdev->dev));
			if (!rdev->faulty) {
				MD_BUG();
				goto abort;
			}
			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = rdev->dev;

			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 0;
			disk->used_slot = 1;
			continue;
		}
		if (disk_active(desc)) {
			if (!disk_sync(desc)) {
				printk(KERN_ERR "raid5: disabled device %s (not in sync)\n", partition_name(rdev->dev));
				MD_BUG();
				goto abort;
			}
			if (raid_disk > sb->raid_disks) {
				printk(KERN_ERR "raid5: disabled device %s (inconsistent descriptor)\n", partition_name(rdev->dev));
				continue;
			}
			if (disk->operational) {
				printk(KERN_ERR "raid5: disabled device %s (device %d already operational)\n", partition_name(rdev->dev), raid_disk);
				continue;
			}
			printk(KERN_INFO "raid5: device %s operational as raid disk %d\n", partition_name(rdev->dev), raid_disk);
	
			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = rdev->dev;
			disk->operational = 1;
			disk->used_slot = 1;

			conf->working_disks++;
		} else {
			/*
			 * Must be a spare disk ..
			 */
			printk(KERN_INFO "raid5: spare disk %s\n", partition_name(rdev->dev));
			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = rdev->dev;

			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 1;
			disk->used_slot = 1;
		}
	}

	for (i = 0; i < MD_SB_DISKS; i++) {
		desc = sb->disks + i;
		raid_disk = desc->raid_disk;
		disk = conf->disks + raid_disk;

		if (disk_faulty(desc) && (raid_disk < sb->raid_disks) &&
			!conf->disks[raid_disk].used_slot) {

			disk->number = desc->number;
			disk->raid_disk = raid_disk;
			disk->dev = MKDEV(0,0);

			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 0;
			disk->used_slot = 1;
		}
	}

	conf->raid_disks = sb->raid_disks;
	/*
	 * 0 for a fully functional array, 1 for a degraded array.
	 */
	conf->failed_disks = conf->raid_disks - conf->working_disks;
	conf->mddev = mddev;
	conf->chunk_size = sb->chunk_size;
	conf->level = sb->level;
	conf->algorithm = sb->layout;
	conf->max_nr_stripes = NR_STRIPES;

#if 0
	for (i = 0; i < conf->raid_disks; i++) {
		if (!conf->disks[i].used_slot) {
			MD_BUG();
			goto abort;
		}
	}
#endif
	if (!conf->chunk_size || conf->chunk_size % 4) {
		printk(KERN_ERR "raid5: invalid chunk size %d for md%d\n", conf->chunk_size, mdidx(mddev));
		goto abort;
	}
	if (conf->algorithm > ALGORITHM_RIGHT_SYMMETRIC) {
		printk(KERN_ERR "raid5: unsupported parity algorithm %d for md%d\n", conf->algorithm, mdidx(mddev));
		goto abort;
	}
	if (conf->failed_disks > 1) {
		printk(KERN_ERR "raid5: not enough operational devices for md%d (%d/%d failed)\n", mdidx(mddev), conf->failed_disks, conf->raid_disks);
		goto abort;
	}

	if (conf->working_disks != sb->raid_disks) {
		printk(KERN_ALERT "raid5: md%d, not all disks are operational -- trying to recover array\n", mdidx(mddev));
		start_recovery = 1;
	}

	if (!start_recovery && (sb->state & (1 << MD_SB_CLEAN)) &&
			check_consistency(mddev)) {
		printk(KERN_ERR "raid5: detected raid-5 superblock xor inconsistency -- running resync\n");
		sb->state &= ~(1 << MD_SB_CLEAN);
	}

	{
		const char * name = "raid5d";

		conf->thread = md_register_thread(raid5d, conf, name);
		if (!conf->thread) {
			printk(KERN_ERR "raid5: couldn't allocate thread for md%d\n", mdidx(mddev));
			goto abort;
		}
	}

	memory = conf->max_nr_stripes * (sizeof(struct stripe_head) +
		 conf->raid_disks * (sizeof(struct buffer_head) +
		 2 * (sizeof(struct buffer_head) + PAGE_SIZE))) / 1024;
	if (grow_stripes(conf, conf->max_nr_stripes, GFP_KERNEL)) {
		printk(KERN_ERR "raid5: couldn't allocate %dkB for buffers\n", memory);
		shrink_stripes(conf, conf->max_nr_stripes);
		goto abort;
	} else
		printk(KERN_INFO "raid5: allocated %dkB for md%d\n", memory, mdidx(mddev));

	/*
	 * Regenerate the "device is in sync with the raid set" bit for
	 * each device.
	 */
	for (i = 0; i < MD_SB_DISKS ; i++) {
		mark_disk_nonsync(sb->disks + i);
		for (j = 0; j < sb->raid_disks; j++) {
			if (!conf->disks[j].operational)
				continue;
			if (sb->disks[i].number == conf->disks[j].number)
				mark_disk_sync(sb->disks + i);
		}
	}
	sb->active_disks = conf->working_disks;

	if (sb->active_disks == sb->raid_disks)
		printk("raid5: raid level %d set md%d active with %d out of %d devices, algorithm %d\n", conf->level, mdidx(mddev), sb->active_disks, sb->raid_disks, conf->algorithm);
	else
		printk(KERN_ALERT "raid5: raid level %d set md%d active with %d out of %d devices, algorithm %d\n", conf->level, mdidx(mddev), sb->active_disks, sb->raid_disks, conf->algorithm);

	if (!start_recovery && !(sb->state & (1 << MD_SB_CLEAN))) {
		const char * name = "raid5syncd";

		conf->resync_thread = md_register_thread(raid5syncd, conf,name);
		if (!conf->resync_thread) {
			printk(KERN_ERR "raid5: couldn't allocate thread for md%d\n", mdidx(mddev));
			goto abort;
		}

		printk("raid5: raid set md%d not clean; reconstructing parity\n", mdidx(mddev));
		conf->resync_parity = 1;
		md_wakeup_thread(conf->resync_thread);
	}

	print_raid5_conf(conf);
	if (start_recovery)
		md_recover_arrays();
	print_raid5_conf(conf);

	/* Ok, everything is just fine now */
	return (0);
abort:
	if (conf) {
		print_raid5_conf(conf);
		if (conf->stripe_hashtbl)
			free_pages((unsigned long) conf->stripe_hashtbl,
							HASH_PAGES_ORDER);
		kfree(conf);
	}
	mddev->private = NULL;
	printk(KERN_ALERT "raid5: failed to run raid set md%d\n", mdidx(mddev));
	MOD_DEC_USE_COUNT;
	return -EIO;
}

static int raid5_stop_resync (mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	mdk_thread_t *thread = conf->resync_thread;

	if (thread) {
		if (conf->resync_parity) {
			conf->resync_parity = 2;
			md_interrupt_thread(thread);
			printk(KERN_INFO "raid5: parity resync was not fully finished, restarting next time.\n");
			return 1;
		}
		return 0;
	}
	return 0;
}

static int raid5_restart_resync (mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);

	if (conf->resync_parity) {
		if (!conf->resync_thread) {
			MD_BUG();
			return 0;
		}
		printk("raid5: waking up raid5resync.\n");
		conf->resync_parity = 1;
		md_wakeup_thread(conf->resync_thread);
		return 1;
	} else
		printk("raid5: no restart-resync needed.\n");
	return 0;
}


static int raid5_stop (mddev_t *mddev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;

	shrink_stripe_cache(conf, conf->max_nr_stripes);
	shrink_stripes(conf, conf->max_nr_stripes);
	md_unregister_thread(conf->thread);
	if (conf->resync_thread)
		md_unregister_thread(conf->resync_thread);
	free_pages((unsigned long) conf->stripe_hashtbl, HASH_PAGES_ORDER);
	kfree(conf);
	mddev->private = NULL;
	MOD_DEC_USE_COUNT;
	return 0;
}

#if RAID5_DEBUG
static void print_sh (struct stripe_head *sh)
{
	int i;

	printk("sh %lu, phase %d, size %d, pd_idx %d, state %ld, cmd %d.\n", sh->sector, sh->phase, sh->size, sh->pd_idx, sh->state, sh->cmd);
	printk("sh %lu, write_method %d, nr_pending %d, count %d.\n", sh->sector, sh->write_method, atomic_read(&sh->nr_pending), atomic_read(&sh->count));
	printk("sh %lu, ", sh->sector);
	for (i = 0; i < MD_SB_DISKS; i++) {
		if (sh->bh_old[i])
			printk("(old%d: %p) ", i, sh->bh_old[i]);
		if (sh->bh_new[i])
			printk("(new%d: %p) ", i, sh->bh_new[i]);
		if (sh->bh_copy[i])
			printk("(copy%d: %p) ", i, sh->bh_copy[i]);
		if (sh->bh_req[i])
			printk("(req%d: %p) ", i, sh->bh_req[i]);
	}
	printk("\n");
	for (i = 0; i < MD_SB_DISKS; i++)
		printk("%d(%d/%d) ", i, sh->cmd_new[i], sh->new[i]);
	printk("\n");
}

static void printall (raid5_conf_t *conf)
{
	struct stripe_head *sh;
	int i;

	md_spin_lock_irq(&conf->device_lock);
	for (i = 0; i < NR_HASH; i++) {
		sh = conf->stripe_hashtbl[i];
		for (; sh; sh = sh->hash_next) {
			if (sh->raid_conf != conf)
				continue;
			print_sh(sh);
		}
	}
	md_spin_unlock_irq(&conf->device_lock);

	PRINTK("--- raid5d inactive\n");
}
#endif

static int raid5_status (char *page, mddev_t *mddev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	mdp_super_t *sb = mddev->sb;
	int sz = 0, i;

	sz += sprintf (page+sz, " level %d, %dk chunk, algorithm %d", sb->level, sb->chunk_size >> 10, sb->layout);
	sz += sprintf (page+sz, " [%d/%d] [", conf->raid_disks, conf->working_disks);
	for (i = 0; i < conf->raid_disks; i++)
		sz += sprintf (page+sz, "%s", conf->disks[i].operational ? "U" : "_");
	sz += sprintf (page+sz, "]");
#if RAID5_DEBUG
#define D(x) \
	sz += sprintf (page+sz, "<"#x":%d>", atomic_read(&conf->x))
	D(nr_handle);
	D(nr_stripes);
	D(nr_hashed_stripes);
	D(nr_locked_stripes);
	D(nr_pending_stripes);
	D(nr_cached_stripes);
	D(nr_free_sh);
	printall(conf);
#endif
	return sz;
}

static void print_raid5_conf (raid5_conf_t *conf)
{
	int i;
	struct disk_info *tmp;

	printk("RAID5 conf printout:\n");
	if (!conf) {
		printk("(conf==NULL)\n");
		return;
	}
	printk(" --- rd:%d wd:%d fd:%d\n", conf->raid_disks,
		 conf->working_disks, conf->failed_disks);

	for (i = 0; i < MD_SB_DISKS; i++) {
		tmp = conf->disks + i;
		printk(" disk %d, s:%d, o:%d, n:%d rd:%d us:%d dev:%s\n",
			i, tmp->spare,tmp->operational,
			tmp->number,tmp->raid_disk,tmp->used_slot,
			partition_name(tmp->dev));
	}
}

static int raid5_diskop(mddev_t *mddev, mdp_disk_t **d, int state)
{
	int err = 0;
	int i, failed_disk=-1, spare_disk=-1, removed_disk=-1, added_disk=-1;
	raid5_conf_t *conf = mddev->private;
	struct disk_info *tmp, *sdisk, *fdisk, *rdisk, *adisk;
	mdp_super_t *sb = mddev->sb;
	mdp_disk_t *failed_desc, *spare_desc, *added_desc;

	print_raid5_conf(conf);
	md_spin_lock_irq(&conf->device_lock);
	/*
	 * find the disk ...
	 */
	switch (state) {

	case DISKOP_SPARE_ACTIVE:

		/*
		 * Find the failed disk within the RAID5 configuration ...
		 * (this can only be in the first conf->raid_disks part)
		 */
		for (i = 0; i < conf->raid_disks; i++) {
			tmp = conf->disks + i;
			if ((!tmp->operational && !tmp->spare) ||
					!tmp->used_slot) {
				failed_disk = i;
				break;
			}
		}
		/*
		 * When we activate a spare disk we _must_ have a disk in
		 * the lower (active) part of the array to replace.
		 */
		if ((failed_disk == -1) || (failed_disk >= conf->raid_disks)) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		/* fall through */

	case DISKOP_SPARE_WRITE:
	case DISKOP_SPARE_INACTIVE:

		/*
		 * Find the spare disk ... (can only be in the 'high'
		 * area of the array)
		 */
		for (i = conf->raid_disks; i < MD_SB_DISKS; i++) {
			tmp = conf->disks + i;
			if (tmp->spare && tmp->number == (*d)->number) {
				spare_disk = i;
				break;
			}
		}
		if (spare_disk == -1) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		break;

	case DISKOP_HOT_REMOVE_DISK:

		for (i = 0; i < MD_SB_DISKS; i++) {
			tmp = conf->disks + i;
			if (tmp->used_slot && (tmp->number == (*d)->number)) {
				if (tmp->operational) {
					err = -EBUSY;
					goto abort;
				}
				removed_disk = i;
				break;
			}
		}
		if (removed_disk == -1) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		break;

	case DISKOP_HOT_ADD_DISK:

		for (i = conf->raid_disks; i < MD_SB_DISKS; i++) {
			tmp = conf->disks + i;
			if (!tmp->used_slot) {
				added_disk = i;
				break;
			}
		}
		if (added_disk == -1) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		break;
	}

	switch (state) {
	/*
	 * Switch the spare disk to write-only mode:
	 */
	case DISKOP_SPARE_WRITE:
		if (conf->spare) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		sdisk = conf->disks + spare_disk;
		sdisk->operational = 1;
		sdisk->write_only = 1;
		conf->spare = sdisk;
		break;
	/*
	 * Deactivate a spare disk:
	 */
	case DISKOP_SPARE_INACTIVE:
		sdisk = conf->disks + spare_disk;
		sdisk->operational = 0;
		sdisk->write_only = 0;
		/*
		 * Was the spare being resynced?
		 */
		if (conf->spare == sdisk)
			conf->spare = NULL;
		break;
	/*
	 * Activate (mark read-write) the (now sync) spare disk,
	 * which means we switch it's 'raid position' (->raid_disk)
	 * with the failed disk. (only the first 'conf->raid_disks'
	 * slots are used for 'real' disks and we must preserve this
	 * property)
	 */
	case DISKOP_SPARE_ACTIVE:
		if (!conf->spare) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		sdisk = conf->disks + spare_disk;
		fdisk = conf->disks + failed_disk;

		spare_desc = &sb->disks[sdisk->number];
		failed_desc = &sb->disks[fdisk->number];

		if (spare_desc != *d) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (spare_desc->raid_disk != sdisk->raid_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}
			
		if (sdisk->raid_disk != spare_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (failed_desc->raid_disk != fdisk->raid_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (fdisk->raid_disk != failed_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		/*
		 * do the switch finally
		 */
		xchg_values(*spare_desc, *failed_desc);
		xchg_values(*fdisk, *sdisk);

		/*
		 * (careful, 'failed' and 'spare' are switched from now on)
		 *
		 * we want to preserve linear numbering and we want to
		 * give the proper raid_disk number to the now activated
		 * disk. (this means we switch back these values)
		 */
	
		xchg_values(spare_desc->raid_disk, failed_desc->raid_disk);
		xchg_values(sdisk->raid_disk, fdisk->raid_disk);
		xchg_values(spare_desc->number, failed_desc->number);
		xchg_values(sdisk->number, fdisk->number);

		*d = failed_desc;

		if (sdisk->dev == MKDEV(0,0))
			sdisk->used_slot = 0;

		/*
		 * this really activates the spare.
		 */
		fdisk->spare = 0;
		fdisk->write_only = 0;

		/*
		 * if we activate a spare, we definitely replace a
		 * non-operational disk slot in the 'low' area of
		 * the disk array.
		 */
		conf->failed_disks--;
		conf->working_disks++;
		conf->spare = NULL;

		break;

	case DISKOP_HOT_REMOVE_DISK:
		rdisk = conf->disks + removed_disk;

		if (rdisk->spare && (removed_disk < conf->raid_disks)) {
			MD_BUG();	
			err = 1;
			goto abort;
		}
		rdisk->dev = MKDEV(0,0);
		rdisk->used_slot = 0;

		break;

	case DISKOP_HOT_ADD_DISK:
		adisk = conf->disks + added_disk;
		added_desc = *d;

		if (added_disk != added_desc->number) {
			MD_BUG();	
			err = 1;
			goto abort;
		}

		adisk->number = added_desc->number;
		adisk->raid_disk = added_desc->raid_disk;
		adisk->dev = MKDEV(added_desc->major,added_desc->minor);

		adisk->operational = 0;
		adisk->write_only = 0;
		adisk->spare = 1;
		adisk->used_slot = 1;


		break;

	default:
		MD_BUG();	
		err = 1;
		goto abort;
	}
abort:
	md_spin_unlock_irq(&conf->device_lock);
	print_raid5_conf(conf);
	return err;
}

static mdk_personality_t raid5_personality=
{
	name:		"raid5",
	make_request:	raid5_make_request,
	run:		raid5_run,
	stop:		raid5_stop,
	status:		raid5_status,
	error_handler:	raid5_error,
	diskop:		raid5_diskop,
	stop_resync:	raid5_stop_resync,
	restart_resync:	raid5_restart_resync,
	sync_request:	raid5_sync_request
};

int raid5_init (void)
{
	int err;

	err = register_md_personality (RAID5, &raid5_personality);
	if (err)
		return err;

	/*
	 * pick a XOR routine, runtime.
	 */
	calibrate_xor_block();

	return 0;
}

#ifdef MODULE
int init_module (void)
{
	return raid5_init();
}

void cleanup_module (void)
{
	unregister_md_personality (RAID5);
}
#endif
