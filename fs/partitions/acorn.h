/*
 * fs/partitions/acorn.h
 *
 * Copyright (C) 1996-1998 Russell King
 */
#include <linux/adfs_fs.h>

/*
 * Offset in bytes of the boot block on the disk.
 */
#define BOOT_SECTOR_ADDRESS 0xc00

/*
 * Disc record size
 */
#define RECSIZE 60

/*
 * Disc record
 */
struct disc_record {
	unsigned char  log2secsize;
	unsigned char  secspertrack;
	unsigned char  heads;
	unsigned char  density;
	unsigned char  idlen;
	unsigned char  log2bpmb;
	unsigned char  skew;
	unsigned char  bootoption;
	unsigned char  lowsector;
	unsigned char  nzones;
	unsigned short zone_spare;
	unsigned long  root;
	unsigned long  disc_size;
	unsigned short disc_id;
	unsigned char  disc_name[10];
	unsigned long  disc_type;
	unsigned long  disc_size_high;
	unsigned char  log2sharesize:4;
	unsigned char  unused:4;
	unsigned char  big_flag:1;
};

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

