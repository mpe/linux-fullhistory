/*
 * raid1.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 1999, 2000, 2001 Ingo Molnar, Red Hat
 *
 * Copyright (C) 1996, 1997, 1998 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *
 * RAID-1 management functions.
 *
 * Better read-balancing code written by Mika Kuoppala <miku@iki.fi>, 2000
 *
 * Fixes to reconstruction by Jakob �stergaard" <jakob@ostenfeld.dk>
 * Various fixes by Neil Brown <neilb@cse.unsw.edu.au>
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

#include <linux/raid/raid1.h>
#include <linux/bio.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY
#define DEVICE_NR(device) (minor(device))

/*
 * Number of guaranteed r1bios in case of extreme VM load:
 */
#define	NR_RAID1_BIOS 256

static mdk_personality_t raid1_personality;
static spinlock_t retry_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(retry_list_head);

static void * r1bio_pool_alloc(int gfp_flags, void *data)
{
	r1bio_t *r1_bio;

	r1_bio = kmalloc(sizeof(r1bio_t), gfp_flags);
	if (r1_bio)
		memset(r1_bio, 0, sizeof(*r1_bio));

	return r1_bio;
}

static void r1bio_pool_free(void *r1_bio, void *data)
{
	kfree(r1_bio);
}

#define RESYNC_BLOCK_SIZE (64*1024)
#define RESYNC_SECTORS (RESYNC_BLOCK_SIZE >> 9)
#define RESYNC_PAGES ((RESYNC_BLOCK_SIZE + PAGE_SIZE-1) / PAGE_SIZE)
#define RESYNC_WINDOW (2048*1024)

static void * r1buf_pool_alloc(int gfp_flags, void *data)
{
	conf_t *conf = data;
	struct page *page;
	r1bio_t *r1_bio;
	struct bio *bio;
	int i, j;

	r1_bio = mempool_alloc(conf->r1bio_pool, gfp_flags);

	bio = bio_alloc(gfp_flags, RESYNC_PAGES);
	if (!bio)
		goto out_free_r1_bio;

	for (i = 0; i < RESYNC_PAGES; i++) {
		page = alloc_page(gfp_flags);
		if (unlikely(!page))
			goto out_free_pages;

		bio->bi_io_vec[i].bv_page = page;
		bio->bi_io_vec[i].bv_len = PAGE_SIZE;
		bio->bi_io_vec[i].bv_offset = 0;
	}

	/*
	 * Allocate a single data page for this iovec.
	 */
	bio->bi_vcnt = RESYNC_PAGES;
	bio->bi_idx = 0;
	bio->bi_size = RESYNC_BLOCK_SIZE;
	bio->bi_end_io = NULL;
	atomic_set(&bio->bi_cnt, 1);

	r1_bio->master_bio = bio;

	return r1_bio;

out_free_pages:
	for (j = 0; j < i; j++)
		__free_page(bio->bi_io_vec[j].bv_page);
	bio_put(bio);
out_free_r1_bio:
	mempool_free(r1_bio, conf->r1bio_pool);
	return NULL;
}

static void r1buf_pool_free(void *__r1_bio, void *data)
{
	int i;
	conf_t *conf = data;
	r1bio_t *r1bio = __r1_bio;
	struct bio *bio = r1bio->master_bio;

	if (atomic_read(&bio->bi_cnt) != 1)
		BUG();
	for (i = 0; i < RESYNC_PAGES; i++) {
		__free_page(bio->bi_io_vec[i].bv_page);
		bio->bi_io_vec[i].bv_page = NULL;
	}
	if (atomic_read(&bio->bi_cnt) != 1)
		BUG();
	bio_put(bio);
	mempool_free(r1bio, conf->r1bio_pool);
}

static void put_all_bios(conf_t *conf, r1bio_t *r1_bio)
{
	int i;

	if (r1_bio->read_bio) {
		if (atomic_read(&r1_bio->read_bio->bi_cnt) != 1)
			BUG();
		bio_put(r1_bio->read_bio);
		r1_bio->read_bio = NULL;
	}
	for (i = 0; i < conf->raid_disks; i++) {
		struct bio **bio = r1_bio->write_bios + i;
		if (*bio) {
			if (atomic_read(&(*bio)->bi_cnt) != 1)
				BUG();
			bio_put(*bio);
		}
		*bio = NULL;
	}
}

static inline void free_r1bio(r1bio_t *r1_bio)
{
	unsigned long flags;

	conf_t *conf = mddev_to_conf(r1_bio->mddev);

	/*
	 * Wake up any possible resync thread that waits for the device
	 * to go idle.
	 */
	spin_lock_irqsave(&conf->resync_lock, flags);
	if (!--conf->nr_pending) {
		wake_up(&conf->wait_idle);
		wake_up(&conf->wait_resume);
	}
	spin_unlock_irqrestore(&conf->resync_lock, flags);

	put_all_bios(conf, r1_bio);
	mempool_free(r1_bio, conf->r1bio_pool);
}

