/*
 * linux/amiga/amipart.c
 *
 * Amiga partition checking driver for 680x0 Linux
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>

#include <asm/amigardb.h>
#include <asm/machdep.h>

extern int current_minor;

static ulong checksum (ulong *ptr, int len)
{
	ulong sum;
	int cnt;

	for (sum = 0, cnt = 0; cnt < len; cnt++)
		sum += ptr[cnt];

	return sum;
}

/* XXX */
/* int current_minor = 0; */

void amiga_check_partition(struct gendisk *hd, kdev_t dev)
{
	int i, minor = current_minor, m_lim = current_minor + hd->max_p;
	struct buffer_head *bh;
	struct RigidDiskBlock *rdb;
	struct PartitionBlock *pb;
	ulong bnum, partsect;

	for (bnum = 0; bnum < RDB_LOCATION_LIMIT/2; bnum++) {
		if (!(bh = bread(dev,bnum,1024))) {
			printk (" unable to read block %ld\n", bnum);
			return;
		}
#ifdef DEBUG
		printk ("block read, press mousebutton to continue\n");
		waitbut();
#endif
		rdb = (struct RigidDiskBlock *)bh->b_data;
		if (rdb->rdb_ID == IDNAME_RIGIDDISK)
			break;
		rdb = (struct RigidDiskBlock *)&bh->b_data[512];
		if (rdb->rdb_ID == IDNAME_RIGIDDISK)
			break;
		brelse (bh);
	}
	if (bnum == RDB_LOCATION_LIMIT/2) {
		/* no RDB on the disk! */
		printk (" unable to find RigidDiskBlock\n");
		return;
	}

	/* checksum the RigidDiskBlock */
	if (checksum ((ulong *)rdb, rdb->rdb_SummedLongs) != 0) {
		printk (" RDB checksum bad\n");
		return;
	}

	printk("  %s%c:", hd->major_name, 'a'+(minor >> hd->minor_shift));

	partsect = rdb->rdb_PartitionList;
	brelse (bh);

	for (i = 1; minor < m_lim && partsect != 0xffffffff; minor++, i++)
	{
		ulong *env;

		if (!(bh = bread(dev,partsect/2,1024))) {
			printk (" block %ld read failed\n", partsect);
			return;
		}
#ifdef DEBUG
		printk ("block read, press mousebutton to continue\n");
		waitbut();
#endif
		pb = (struct PartitionBlock *)bh->b_data;
		if (partsect & 1)
			pb = (struct PartitionBlock *)&bh->b_data[512];
		if (pb->pb_ID != IDNAME_PARTITION) {
			printk (" block %ld Not a partition block (%#lx)\n",
				partsect, pb->pb_ID);
			brelse (bh);
			return;
		}
		if (checksum ((ulong *)pb, pb->pb_SummedLongs) != 0) {
			printk (" block %ld checksum bad\n", partsect);
			brelse (bh);
			return;
		}

		env = pb->pb_Environment;

		hd->part[minor].start_sect = env[DE_LOWCYL]
			* env[DE_NUMHEADS] * env[DE_BLKSPERTRACK];
		hd->part[minor].nr_sects = (env[DE_UPPERCYL]
					    - env[DE_LOWCYL] + 1)
			* env[DE_NUMHEADS] * env[DE_BLKSPERTRACK];

		printk(" %s%c%d", hd->major_name,
		       'a'+(minor >> hd->minor_shift), i);

		partsect = pb->pb_Next;
		brelse (bh);
	}

	printk ("\n");
}
