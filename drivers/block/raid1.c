/************************************************************************
 * raid1.c : Multiple Devices driver for Linux
 *           Copyright (C) 1996 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *
 * RAID-1 management functions.
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

#include <linux/module.h>
#include <linux/locks.h>
#include <linux/malloc.h>
#include <linux/md.h>
#include <linux/raid1.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

/*
 * The following can be used to debug the driver
 */
/*#define RAID1_DEBUG*/
#ifdef RAID1_DEBUG
#define PRINTK(x)   do { printk x; } while (0);
#else
#define PRINTK(x)   do { ; } while (0);
#endif

#define MAX(a,b)	((a) > (b) ? (a) : (b))
#define MIN(a,b)	((a) < (b) ? (a) : (b))

static struct md_personality raid1_personality;
static struct md_thread *raid1_thread = NULL;
struct buffer_head *raid1_retry_list = NULL;

static int __raid1_map (struct md_dev *mddev, kdev_t *rdev,
		        unsigned long *rsector, unsigned long size)
{
	struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;
	int i, n = raid_conf->raid_disks;

	/*
	 * Later we do read balancing on the read side 
	 * now we use the first available disk.
	 */

	PRINTK(("raid1_map().\n"));

	for (i=0; i<n; i++) {
		if (raid_conf->mirrors[i].operational) {
			*rdev = raid_conf->mirrors[i].dev;
			return (0);
		}
	}

	printk (KERN_ERR "raid1_map(): huh, no more operational devices?\n");
	return (-1);
}

static int raid1_map (struct md_dev *mddev, kdev_t *rdev,
		      unsigned long *rsector, unsigned long size)
{
	return 0;
}

void raid1_reschedule_retry (struct buffer_head *bh)
{
	struct raid1_bh * r1_bh = (struct raid1_bh *)(bh->b_dev_id);

	PRINTK(("raid1_reschedule_retry().\n"));

	r1_bh->next_retry = raid1_retry_list;
	raid1_retry_list = bh;
	md_wakeup_thread(raid1_thread);
}

/*
 * raid1_end_buffer_io() is called when we have finished servicing a mirrored
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static inline void raid1_end_buffer_io(struct raid1_bh *r1_bh, int uptodate)
{
	struct buffer_head *bh = r1_bh->master_bh;

	bh->b_end_io(bh, uptodate);
	kfree(r1_bh);
}

int raid1_one_error=0;

void raid1_end_request (struct buffer_head *bh, int uptodate)
{
	struct raid1_bh * r1_bh = (struct raid1_bh *)(bh->b_dev_id);
	unsigned long flags;

	save_flags(flags);
	cli();
	PRINTK(("raid1_end_request().\n"));

	if (raid1_one_error) {
		raid1_one_error=0;
		uptodate=0;
	}
	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	if (!uptodate)
		md_error (bh->b_dev, bh->b_rdev);
	else {
		/*
		 * Set BH_Uptodate in our master buffer_head, so that
		 * we will return a good error code for to the higher
		 * levels even if IO on some other mirrored buffer fails.
		 *
		 * The 'master' represents the complex operation to 
		 * user-side. So if something waits for IO, then it will
		 * wait for the 'master' buffer_head.
		 */
		set_bit (BH_Uptodate, &r1_bh->state);
	}

	/*
	 * We split up the read and write side, imho they are 
	 * conceptually different.
	 */

	if ( (r1_bh->cmd == READ) || (r1_bh->cmd == READA) ) {

		PRINTK(("raid1_end_request(), read branch.\n"));

		/*
		 * we have only one buffer_head on the read side
		 */
		if (uptodate) {
			PRINTK(("raid1_end_request(), read branch, uptodate.\n"));
			raid1_end_buffer_io(r1_bh, uptodate);
			restore_flags(flags);
			return;
		}
		/*
		 * oops, read error:
		 */
		printk(KERN_ERR "raid1: %s: rescheduling block %lu\n", 
				 kdevname(bh->b_dev), bh->b_blocknr);
		raid1_reschedule_retry (bh);
		restore_flags(flags);
		return;
	}

	/*
	 * WRITE or WRITEA.
	 */
	PRINTK(("raid1_end_request(), write branch.\n"));

	/*
	 * Let's see if all mirrored write operations have finished 
	 * already [we have irqs off, so we can decrease]:
	 */

	if (!--r1_bh->remaining) {
		struct md_dev *mddev = r1_bh->mddev;
		struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;
		int i, n = raid_conf->raid_disks;

		PRINTK(("raid1_end_request(), remaining == 0.\n"));

		for ( i=0; i<n; i++)
			if (r1_bh->mirror_bh[i]) kfree(r1_bh->mirror_bh[i]);

		raid1_end_buffer_io(r1_bh, test_bit(BH_Uptodate, &r1_bh->state));
	}
	else PRINTK(("raid1_end_request(), remaining == %u.\n", r1_bh->remaining));
	restore_flags(flags);
}