static inline void put_buf(r1bio_t *r1_bio)
{
	conf_t *conf = mddev_to_conf(r1_bio->mddev);
	struct bio *bio = r1_bio->master_bio;
	unsigned long flags;

	/*
	 * undo any possible partial request fixup magic:
	 */
	if (bio->bi_size != RESYNC_BLOCK_SIZE)
		bio->bi_io_vec[bio->bi_vcnt-1].bv_len = PAGE_SIZE;
	put_all_bios(conf, r1_bio);
	mempool_free(r1_bio, conf->r1buf_pool);

	spin_lock_irqsave(&conf->resync_lock, flags);
	if (!conf->barrier)
		BUG();
	--conf->barrier;
	wake_up(&conf->wait_resume);
	wake_up(&conf->wait_idle);

	if (!--conf->nr_pending) {
		wake_up(&conf->wait_idle);
		wake_up(&conf->wait_resume);
	}
	spin_unlock_irqrestore(&conf->resync_lock, flags);
}

static int map(mddev_t *mddev, mdk_rdev_t **rdevp)
{
	conf_t *conf = mddev_to_conf(mddev);
	int i, disks = conf->raid_disks;

	/*
	 * Later we do read balancing on the read side
	 * now we use the first available disk.
	 */

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < disks; i++) {
		mdk_rdev_t *rdev = conf->mirrors[i].rdev;
		if (rdev && rdev->in_sync) {
			*rdevp = rdev;
			atomic_inc(&rdev->nr_pending);
			spin_unlock_irq(&conf->device_lock);
			return 0;
		}
	}
	spin_unlock_irq(&conf->device_lock);

	printk (KERN_ERR "raid1_map(): huh, no more operational devices?\n");
	return -1;
}

static void reschedule_retry(r1bio_t *r1_bio)
{
	unsigned long flags;
	mddev_t *mddev = r1_bio->mddev;
	conf_t *conf = mddev_to_conf(mddev);

	spin_lock_irqsave(&retry_list_lock, flags);
	list_add(&r1_bio->retry_list, &retry_list_head);
	spin_unlock_irqrestore(&retry_list_lock, flags);

	md_wakeup_thread(conf->thread);
}

/*
 * raid_end_bio_io() is called when we have finished servicing a mirrored
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static void raid_end_bio_io(r1bio_t *r1_bio, int uptodate)
{
	struct bio *bio = r1_bio->master_bio;

	bio_endio(bio, bio->bi_size, uptodate ? 0 : -EIO);
	free_r1bio(r1_bio);
}

/*
 * Update disk head position estimator based on IRQ completion info.
 */
static void inline update_head_pos(int disk, r1bio_t *r1_bio)
{
	conf_t *conf = mddev_to_conf(r1_bio->mddev);

	conf->mirrors[disk].head_position =
		r1_bio->sector + (r1_bio->master_bio->bi_size >> 9);
}

static int end_request(struct bio *bio, unsigned int bytes_done, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t * r1_bio = (r1bio_t *)(bio->bi_private);
	int mirror;
	conf_t *conf = mddev_to_conf(r1_bio->mddev);

	if (bio->bi_size)
		return 1;
	
	if (r1_bio->cmd == READ || r1_bio->cmd == READA)
		mirror = r1_bio->read_disk;
	else {
		for (mirror = 0; mirror < conf->raid_disks; mirror++)
			if (r1_bio->write_bios[mirror] == bio)
				break;
	}
	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	if (!uptodate)
		md_error(r1_bio->mddev, conf->mirrors[mirror].rdev);
	else
		/*
		 * Set R1BIO_Uptodate in our master bio, so that
		 * we will return a good error code for to the higher
		 * levels even if IO on some other mirrored buffer fails.
		 *
		 * The 'master' represents the composite IO operation to
		 * user-side. So if something waits for IO, then it will
		 * wait for the 'master' bio.
		 */
		set_bit(R1BIO_Uptodate, &r1_bio->state);

	update_head_pos(mirror, r1_bio);
	if ((r1_bio->cmd == READ) || (r1_bio->cmd == READA)) {
		if (!r1_bio->read_bio)
			BUG();
		/*
		 * we have only one bio on the read side
		 */
		if (uptodate)
			raid_end_bio_io(r1_bio, uptodate);
		else {
			/*
			 * oops, read error:
			 */
			printk(KERN_ERR "raid1: %s: rescheduling sector %llu\n",
			       bdev_partition_name(conf->mirrors[mirror].rdev->bdev), (unsigned long long)r1_bio->sector);
			reschedule_retry(r1_bio);
		}
	} else {

		if (r1_bio->read_bio)
			BUG();
		/*
		 * WRITE:
		 *
		 * Let's see if all mirrored write operations have finished
		 * already.
		 */
		if (atomic_dec_and_test(&r1_bio->remaining))
			raid_end_bio_io(r1_bio, uptodate);
	}
	atomic_dec(&conf->mirrors[mirror].rdev->nr_pending);
	return 0;
}

