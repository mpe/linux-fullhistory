#ifndef _RAID10_H
#define _RAID10_H

#include <linux/raid/md.h>

typedef struct mirror_info mirror_info_t;

struct mirror_info {
	mdk_rdev_t	*rdev;
	sector_t	head_position;
};

typedef struct r10bio_s r10bio_t;

struct r10_private_data_s {
	mddev_t			*mddev;
	mirror_info_t		*mirrors;
	int			raid_disks;
	int			working_disks;
	spinlock_t		device_lock;

	/* geometry */
	int			near_copies;  /* number of copies layed out raid0 style */
	int 			far_copies;   /* number of copies layed out
					       * at large strides across drives
					       */
	int			copies;	      /* near_copies * far_copies.
					       * must be <= raid_disks
					       */
	sector_t		stride;	      /* distance between far copies.
					       * This is size / far_copies
					       */

	int chunk_shift; /* shift from chunks to sectors */
	sector_t chunk_mask;

	struct list_head	retry_list;
	/* for use when syncing mirrors: */

	spinlock_t		resync_lock;
	int nr_pending;
	int barrier;
	sector_t		next_resync;

	wait_queue_head_t	wait_idle;
	wait_queue_head_t	wait_resume;

	mempool_t *r10bio_pool;
	mempool_t *r10buf_pool;
};

typedef struct r10_private_data_s conf_t;

/*
 * this is the only point in the RAID code where we violate
 * C type safety. mddev->private is an 'opaque' pointer.
 */
#define mddev_to_conf(mddev) ((conf_t *) mddev->private)

/*
 * this is our 'private' RAID10 bio.
 *
 * it contains information about what kind of IO operations were started
 * for this RAID10 operation, and about their status:
 */

struct r10bio_s {
	atomic_t		remaining; /* 'have we finished' count,
					    * used from IRQ handlers
					    */
	sector_t		sector;	/* virtual sector number */
	int			sectors;
	unsigned long		state;
	mddev_t			*mddev;
	/*
	 * original bio going to /dev/mdx
	 */
	struct bio		*master_bio;
	/*
	 * if the IO is in READ direction, then this is where we read
	 */
	int			read_slot;

	struct list_head	retry_list;
	/*
	 * if the IO is in WRITE direction, then multiple bios are used,
	 * one for each copy.
	 * When resyncing we also use one for each copy.
	 * When reconstructing, we use 2 bios, one for read, one for write.
	 * We choose the number when they are allocated.
	 */
	struct {
		struct bio		*bio;
		sector_t addr;
		int devnum;
	} devs[0];
};

/* bits for r10bio.state */
#define	R10BIO_Uptodate	0
#define	R10BIO_IsSync	1
#define	R10BIO_IsRecover 2
#endif