/* This routine checks if the undelying device is an md device and in that
 * case it maps the blocks before putting the request on the queue
 */
static inline void
map_and_make_request (int rw, struct buffer_head *bh)
{
	if (MAJOR (bh->b_rdev) == MD_MAJOR)
		md_map (MINOR (bh->b_rdev), &bh->b_rdev, &bh->b_rsector, bh->b_size >> 9);
	clear_bit(BH_Lock, &bh->b_state);
	make_request (MAJOR (bh->b_rdev), rw, bh);
}
	
static int
raid1_make_request (struct md_dev *mddev, int rw, struct buffer_head * bh)
{

	struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;
	struct buffer_head *mirror_bh[MD_SB_DISKS], *bh_req;
	struct raid1_bh * r1_bh;
	int n = raid_conf->raid_disks, i, sum_bhs = 0, switch_disks = 0, sectors;
	struct mirror_info *mirror;

	PRINTK(("raid1_make_request().\n"));

	while (!( /* FIXME: now we are rather fault tolerant than nice */
	r1_bh = kmalloc (sizeof (struct raid1_bh), GFP_KERNEL)
	) )
		printk ("raid1_make_request(#1): out of memory\n");
	memset (r1_bh, 0, sizeof (struct raid1_bh));

/*
 * make_request() can abort the operation when READA or WRITEA are being
 * used and no empty request is available.
 *
 * Currently, just replace the command with READ/WRITE.
 */
	if (rw == READA) rw = READ;
	if (rw == WRITEA) rw = WRITE;

	if (rw == WRITE || rw == WRITEA)
		mark_buffer_clean(bh);		/* Too early ? */

/*
 * i think the read and write branch should be separated completely, since we want
 * to do read balancing on the read side for example. Comments? :) --mingo
 */

	r1_bh->master_bh=bh;
	r1_bh->mddev=mddev;
	r1_bh->cmd = rw;

	if (rw==READ || rw==READA) {
		int last_used = raid_conf->last_used;
		PRINTK(("raid1_make_request(), read branch.\n"));
		mirror = raid_conf->mirrors + last_used;
		bh->b_rdev = mirror->dev;
		sectors = bh->b_size >> 9;
		if (bh->b_blocknr * sectors == raid_conf->next_sect) {
			raid_conf->sect_count += sectors;
			if (raid_conf->sect_count >= mirror->sect_limit)
				switch_disks = 1;
		} else
			switch_disks = 1;
		raid_conf->next_sect = (bh->b_blocknr + 1) * sectors;
		if (switch_disks) {
			PRINTK(("read-balancing: switching %d -> %d (%d sectors)\n", last_used, mirror->next, raid_conf->sect_count));
			raid_conf->sect_count = 0;
			last_used = raid_conf->last_used = mirror->next;
			/*
			 * Do not switch to write-only disks ... resyncing
			 * is in progress
			 */
			while (raid_conf->mirrors[last_used].write_only)
				raid_conf->last_used = raid_conf->mirrors[last_used].next;
		}
		PRINTK (("raid1 read queue: %d %d\n", MAJOR (bh->b_rdev), MINOR (bh->b_rdev)));
		bh_req = &r1_bh->bh_req;
		memcpy(bh_req, bh, sizeof(*bh));
		bh_req->b_end_io = raid1_end_request;
		bh_req->b_dev_id = r1_bh;
		map_and_make_request (rw, bh_req);
		return 0;
	}

	/*
	 * WRITE or WRITEA.
	 */
	PRINTK(("raid1_make_request(n=%d), write branch.\n",n));

	for (i = 0; i < n; i++) {

		if (!raid_conf->mirrors [i].operational) {
			/*
			 * the r1_bh->mirror_bh[i] pointer remains NULL
			 */
			mirror_bh[i] = NULL;
			continue;
		}

	/*
	 * We should use a private pool (size depending on NR_REQUEST),
	 * to avoid writes filling up the memory with bhs
	 *
	 * Such pools are much faster than kmalloc anyways (so we waste almost 
	 * nothing by not using the master bh when writing and win alot of cleanness)
	 *
	 * but for now we are cool enough. --mingo
	 *
	 * It's safe to sleep here, buffer heads cannot be used in a shared
	 * manner in the write branch. Look how we lock the buffer at the beginning
	 * of this function to grok the difference ;)
	 */
		while (!( /* FIXME: now we are rather fault tolerant than nice */
		mirror_bh[i] = kmalloc (sizeof (struct buffer_head), GFP_KERNEL)
		) )
			printk ("raid1_make_request(#2): out of memory\n");
		memset (mirror_bh[i], 0, sizeof (struct buffer_head));

	/*
	 * prepare mirrored bh (fields ordered for max mem throughput):
	 */
		mirror_bh [i]->b_blocknr    = bh->b_blocknr;
		mirror_bh [i]->b_dev        = bh->b_dev;
		mirror_bh [i]->b_rdev 	    = raid_conf->mirrors [i].dev;
		mirror_bh [i]->b_rsector    = bh->b_rsector;
		mirror_bh [i]->b_state      = (1<<BH_Req) | (1<<BH_Dirty);
		mirror_bh [i]->b_count      = 1;
		mirror_bh [i]->b_size       = bh->b_size;
		mirror_bh [i]->b_data       = bh->b_data;
		mirror_bh [i]->b_list       = BUF_LOCKED;
		mirror_bh [i]->b_end_io     = raid1_end_request;
		mirror_bh [i]->b_dev_id     = r1_bh;

		r1_bh->mirror_bh[i] = mirror_bh[i];
		sum_bhs++;
	}

	r1_bh->remaining = sum_bhs;

	PRINTK(("raid1_make_request(), write branch, sum_bhs=%d.\n",sum_bhs));

	/*
	 * We have to be a bit careful about the semaphore above, thats why we
	 * start the requests separately. Since kmalloc() could fail, sleep and
	 * make_request() can sleep too, this is the safer solution. Imagine,
	 * end_request decreasing the semaphore before we could have set it up ...
	 * We could play tricks with the semaphore (presetting it and correcting
	 * at the end if sum_bhs is not 'n' but we have to do end_request by hand
	 * if all requests finish until we had a chance to set up the semaphore
	 * correctly ... lots of races).
	 */
	for (i = 0; i < n; i++)
		if (mirror_bh [i] != NULL)
			map_and_make_request (rw, mirror_bh [i]);

	return (0);
}
			   