/*
 * This routine returns the disk from which the requested read should
 * be done. There is a per-array 'next expected sequential IO' sector
 * number - if this matches on the next IO then we use the last disk.
 * There is also a per-disk 'last know head position' sector that is
 * maintained from IRQ contexts, both the normal and the resync IO
 * completion handlers update this position correctly. If there is no
 * perfect sequential match then we pick the disk whose head is closest.
 *
 * If there are 2 mirrors in the same 2 devices, performance degrades
 * because position is mirror, not device based.
 *
 * The rdev for the device selected will have nr_pending incremented.
 */
static int read_balance(conf_t *conf, struct bio *bio, r1bio_t *r1_bio)
{
	const unsigned long this_sector = r1_bio->sector;
	int new_disk = conf->last_used, disk = new_disk;
	const int sectors = bio->bi_size >> 9;
	sector_t new_distance, current_distance;

	spin_lock_irq(&conf->device_lock);
	/*
	 * Check if it if we can balance. We can balance on the whole
	 * device if no resync is going on, or below the resync window.
	 * We take the first readable disk when above the resync window.
	 */
	if (!conf->mddev->in_sync && (this_sector + sectors >= conf->next_resync)) {
		/* make sure that disk is operational */
		new_disk = 0;

		while (!conf->mirrors[new_disk].rdev ||
		       !conf->mirrors[new_disk].rdev->in_sync) {
			new_disk++;
			if (new_disk == conf->raid_disks) {
				new_disk = 0;
				break;
			}
		}
		goto rb_out;
	}


	/* make sure the disk is operational */
	while (!conf->mirrors[new_disk].rdev ||
	       !conf->mirrors[new_disk].rdev->in_sync) {
		if (new_disk <= 0)
			new_disk = conf->raid_disks;
		new_disk--;
		if (new_disk == disk) {
			new_disk = conf->last_used;
			goto rb_out;
		}
	}
	disk = new_disk;
	/* now disk == new_disk == starting point for search */

	/*
	 * Don't change to another disk for sequential reads:
	 */
	if (conf->next_seq_sect == this_sector)
		goto rb_out;
	if (this_sector == conf->mirrors[new_disk].head_position)
		goto rb_out;

	current_distance = abs(this_sector - conf->mirrors[disk].head_position);

	/* Find the disk whose head is closest */

	do {
		if (disk <= 0)
			disk = conf->raid_disks;
		disk--;

		if (!conf->mirrors[disk].rdev ||
		    !conf->mirrors[disk].rdev->in_sync)
			continue;

		if (!atomic_read(&conf->mirrors[disk].rdev->nr_pending)) {
			new_disk = disk;
			break;
		}
		new_distance = abs(this_sector - conf->mirrors[disk].head_position);
		if (new_distance < current_distance) {
			current_distance = new_distance;
			new_disk = disk;
		}
	} while (disk != conf->last_used);

rb_out:
	r1_bio->read_disk = new_disk;
	conf->next_seq_sect = this_sector + sectors;

	conf->last_used = new_disk;

	if (conf->mirrors[new_disk].rdev)
		atomic_inc(&conf->mirrors[new_disk].rdev->nr_pending);
	spin_unlock_irq(&conf->device_lock);

	return new_disk;
}

/*
 * Throttle resync depth, so that we can both get proper overlapping of
 * requests, but are still able to handle normal requests quickly.
 */
#define RESYNC_DEPTH 32

static void device_barrier(conf_t *conf, sector_t sect)
{
	spin_lock_irq(&conf->resync_lock);
	wait_event_lock_irq(conf->wait_idle, !waitqueue_active(&conf->wait_resume), conf->resync_lock);
	
	if (!conf->barrier++) {
		wait_event_lock_irq(conf->wait_idle, !conf->nr_pending, conf->resync_lock);
		if (conf->nr_pending)
			BUG();
	}
	wait_event_lock_irq(conf->wait_resume, conf->barrier < RESYNC_DEPTH, conf->resync_lock);
	conf->next_resync = sect;
	spin_unlock_irq(&conf->resync_lock);
}

