/*
 *  fs/partitions/amiga.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>

#include <asm/byteorder.h>
#include <linux/affs_hardblocks.h>

#include "check.h"
#include "amiga.h"

static int current_minor;

static __inline__ u32
checksum_block(u32 *m, int size)
{
	u32 sum = 0;

	while (size--)
		sum += htonl(*m++);
	return sum;
}

int
amiga_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector, int first_part_minor)
{
	struct buffer_head	*bh;
	struct RigidDiskBlock	*rdb;
	struct PartitionBlock	*pb;
	int			 start_sect;
	int			 nr_sects;
	int			 blk;
	int			 part, res;
	int			 old_blocksize;
	int			 blocksize;

	old_blocksize = get_ptable_blocksize(dev);
	blocksize = get_hardsect_size(dev);

	set_blocksize(dev,blocksize);
	res = 0;

	for (blk = 0; blk < RDB_ALLOCATION_LIMIT; blk++) {
		if(!(bh = bread(dev,blk,blocksize))) {
			printk("Dev %s: unable to read RDB block %d\n",
			       kdevname(dev),blk);
			goto rdb_done;
		}
		if (*(u32 *)bh->b_data == htonl(IDNAME_RIGIDDISK)) {
			rdb = (struct RigidDiskBlock *)bh->b_data;
			if (checksum_block((u32 *)bh->b_data,htonl(rdb->rdb_SummedLongs) & 0x7F)) {
				/* Try again with 0xdc..0xdf zeroed, Windows might have
				 * trashed it.
				 */
				*(u32 *)(&bh->b_data[0xdc]) = 0;
				if (checksum_block((u32 *)bh->b_data,
						htonl(rdb->rdb_SummedLongs) & 0x7F)) {
					brelse(bh);
					printk("Dev %s: RDB in block %d has bad checksum\n",
					       kdevname(dev),blk);
					continue;
				}
				printk("Warning: Trashed word at 0xd0 in block %d "
					"ignored in checksum calculation\n",blk);
			}
			printk(" RDSK");
			blk = htonl(rdb->rdb_PartitionList);
			brelse(bh);
			for (part = 1; blk > 0 && part <= 16; part++) {
				if (!(bh = bread(dev,blk,blocksize))) {
					printk("Dev %s: unable to read partition block %d\n",
						       kdevname(dev),blk);
					goto rdb_done;
				}
				pb  = (struct PartitionBlock *)bh->b_data;
				blk = htonl(pb->pb_Next);
				if (pb->pb_ID == htonl(IDNAME_PARTITION) && checksum_block(
				    (u32 *)pb,htonl(pb->pb_SummedLongs) & 0x7F) == 0 ) {

					/* Tell Kernel about it */

					if (!(nr_sects = (htonl(pb->pb_Environment[10]) + 1 -
							  htonl(pb->pb_Environment[9])) *
							 htonl(pb->pb_Environment[3]) *
							 htonl(pb->pb_Environment[5]))) {
						brelse(bh);
						continue;
					}
					start_sect = htonl(pb->pb_Environment[9]) *
						     htonl(pb->pb_Environment[3]) *
						     htonl(pb->pb_Environment[5]);
					add_gd_partition(hd,current_minor,start_sect,nr_sects);
					current_minor++;
					res = 1;
				}
				brelse(bh);
			}
			printk("\n");
		}
		else
			brelse(bh);
	}

rdb_done:
	set_blocksize(dev,old_blocksize);
	return res;
}