static int raid1_status (char *page, int minor, struct md_dev *mddev)
{
	struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;
	int sz = 0, i;
	
	sz += sprintf (page+sz, " [%d/%d] [", raid_conf->raid_disks, raid_conf->working_disks);
	for (i = 0; i < raid_conf->raid_disks; i++)
		sz += sprintf (page+sz, "%s", raid_conf->mirrors [i].operational ? "U" : "_");
	sz += sprintf (page+sz, "]");
	return sz;
}

static void raid1_fix_links (struct raid1_data *raid_conf, int failed_index)
{
	int disks = raid_conf->raid_disks;
	int j;

	for (j = 0; j < disks; j++)
		if (raid_conf->mirrors [j].next == failed_index)
			raid_conf->mirrors [j].next = raid_conf->mirrors [failed_index].next;
}

#define LAST_DISK KERN_ALERT \
"raid1: only one disk left and IO error.\n"

#define NO_SPARE_DISK KERN_ALERT \
"raid1: no spare disk left, degrading mirror level by one.\n"

#define DISK_FAILED KERN_ALERT \
"raid1: Disk failure on %s, disabling device. \n" \
"       Operation continuing on %d devices\n"

#define START_SYNCING KERN_ALERT \
"raid1: start syncing spare disk.\n"