static int make_request(request_queue_t *q, struct bio * bio)
{
	mddev_t *mddev = q->queuedata;
	conf_t *conf = mddev_to_conf(mddev);
	mirror_info_t *mirror;
	r1bio_t *r1_bio;
	struct bio *read_bio;
	int i, sum_bios = 0, disks = conf->raid_disks;

	/*
	 * Register the new request and wait if the reconstruction
	 * thread has put up a bar for new requests.
	 * Continue immediately if no resync is active currently.
	 */
	spin_lock_irq(&conf->resync_lock);
	wait_event_lock_irq(conf->wait_resume, !conf->barrier, conf->resync_lock);
	conf->nr_pending++;
	spin_unlock_irq(&conf->resync_lock);

	/*
	 * make_request() can abort the operation when READA is being
	 * used and no empty request is available.
	 *
	 */
	r1_bio = mempool_alloc(conf->r1bio_pool, GFP_NOIO);

	r1_bio->master_bio = bio;

	r1_bio->mddev = mddev;
	r1_bio->sector = bio->bi_sector;
	r1_bio->cmd = bio_data_dir(bio);

	if (r1_bio->cmd == READ) {
		/*
		 * read balancing logic:
		 */
		mirror = conf->mirrors + read_balance(conf, bio, r1_bio);

		read_bio = bio_clone(bio, GFP_NOIO);
		if (r1_bio->read_bio)
			BUG();
		r1_bio->read_bio = read_bio;

		read_bio->bi_sector = r1_bio->sector;
		read_bio->bi_bdev = mirror->rdev->bdev;
		read_bio->bi_end_io = end_request;
		read_bio->bi_rw = r1_bio->cmd;
		read_bio->bi_private = r1_bio;

		generic_make_request(read_bio);
		return 0;
	}

	/*
	 * WRITE:
	 */
	/* first select target devices under spinlock and
	 * inc refcount on their rdev.  Record them by setting
	 * write_bios[x] to bio
	 */
	spin_lock_irq(&conf->device_lock);
	for (i = 0;  i < disks; i++) {
		if (conf->mirrors[i].rdev &&
		    !conf->mirrors[i].rdev->faulty) {
			atomic_inc(&conf->mirrors[i].rdev->nr_pending);
			r1_bio->write_bios[i] = bio;
		} else
			r1_bio->write_bios[i] = NULL;
	}
	spin_unlock_irq(&conf->device_lock);
	for (i = 0; i < disks; i++) {
		struct bio *mbio;
		if (!r1_bio->write_bios[i])
			continue;

		mbio = bio_clone(bio, GFP_NOIO);
		r1_bio->write_bios[i] = mbio;

		mbio->bi_sector	= r1_bio->sector;
		mbio->bi_bdev = conf->mirrors[i].rdev->bdev;
		mbio->bi_end_io	= end_request;
		mbio->bi_rw = r1_bio->cmd;
		mbio->bi_private = r1_bio;

		sum_bios++;
	}
	if (!sum_bios) {
		/*
		 * If all mirrors are non-operational
		 * then return an IO error:
		 */
		raid_end_bio_io(r1_bio, 0);
		return 0;
	}
	atomic_set(&r1_bio->remaining, sum_bios);

	/*
	 * We have to be a bit careful about the semaphore above, thats
	 * why we start the requests separately. Since generic_make_request()
	 * can sleep, this is the safer solution. Imagine, end_request
	 * decreasing the semaphore before we could have set it up ...
	 * We could play tricks with the semaphore (presetting it and
	 * correcting at the end if sum_bios is not 'n' but we have to
	 * do end_request by hand if all requests finish until we had a
	 * chance to set up the semaphore correctly ... lots of races).
	 */
	for (i=disks; i--; ) {
		struct bio *mbio;
		mbio = r1_bio->write_bios[i];
		if (!mbio)
			continue;

		generic_make_request(mbio);
	}
	return 0;
}

static int status(char *page, mddev_t *mddev)
{
	conf_t *conf = mddev_to_conf(mddev);
	int sz = 0, i;

	sz += sprintf(page+sz, " [%d/%d] [", conf->raid_disks,
						conf->working_disks);
	for (i = 0; i < conf->raid_disks; i++)
		sz += sprintf(page+sz, "%s",
			      conf->mirrors[i].rdev &&
			      conf->mirrors[i].rdev->in_sync ? "U" : "_");
	sz += sprintf (page+sz, "]");
	return sz;
}

#define LAST_DISK KERN_ALERT \
"raid1: only one disk left and IO error.\n"

#define NO_SPARE_DISK KERN_ALERT \
"raid1: no spare disk left, degrading mirror level by one.\n"

#define DISK_FAILED KERN_ALERT \
"raid1: Disk failure on %s, disabling device. \n" \
"	Operation continuing on %d devices\n"

#define START_SYNCING KERN_ALERT \
"raid1: start syncing spare disk.\n"

#define ALREADY_SYNCING KERN_INFO \
"raid1: syncing already in progress.\n"


