/*
 * fs/partitions/acorn.h
 *
 * Copyright (C) 1996-1998 Russell King
 */
#include <linux/adfs_fs.h>

/*
 * Partition types. (Oh for reusability)
 */
#define PARTITION_RISCIX_MFM	1
#define PARTITION_RISCIX_SCSI	2
#define PARTITION_LINUX		9

struct riscix_part {
	unsigned long  start;
	unsigned long  length;
	unsigned long  one;
	char name[16];
};

struct riscix_record {
	unsigned long  magic;
#define RISCIX_MAGIC	(0x4a657320)
	unsigned long  date;
	struct riscix_part part[8];
};

int
acorn_partition(struct gendisk *hd, kdev_t dev,
		unsigned long first_sector, int first_part_minor);

