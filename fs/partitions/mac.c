/*
 *  fs/partitions/msdos.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/ctype.h>

#include <asm/system.h>

#include "check.h"
#include "mac.h"

#ifdef CONFIG_PPC
extern void note_bootable_part(kdev_t dev, int part);
#endif

/*
 * Code to understand MacOS partition tables.
 */

int mac_partition(struct gendisk *hd, kdev_t dev, unsigned long fsec, int first_part_minor)
{
	struct buffer_head *bh;
	int blk, blocks_in_map;
	int dev_bsize, dev_pos, pos;
	unsigned secsize;
#ifdef CONFIG_PPC
	int first_bootable = 1;
#endif
	struct mac_partition *part;
	struct mac_driver_desc *md;

	dev_bsize = get_ptable_blocksize(dev);
	dev_pos = 0;
	/* Get 0th block and look at the first partition map entry. */
	if ((bh = bread(dev, 0, dev_bsize)) == 0) {
	    printk("%s: error reading partition table\n",
		   kdevname(dev));
	    return -1;
	}
	md = (struct mac_driver_desc *) bh->b_data;
	if (be16_to_cpu(md->signature) != MAC_DRIVER_MAGIC) {
		brelse(bh);
		return 0;
	}
	secsize = be16_to_cpu(md->block_size);
	if (secsize >= dev_bsize) {
		brelse(bh);
		dev_pos = secsize;
		if ((bh = bread(dev, secsize/dev_bsize, dev_bsize)) == 0) {
			printk("%s: error reading partition table\n",
			       kdevname(dev));
			return -1;
		}
	}
	part = (struct mac_partition *) (bh->b_data + secsize - dev_pos);
	if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC) {
		brelse(bh);
		return 0;		/* not a MacOS disk */
	}
	blocks_in_map = be32_to_cpu(part->map_count);
	for (blk = 1; blk <= blocks_in_map; ++blk) {
		pos = blk * secsize;
		if (pos >= dev_pos + dev_bsize) {
			brelse(bh);
			dev_pos = pos;
			if ((bh = bread(dev, pos/dev_bsize, dev_bsize)) == 0) {
				printk("%s: error reading partition table\n",
				       kdevname(dev));
				return -1;
			}
		}
		part = (struct mac_partition *) (bh->b_data + pos - dev_pos);
		if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
			break;
		blocks_in_map = be32_to_cpu(part->map_count);
		add_gd_partition(hd, first_part_minor,
			fsec + be32_to_cpu(part->start_block) * (secsize/512),
			be32_to_cpu(part->block_count) * (secsize/512));

#ifdef CONFIG_PPC
		/*
		 * If this is the first bootable partition, tell the
		 * setup code, in case it wants to make this the root.
		 */
		if ( (_machine == _MACH_Pmac) && first_bootable
		    && (be32_to_cpu(part->status) & MAC_STATUS_BOOTABLE)
		    && strcasecmp(part->processor, "powerpc") == 0) {
			note_bootable_part(dev, blk);
			first_bootable = 0;
		}
#endif /* CONFIG_PPC */

		++first_part_minor;
	}
	brelse(bh);
	printk("\n");
	return 1;
}