static void error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	conf_t *conf = mddev_to_conf(mddev);

	/*
	 * If it is not operational, then we have already marked it as dead
	 * else if it is the last working disks, ignore the error, let the
	 * next level up know.
	 * else mark the drive as failed
	 */
	if (rdev->in_sync
	    && conf->working_disks == 1)
		/*
		 * Don't fail the drive, act as though we were just a
		 * normal single drive
		 */
		return;
	if (rdev->in_sync) {
		mddev->degraded++;
		conf->working_disks--;
		/*
		 * if recovery was running, stop it now.
		 */
		if (mddev->recovery_running) 
			mddev->recovery_running = -EIO;
	}
	rdev->in_sync = 0;
	rdev->faulty = 1;
	mddev->sb_dirty = 1;
	printk(DISK_FAILED, bdev_partition_name(rdev->bdev), conf->working_disks);
}

static void print_conf(conf_t *conf)
{
	int i;
	mirror_info_t *tmp;

	printk("RAID1 conf printout:\n");
	if (!conf) {
		printk("(!conf)\n");
		return;
	}
	printk(" --- wd:%d rd:%d\n", conf->working_disks,
			conf->raid_disks);

	for (i = 0; i < conf->raid_disks; i++) {
		tmp = conf->mirrors + i;
		if (tmp->rdev)
			printk(" disk %d, wo:%d, o:%d, dev:%s\n",
			       i, !tmp->rdev->in_sync, !tmp->rdev->faulty,
			       bdev_partition_name(tmp->rdev->bdev));
	}
}

static void close_sync(conf_t *conf)
{
	spin_lock_irq(&conf->resync_lock);
	wait_event_lock_irq(conf->wait_resume, !conf->barrier, conf->resync_lock);
	spin_unlock_irq(&conf->resync_lock);

	if (conf->barrier) BUG();
	if (waitqueue_active(&conf->wait_idle)) BUG();

	mempool_destroy(conf->r1buf_pool);
	conf->r1buf_pool = NULL;
}

static int raid1_spare_active(mddev_t *mddev)
{
	int i;
	conf_t *conf = mddev->private;
	mirror_info_t *tmp;

	spin_lock_irq(&conf->device_lock);
	/*
	 * Find all failed disks within the RAID1 configuration 
	 * and mark them readable
	 */
	for (i = 0; i < conf->raid_disks; i++) {
		tmp = conf->mirrors + i;
		if (tmp->rdev 
		    && !tmp->rdev->faulty
		    && !tmp->rdev->in_sync) {
			conf->working_disks++;
			mddev->degraded--;
			tmp->rdev->in_sync = 1;
		}
	}
	spin_unlock_irq(&conf->device_lock);

	print_conf(conf);
	return 0;
}


static int raid1_add_disk(mddev_t *mddev, mdk_rdev_t *rdev)
{
	conf_t *conf = mddev->private;
	int found = 0;
	int mirror;
	mirror_info_t *p;

	spin_lock_irq(&conf->device_lock);
	for (mirror=0; mirror < mddev->raid_disks; mirror++)
		if ( !(p=conf->mirrors+mirror)->rdev) {
			p->rdev = rdev;
			p->head_position = 0;
			rdev->raid_disk = mirror;
			found = 1;
			break;
		}
	spin_unlock_irq(&conf->device_lock);

	print_conf(conf);
	return found;
}

static int raid1_remove_disk(mddev_t *mddev, int number)
{
	conf_t *conf = mddev->private;
	int err = 1;
	mirror_info_t *p = conf->mirrors+ number;

	print_conf(conf);
	spin_lock_irq(&conf->device_lock);
	if (p->rdev) {
		if (p->rdev->in_sync ||
		    atomic_read(&p->rdev->nr_pending)) {
			err = -EBUSY;
			goto abort;
		}
		p->rdev = NULL;
		err = 0;
	}
	if (err)
		MD_BUG();
abort:
	spin_unlock_irq(&conf->device_lock);

	print_conf(conf);
	return err;
}

#define IO_ERROR KERN_ALERT \
"raid1: %s: unrecoverable I/O read error for block %llu\n"

#define REDIRECT_SECTOR KERN_ERR \
"raid1: %s: redirecting sector %llu to another mirror\n"

static int end_sync_read(struct bio *bio, unsigned int bytes_done, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t * r1_bio = (r1bio_t *)(bio->bi_private);
	conf_t *conf = mddev_to_conf(r1_bio->mddev);

	if (bio->bi_size)
		return 1;

	if (r1_bio->read_bio != bio)
		BUG();
	update_head_pos(r1_bio->read_disk, r1_bio);
	/*
	 * we have read a block, now it needs to be re-written,
	 * or re-read if the read failed.
	 * We don't do much here, just schedule handling by raid1d
	 */
	if (!uptodate)
		md_error(r1_bio->mddev,
			 conf->mirrors[r1_bio->read_disk].rdev);
	else
		set_bit(R1BIO_Uptodate, &r1_bio->state);
	atomic_dec(&conf->mirrors[r1_bio->read_disk].rdev->nr_pending);
	reschedule_retry(r1_bio);
	return 0;
}