#define ALREADY_SYNCING KERN_INFO \
"raid1: syncing already in progress.\n"

static int raid1_error (struct md_dev *mddev, kdev_t dev)
{
	struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;
	struct mirror_info *mirror;
	md_superblock_t *sb = mddev->sb;
	int disks = raid_conf->raid_disks;
	int i;

	PRINTK(("raid1_error called\n"));

	if (raid_conf->working_disks == 1) {
		/*
		 * Uh oh, we can do nothing if this is our last disk, but
		 * first check if this is a queued request for a device
		 * which has just failed.
		 */
		for (i = 0, mirror = raid_conf->mirrors; i < disks;
				 i++, mirror++)
			if (mirror->dev == dev && !mirror->operational)
				return 0;
		printk (LAST_DISK);
	} else {
		/* Mark disk as unusable */
		for (i = 0, mirror = raid_conf->mirrors; i < disks;
				 i++, mirror++) {
			if (mirror->dev == dev && mirror->operational){
				mirror->operational = 0;
				raid1_fix_links (raid_conf, i);
				sb->disks[mirror->number].state |=
						(1 << MD_FAULTY_DEVICE);
				sb->disks[mirror->number].state &=
						~(1 << MD_SYNC_DEVICE);
				sb->disks[mirror->number].state &=
						~(1 << MD_ACTIVE_DEVICE);
				sb->active_disks--;
				sb->working_disks--;
				sb->failed_disks++;
				mddev->sb_dirty = 1;
				md_wakeup_thread(raid1_thread);
				raid_conf->working_disks--;
				printk (DISK_FAILED, kdevname (dev),
						raid_conf->working_disks);
			}
		}
	}
	return 0;
}

#undef LAST_DISK
#undef NO_SPARE_DISK
#undef DISK_FAILED
#undef START_SYNCING

/*
 * This is the personality-specific hot-addition routine
 */

#define NO_SUPERBLOCK KERN_ERR \
"raid1: cannot hot-add disk to the array with no RAID superblock\n"

#define WRONG_LEVEL KERN_ERR \
"raid1: hot-add: level of disk is not RAID-1\n"

#define HOT_ADD_SUCCEEDED KERN_INFO \
"raid1: device %s hot-added\n"

static int raid1_hot_add_disk (struct md_dev *mddev, kdev_t dev)
{
	unsigned long flags;
	struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;
	struct mirror_info *mirror;
	md_superblock_t *sb = mddev->sb;
	struct real_dev * realdev;
	int n;

	/*
	 * The device has its superblock already read and it was found
	 * to be consistent for generic RAID usage.  Now we check whether
	 * it's usable for RAID-1 hot addition.
	 */

	n = mddev->nb_dev++;
	realdev = &mddev->devices[n];
	if (!realdev->sb) {
		printk (NO_SUPERBLOCK);
		return -EINVAL;
	}
	if (realdev->sb->level != 1) {
		printk (WRONG_LEVEL);
		return -EINVAL;
	}
	/* FIXME: are there other things left we could sanity-check? */

	/*
	 * We have to disable interrupts, as our RAID-1 state is used
	 * from irq handlers as well.
	 */
	save_flags(flags);
	cli();

	raid_conf->raid_disks++;
	mirror = raid_conf->mirrors+n;

	mirror->number=n;
	mirror->raid_disk=n;
	mirror->dev=dev;
	mirror->next=0; /* FIXME */
	mirror->sect_limit=128;

	mirror->operational=0;
	mirror->spare=1;
	mirror->write_only=0;

	sb->disks[n].state |= (1 << MD_FAULTY_DEVICE);
	sb->disks[n].state &= ~(1 << MD_SYNC_DEVICE);
	sb->disks[n].state &= ~(1 << MD_ACTIVE_DEVICE);
	sb->nr_disks++;
	sb->spare_disks++;

	restore_flags(flags);

	md_update_sb(MINOR(dev));

	printk (HOT_ADD_SUCCEEDED, kdevname(realdev->dev));

	return 0;
}