static int end_sync_write(struct bio *bio, unsigned int bytes_done, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t * r1_bio = (r1bio_t *)(bio->bi_private);
	mddev_t *mddev = r1_bio->mddev;
	conf_t *conf = mddev_to_conf(mddev);
	int i;
	int mirror=0;

	if (bio->bi_size)
		return 1;

	for (i = 0; i < conf->raid_disks; i++)
		if (r1_bio->write_bios[i] == bio) {
			mirror = i;
			break;
		}
	if (!uptodate)
		md_error(mddev, conf->mirrors[mirror].rdev);
	update_head_pos(mirror, r1_bio);

	if (atomic_dec_and_test(&r1_bio->remaining)) {
		md_done_sync(mddev, r1_bio->master_bio->bi_size >> 9, uptodate);
		put_buf(r1_bio);
	}
	atomic_dec(&conf->mirrors[mirror].rdev->nr_pending);
	return 0;
}

static void sync_request_write(mddev_t *mddev, r1bio_t *r1_bio)
{
	conf_t *conf = mddev_to_conf(mddev);
	int i, sum_bios = 0;
	int disks = conf->raid_disks;
	struct bio *bio, *mbio;

	bio = r1_bio->master_bio;

	/*
	 * have to allocate lots of bio structures and
	 * schedule writes
	 */
	if (!test_bit(R1BIO_Uptodate, &r1_bio->state)) {
		/*
		 * There is no point trying a read-for-reconstruct as
		 * reconstruct is about to be aborted
		 */
		printk(IO_ERROR, bdev_partition_name(bio->bi_bdev), (unsigned long long)r1_bio->sector);
		md_done_sync(mddev, r1_bio->master_bio->bi_size >> 9, 0);
		put_buf(r1_bio);
		return;
	}

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < disks ; i++) {
		r1_bio->write_bios[i] = NULL;
		if (!conf->mirrors[i].rdev || 
		    conf->mirrors[i].rdev->faulty)
			continue;
		if (i == conf->last_used)
			/*
			 * we read from here, no need to write
			 */
			continue;
		if (conf->mirrors[i].rdev->in_sync && mddev->in_sync)
			/*
			 * don't need to write this we are just rebuilding
			 */
			continue;
		atomic_inc(&conf->mirrors[i].rdev->nr_pending);
		r1_bio->write_bios[i] = bio;
	}
	spin_unlock_irq(&conf->device_lock);

	for (i = 0; i < disks ; i++) {
		if (!r1_bio->write_bios[i])
			continue;
		mbio = bio_clone(bio, GFP_NOIO);
		r1_bio->write_bios[i] = mbio;
		mbio->bi_bdev = conf->mirrors[i].rdev->bdev;
		mbio->bi_sector = r1_bio->sector;
		mbio->bi_end_io	= end_sync_write;
		mbio->bi_rw = WRITE;
		mbio->bi_private = r1_bio;

		sum_bios++;
	}
	if (i != disks)
		BUG();
	atomic_set(&r1_bio->remaining, sum_bios);


	if (!sum_bios) {
		/*
		 * Nowhere to write this to... I guess we
		 * must be done
		 */
		printk(KERN_ALERT "raid1: sync aborting as there is nowhere to write sector %llu\n", 
			(unsigned long long)r1_bio->sector);
		md_done_sync(mddev, r1_bio->master_bio->bi_size >> 9, 0);
		put_buf(r1_bio);
		return;
	}
	for (i = 0; i < disks ; i++) {
		mbio = r1_bio->write_bios[i];
		if (!mbio)
			continue;

		md_sync_acct(conf->mirrors[i].rdev, mbio->bi_size >> 9);
		generic_make_request(mbio);
	}
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working mirrors.
 *	2.	Updates the raid superblock when problems encounter.
 *	3.	Performs writes following reads for array syncronising.
 */

static void raid1d(void *data)
{
	struct list_head *head = &retry_list_head;
	r1bio_t *r1_bio;
	struct bio *bio;
	unsigned long flags;
	mddev_t *mddev;
	conf_t *conf;
	mdk_rdev_t *rdev;


	for (;;) {
		spin_lock_irqsave(&retry_list_lock, flags);
		if (list_empty(head))
			break;
		r1_bio = list_entry(head->prev, r1bio_t, retry_list);
		list_del(head->prev);
		spin_unlock_irqrestore(&retry_list_lock, flags);

		mddev = r1_bio->mddev;
		conf = mddev_to_conf(mddev);
		bio = r1_bio->master_bio;
		switch(r1_bio->cmd) {
		case SPECIAL:
			sync_request_write(mddev, r1_bio);
			break;
		case READ:
		case READA:
			if (map(mddev, &rdev) == -1) {
				printk(IO_ERROR, bdev_partition_name(bio->bi_bdev), (unsigned long long)r1_bio->sector);
				raid_end_bio_io(r1_bio, 0);
				break;
			}
			printk(REDIRECT_SECTOR,
				bdev_partition_name(rdev->bdev), (unsigned long long)r1_bio->sector);
			bio->bi_bdev = rdev->bdev;
			bio->bi_sector = r1_bio->sector;
			bio->bi_rw = r1_bio->cmd;

			generic_make_request(bio);
			break;
		}
	}
	spin_unlock_irqrestore(&retry_list_lock, flags);
}


static int init_resync(conf_t *conf)
{
	int buffs;

	buffs = RESYNC_WINDOW / RESYNC_BLOCK_SIZE;
	if (conf->r1buf_pool)
		BUG();
	conf->r1buf_pool = mempool_create(buffs, r1buf_pool_alloc, r1buf_pool_free, conf);
	if (!conf->r1buf_pool)
		return -ENOMEM;
	conf->next_resync = 0;
	return 0;
}

/*
 * perform a "sync" on one "block"
 *
 * We need to make sure that no normal I/O request - particularly write
 * requests - conflict with active sync requests.
 *
 * This is achieved by tracking pending requests and a 'barrier' concept
 * that can be installed to exclude normal IO requests.
 */

static int sync_request(mddev_t *mddev, sector_t sector_nr, int go_faster)
{
	conf_t *conf = mddev_to_conf(mddev);
	mirror_info_t *mirror;
	r1bio_t *r1_bio;
	struct bio *read_bio, *bio;
	sector_t max_sector, nr_sectors;
	int disk, partial;

	if (sector_nr == 0)
		if (init_resync(conf))
			return -ENOMEM;

	max_sector = mddev->size << 1;
	if (sector_nr >= max_sector) {
		close_sync(conf);
		return 0;
	}

	/*
	 * If there is non-resync activity waiting for us then
	 * put in a delay to throttle resync.
	 */
	if (!go_faster && waitqueue_active(&conf->wait_resume))
		schedule_timeout(HZ);
	device_barrier(conf, sector_nr + RESYNC_SECTORS);

	/*
	 * If reconstructing, and >1 working disc,
	 * could dedicate one to rebuild and others to
	 * service read requests ..
	 */
	disk = conf->last_used;
	/* make sure disk is operational */
	spin_lock_irq(&conf->device_lock);
	while (conf->mirrors[disk].rdev == NULL ||
	       !conf->mirrors[disk].rdev->in_sync) {
		if (disk <= 0)
			disk = conf->raid_disks;
		disk--;
		if (disk == conf->last_used)
			break;
	}
	conf->last_used = disk;
	atomic_inc(&conf->mirrors[disk].rdev->nr_pending);
	spin_unlock_irq(&conf->device_lock);

	mirror = conf->mirrors + disk;

	r1_bio = mempool_alloc(conf->r1buf_pool, GFP_NOIO);

	spin_lock_irq(&conf->resync_lock);
	conf->nr_pending++;
	spin_unlock_irq(&conf->resync_lock);

	r1_bio->mddev = mddev;
	r1_bio->sector = sector_nr;
	r1_bio->cmd = SPECIAL;
	r1_bio->read_disk = disk;

	bio = r1_bio->master_bio;
	nr_sectors = RESYNC_BLOCK_SIZE >> 9;
	if (max_sector - sector_nr < nr_sectors)
		nr_sectors = max_sector - sector_nr;
	bio->bi_size = nr_sectors << 9;
	bio->bi_vcnt = (bio->bi_size + PAGE_SIZE-1) / PAGE_SIZE;
	/*
	 * Is there a partial page at the end of the request?
	 */
	partial = bio->bi_size % PAGE_SIZE;
	if (partial)
		bio->bi_io_vec[bio->bi_vcnt-1].bv_len = partial;


	read_bio = bio_clone(r1_bio->master_bio, GFP_NOIO);

	read_bio->bi_sector = sector_nr;
	read_bio->bi_bdev = mirror->rdev->bdev;
	read_bio->bi_end_io = end_sync_read;
	read_bio->bi_rw = READ;
	read_bio->bi_private = r1_bio;

	if (r1_bio->read_bio)
		BUG();
	r1_bio->read_bio = read_bio;

	md_sync_acct(mirror->rdev, nr_sectors);

	generic_make_request(read_bio);

	return nr_sectors;
}

#define INVALID_LEVEL KERN_WARNING \
"raid1: md%d: raid level not set to mirroring (%d)\n"

#define NO_SB KERN_ERR \
"raid1: disabled mirror %s (couldn't access raid superblock)\n"

#define ERRORS KERN_ERR \
"raid1: disabled mirror %s (errors detected)\n"

#define NOT_IN_SYNC KERN_ERR \
"raid1: disabled mirror %s (not in sync)\n"

#define INCONSISTENT KERN_ERR \
"raid1: disabled mirror %s (inconsistent descriptor)\n"

#define ALREADY_RUNNING KERN_ERR \
"raid1: disabled mirror %s (mirror %d already operational)\n"