#undef NO_SUPERBLOCK
#undef WRONG_LEVEL
#undef HOT_ADD_SUCCEEDED

/*
 * Insert the spare disk into the drive-ring
 */
static void add_ring(struct raid1_data *raid_conf, struct mirror_info *mirror)
{
	int j, next;
	struct mirror_info *p = raid_conf->mirrors;

	for (j = 0; j < raid_conf->raid_disks; j++, p++)
		if (p->operational && !p->write_only) {
			next = p->next;
			p->next = mirror->raid_disk;
			mirror->next = next;
			return;
		}
	printk("raid1: bug: no read-operational devices\n");
}

static int raid1_mark_spare(struct md_dev *mddev, md_descriptor_t *spare,
				int state)
{
	int i = 0, failed_disk = -1;
	struct raid1_data *raid_conf = mddev->private;
	struct mirror_info *mirror = raid_conf->mirrors;
	md_descriptor_t *descriptor;
	unsigned long flags;

	for (i = 0; i < MD_SB_DISKS; i++, mirror++) {
		if (mirror->spare && mirror->number == spare->number)
			goto found;
	}
	return 1;
found:
	for (i = 0, mirror = raid_conf->mirrors; i < raid_conf->raid_disks;
								i++, mirror++)
		if (!mirror->operational)
			failed_disk = i;

	save_flags(flags);
	cli();
	switch (state) {
		case SPARE_WRITE:
			mirror->operational = 1;
			mirror->write_only = 1;
			raid_conf->raid_disks = MAX(raid_conf->raid_disks,
							mirror->raid_disk + 1);
			break;
		case SPARE_INACTIVE:
			mirror->operational = 0;
			mirror->write_only = 0;
			break;
		case SPARE_ACTIVE:
			mirror->spare = 0;
			mirror->write_only = 0;
			raid_conf->working_disks++;
			add_ring(raid_conf, mirror);

			if (failed_disk != -1) {
				descriptor = &mddev->sb->disks[raid_conf->mirrors[failed_disk].number];
				i = spare->raid_disk;
				spare->raid_disk = descriptor->raid_disk;
				descriptor->raid_disk = i;
			}
			break;
		default:
			printk("raid1_mark_spare: bug: state == %d\n", state);
			restore_flags(flags);
			return 1;
	}
	restore_flags(flags);
	return 0;
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working mirrors.
 *	2.	Updates the raid superblock when problems encounter.
 */
void raid1d (void *data)
{
	struct buffer_head *bh;
	kdev_t dev;
	unsigned long flags;
	struct raid1_bh * r1_bh;
	struct md_dev *mddev;

	PRINTK(("raid1d() active\n"));
	save_flags(flags);
	cli();
	while (raid1_retry_list) {
		bh = raid1_retry_list;
		r1_bh = (struct raid1_bh *)(bh->b_dev_id);
		raid1_retry_list = r1_bh->next_retry;
		restore_flags(flags);

		mddev = md_dev + MINOR(bh->b_dev);
		if (mddev->sb_dirty) {
			printk("dirty sb detected, updating.\n");
			mddev->sb_dirty = 0;
			md_update_sb(MINOR(bh->b_dev));
		}
		dev = bh->b_rdev;
		__raid1_map (md_dev + MINOR(bh->b_dev), &bh->b_rdev, &bh->b_rsector, bh->b_size >> 9);
		if (bh->b_rdev == dev) {
			printk (KERN_ALERT 
					"raid1: %s: unrecoverable I/O read error for block %lu\n",
						kdevname(bh->b_dev), bh->b_blocknr);
			raid1_end_buffer_io(r1_bh, 0);
		} else {
			printk (KERN_ERR "raid1: %s: redirecting sector %lu to another mirror\n", 
					  kdevname(bh->b_dev), bh->b_blocknr);
			map_and_make_request (r1_bh->cmd, bh);
		}
		cli();
	}
	restore_flags(flags);
}

/*
 * This will catch the scenario in which one of the mirrors was
 * mounted as a normal device rather than as a part of a raid set.
 */
static int __check_consistency (struct md_dev *mddev, int row)
{
	struct raid1_data *raid_conf = mddev->private;
	kdev_t dev;
	struct buffer_head *bh = NULL;
	int i, rc = 0;
	char *buffer = NULL;

	for (i = 0; i < raid_conf->raid_disks; i++) {
		if (!raid_conf->mirrors[i].operational)
			continue;
		dev = raid_conf->mirrors[i].dev;
		set_blocksize(dev, 4096);
		if ((bh = bread(dev, row / 4, 4096)) == NULL)
			break;
		if (!buffer) {
			buffer = (char *) __get_free_page(GFP_KERNEL);
			if (!buffer)
				break;
			memcpy(buffer, bh->b_data, 4096);
		} else if (memcmp(buffer, bh->b_data, 4096)) {
			rc = 1;
			break;
		}
		bforget(bh);
		fsync_dev(dev);
		invalidate_buffers(dev);
		bh = NULL;
	}
	if (buffer)
		free_page((unsigned long) buffer);
	if (bh) {
		dev = bh->b_dev;
		bforget(bh);
		fsync_dev(dev);
		invalidate_buffers(dev);
	}
	return rc;
}

static int check_consistency (struct md_dev *mddev)
{
	int size = mddev->sb->size;
	int row;

	for (row = 0; row < size; row += size / 8)
		if (__check_consistency(mddev, row))
			return 1;
	return 0;
}

static int raid1_run (int minor, struct md_dev *mddev)
{
	struct raid1_data *raid_conf;
	int i, j, raid_disk;
	md_superblock_t *sb = mddev->sb;
	md_descriptor_t *descriptor;
	struct real_dev *realdev;

	MOD_INC_USE_COUNT;

	if (sb->level != 1) {
		printk("raid1: %s: raid level not set to mirroring (%d)\n",
				kdevname(MKDEV(MD_MAJOR, minor)), sb->level);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}
	/****
	 * copy the now verified devices into our private RAID1 bookkeeping
	 * area. [whatever we allocate in raid1_run(), should be freed in
	 * raid1_stop()]
	 */

	while (!( /* FIXME: now we are rather fault tolerant than nice */
	mddev->private = kmalloc (sizeof (struct raid1_data), GFP_KERNEL)
	) )
		printk ("raid1_run(): out of memory\n");
	raid_conf = mddev->private;
	memset(raid_conf, 0, sizeof(*raid_conf));

	PRINTK(("raid1_run(%d) called.\n", minor));

  	for (i = 0; i < mddev->nb_dev; i++) {
  		realdev = &mddev->devices[i];
		if (!realdev->sb) {
			printk(KERN_ERR "raid1: disabled mirror %s (couldn't access raid superblock)\n", kdevname(realdev->dev));
			continue;
		}

		/*
		 * This is important -- we are using the descriptor on
		 * the disk only to get a pointer to the descriptor on
		 * the main superblock, which might be more recent.
		 */
		descriptor = &sb->disks[realdev->sb->descriptor.number];
		if (descriptor->state & (1 << MD_FAULTY_DEVICE)) {
			printk(KERN_ERR "raid1: disabled mirror %s (errors detected)\n", kdevname(realdev->dev));
			continue;
		}
		if (descriptor->state & (1 << MD_ACTIVE_DEVICE)) {
			if (!(descriptor->state & (1 << MD_SYNC_DEVICE))) {
				printk(KERN_ERR "raid1: disabled mirror %s (not in sync)\n", kdevname(realdev->dev));
				continue;
			}
			raid_disk = descriptor->raid_disk;
			if (descriptor->number > sb->nr_disks || raid_disk > sb->raid_disks) {
				printk(KERN_ERR "raid1: disabled mirror %s (inconsistent descriptor)\n", kdevname(realdev->dev));
				continue;
			}
			if (raid_conf->mirrors[raid_disk].operational) {
				printk(KERN_ERR "raid1: disabled mirror %s (mirror %d already operational)\n", kdevname(realdev->dev), raid_disk);
				continue;
			}
			printk(KERN_INFO "raid1: device %s operational as mirror %d\n", kdevname(realdev->dev), raid_disk);
			raid_conf->mirrors[raid_disk].number = descriptor->number;
			raid_conf->mirrors[raid_disk].raid_disk = raid_disk;
			raid_conf->mirrors[raid_disk].dev = mddev->devices [i].dev;
			raid_conf->mirrors[raid_disk].operational = 1;
			raid_conf->mirrors[raid_disk].sect_limit = 128;
			raid_conf->working_disks++;
		} else {
		/*
		 * Must be a spare disk ..
		 */
			printk(KERN_INFO "raid1: spare disk %s\n", kdevname(realdev->dev));
			raid_disk = descriptor->raid_disk;
			raid_conf->mirrors[raid_disk].number = descriptor->number;
			raid_conf->mirrors[raid_disk].raid_disk = raid_disk;
			raid_conf->mirrors[raid_disk].dev = mddev->devices [i].dev;
			raid_conf->mirrors[raid_disk].sect_limit = 128;

			raid_conf->mirrors[raid_disk].operational = 0;
			raid_conf->mirrors[raid_disk].write_only = 0;
			raid_conf->mirrors[raid_disk].spare = 1;
		}
	}
	if (!raid_conf->working_disks) {
		printk(KERN_ERR "raid1: no operational mirrors for %s\n", kdevname(MKDEV(MD_MAJOR, minor)));
		kfree(raid_conf);
		mddev->private = NULL;
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

	raid_conf->raid_disks = sb->raid_disks;
	raid_conf->mddev = mddev;

	for (j = 0; !raid_conf->mirrors[j].operational; j++);
	raid_conf->last_used = j;
	for (i = raid_conf->raid_disks - 1; i >= 0; i--) {
		if (raid_conf->mirrors[i].operational) {
			PRINTK(("raid_conf->mirrors[%d].next == %d\n", i, j));
			raid_conf->mirrors[i].next = j;
			j = i;
		}
	}

	if (check_consistency(mddev)) {
		printk(KERN_ERR "raid1: detected mirror differences -- run ckraid\n");
		sb->state |= 1 << MD_SB_ERRORS;
		kfree(raid_conf);
		mddev->private = NULL;
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

	/*
	 * Regenerate the "device is in sync with the raid set" bit for
	 * each device.
	 */
	for (i = 0; i < sb->nr_disks ; i++) {
		sb->disks[i].state &= ~(1 << MD_SYNC_DEVICE);
		for (j = 0; j < sb->raid_disks; j++) {
			if (!raid_conf->mirrors[j].operational)
				continue;
			if (sb->disks[i].number == raid_conf->mirrors[j].number)
				sb->disks[i].state |= 1 << MD_SYNC_DEVICE;
		}
	}
	sb->active_disks = raid_conf->working_disks;

	printk("raid1: raid set %s active with %d out of %d mirrors\n", kdevname(MKDEV(MD_MAJOR, minor)), sb->active_disks, sb->raid_disks);
	/* Ok, everything is just fine now */
	return (0);
}

static int raid1_stop (int minor, struct md_dev *mddev)
{
	struct raid1_data *raid_conf = (struct raid1_data *) mddev->private;

	kfree (raid_conf);
	mddev->private = NULL;
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct md_personality raid1_personality=
{
	"raid1",
	raid1_map,
	raid1_make_request,
	raid1_end_request,
	raid1_run,
	raid1_stop,
	raid1_status,
	NULL,			/* no ioctls */
	0,
	raid1_error,
	raid1_hot_add_disk,
	/* raid1_hot_remove_drive */ NULL,
	raid1_mark_spare
};

int raid1_init (void)
{
	if ((raid1_thread = md_register_thread(raid1d, NULL)) == NULL)
		return -EBUSY;
	return register_md_personality (RAID1, &raid1_personality);
}

#ifdef MODULE
int init_module (void)
{
	return raid1_init();
}

void cleanup_module (void)
{
	md_unregister_thread (raid1_thread);
	unregister_md_personality (RAID1);
}
#endif