#define OPERATIONAL KERN_INFO \
"raid1: device %s operational as mirror %d\n"

#define MEM_ERROR KERN_ERR \
"raid1: couldn't allocate memory for md%d\n"

#define SPARE KERN_INFO \
"raid1: spare disk %s\n"

#define NONE_OPERATIONAL KERN_ERR \
"raid1: no operational mirrors for md%d\n"

#define ARRAY_IS_ACTIVE KERN_INFO \
"raid1: raid set md%d active with %d out of %d mirrors\n"

#define THREAD_ERROR KERN_ERR \
"raid1: couldn't allocate thread for md%d\n"

#define START_RESYNC KERN_WARNING \
"raid1: raid set md%d not clean; reconstructing mirrors\n"

static int run(mddev_t *mddev)
{
	conf_t *conf;
	int i, j, disk_idx;
	mirror_info_t *disk;
	mdk_rdev_t *rdev;
	struct list_head *tmp;

	MOD_INC_USE_COUNT;

	if (mddev->level != 1) {
		printk(INVALID_LEVEL, mdidx(mddev), mddev->level);
		goto out;
	}
	/*
	 * copy the already verified devices into our private RAID1
	 * bookkeeping area. [whatever we allocate in run(),
	 * should be freed in stop()]
	 */
	conf = kmalloc(sizeof(conf_t), GFP_KERNEL);
	mddev->private = conf;
	if (!conf) {
		printk(MEM_ERROR, mdidx(mddev));
		goto out;
	}
	memset(conf, 0, sizeof(*conf));

	conf->r1bio_pool = mempool_create(NR_RAID1_BIOS, r1bio_pool_alloc,
						r1bio_pool_free, NULL);
	if (!conf->r1bio_pool) {
		printk(MEM_ERROR, mdidx(mddev));
		goto out;
	}


	ITERATE_RDEV(mddev, rdev, tmp) {
		disk_idx = rdev->raid_disk;
		if (disk_idx >= mddev->raid_disks
		    || disk_idx < 0)
			continue;
		disk = conf->mirrors + disk_idx;

		disk->rdev = rdev;
		disk->head_position = 0;
		if (!rdev->faulty && rdev->in_sync)
			conf->working_disks++;
	}
	conf->raid_disks = mddev->raid_disks;
	conf->mddev = mddev;
	conf->device_lock = SPIN_LOCK_UNLOCKED;
	if (conf->working_disks == 1)
		mddev->state |= (1 << MD_SB_CLEAN);

	conf->resync_lock = SPIN_LOCK_UNLOCKED;
	init_waitqueue_head(&conf->wait_idle);
	init_waitqueue_head(&conf->wait_resume);

	if (!conf->working_disks) {
		printk(NONE_OPERATIONAL, mdidx(mddev));
		goto out_free_conf;
	}

	mddev->degraded = 0;
	for (i = 0; i < conf->raid_disks; i++) {

		disk = conf->mirrors + i;

		if (!disk->rdev) {
			disk->head_position = 0;
			mddev->degraded++;
		}
	}

	/*
	 * find the first working one and use it as a starting point
	 * to read balancing.
	 */
	for (j = 0; j < conf->raid_disks &&
		     (!conf->mirrors[j].rdev ||
		      !conf->mirrors[j].rdev->in_sync) ; j++)
		/* nothing */;
	conf->last_used = j;



	{
		const char * name = "raid1d";

		conf->thread = md_register_thread(raid1d, conf, name);
		if (!conf->thread) {
			printk(THREAD_ERROR, mdidx(mddev));
			goto out_free_conf;
		}
	}

	printk(ARRAY_IS_ACTIVE, mdidx(mddev), mddev->raid_disks - mddev->degraded, mddev->raid_disks);
	/*
	 * Ok, everything is just fine now
	 */
	return 0;

out_free_conf:
	if (conf->r1bio_pool)
		mempool_destroy(conf->r1bio_pool);
	kfree(conf);
	mddev->private = NULL;
out:
	MOD_DEC_USE_COUNT;
	return -EIO;
}

static int stop(mddev_t *mddev)
{
	conf_t *conf = mddev_to_conf(mddev);

	md_unregister_thread(conf->thread);
	if (conf->r1bio_pool)
		mempool_destroy(conf->r1bio_pool);
	kfree(conf);
	mddev->private = NULL;
	MOD_DEC_USE_COUNT;
	return 0;
}

static mdk_personality_t raid1_personality =
{
	.name		= "raid1",
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid1_add_disk,
	.hot_remove_disk= raid1_remove_disk,
	.spare_active	= raid1_spare_active,
	.sync_request	= sync_request,
};

static int __init raid_init(void)
{
	return register_md_personality(RAID1, &raid1_personality);
}

static void raid_exit(void)
{
	unregister_md_personality(RAID1);
}

module_init(raid_init);
module_exit(raid_exit);
MODULE_LICENSE("GPL");
