/*
 *  Code extracted from
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 */

/*
 *  Support for DiskManager v6.0x added by Mark Lord (mlord@bnr.ca)
 *  with hints from uwe@eas.iis.fhg.de (us3@irz.inf.tu-dresden.de).
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>

struct gendisk *gendisk_head = NULL;

static int current_minor = 0;
extern int *blk_size[];
extern void rd_load(void);
extern int ramdisk_size;

static char minor_name (struct gendisk *hd, int minor)
{
	char base_name = (hd->major == IDE1_MAJOR) ? 'c' : 'a';
	return base_name + (minor >> hd->minor_shift);
}

static void add_partition (struct gendisk *hd, int minor, int start, int size)
{
	hd->part[minor].start_sect = start;
	hd->part[minor].nr_sects   = size;
	printk(" %s%c%d", hd->major_name, minor_name(hd, minor),
		minor & ((1 << hd->minor_shift) - 1));
}

#ifdef CONFIG_MSDOS_PARTITION
/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */

static void extended_partition(struct gendisk *hd, int dev)
{
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector, this_sector;
	int mask = (1 << hd->minor_shift) - 1;

	first_sector = hd->part[MINOR(dev)].start_sect;
	this_sector = first_sector;

	while (1) {
		if ((current_minor & mask) >= (4 + hd->max_p))
			return;
		if (!(bh = bread(dev,0,1024)))
			return;
	  /*
	   * This block is from a device that we're about to stomp on.
	   * So make sure nobody thinks this block is usable.
	   */
		bh->b_dirt = 0;
		bh->b_uptodate = 0;
		bh->b_req = 0;
		if (*(unsigned short *) (bh->b_data+510) == 0xAA55) {
			p = (struct partition *) (0x1BE + bh->b_data);
		/*
		 * Process the first entry, which should be the real
		 * data partition.
		 */
			if (p->sys_ind == EXTENDED_PARTITION || !p->nr_sects)
				goto done;  /* shouldn't happen */
			add_partition(hd, current_minor, this_sector+p->start_sect, p->nr_sects);
			current_minor++;
			p++;
		/*
		 * Process the second entry, which should be a link
		 * to the next logical partition.  Create a minor
		 * for this just long enough to get the next partition
		 * table.  The minor will be reused for the real
		 * data partition.
		 */
			if (p->sys_ind != EXTENDED_PARTITION ||
			    !(hd->part[current_minor].nr_sects = p->nr_sects))
				goto done;  /* no more logicals in this partition */
			hd->part[current_minor].start_sect = first_sector + p->start_sect;
			hd->sizes[current_minor] = p->nr_sects >> (BLOCK_SIZE_BITS - 9);
			this_sector = first_sector + p->start_sect;
			dev = ((hd->major) << 8) | current_minor;
			brelse(bh);
		} else
			goto done;
	}
done:
	brelse(bh);
}

static int msdos_partition(struct gendisk *hd, unsigned int dev, unsigned long first_sector)
{
	int i, minor = current_minor, found_dm6 = 0;
	struct buffer_head *bh;
	struct partition *p;
	int mask = (1 << hd->minor_shift) - 1;
	extern void ide_xlate_1024(dev_t);

read_mbr:
	if (!(bh = bread(dev,0,1024))) {
		printk("unable to read partition table\n");
		return -1;
	}
	if (*(unsigned short *)  (0x1fe + bh->b_data) != 0xAA55) {
		brelse(bh);
		return 0;
	}
	p = (struct partition *) (0x1be + bh->b_data);

	/*
	 *  Check for Disk Manager v6.0x "Dynamic Disk Overlay" (DDO)
	 */
	if (p->sys_ind == DM6_PARTITION && !found_dm6++)
	{
		printk(" [DM6:DDO]");
		/*
		 * Everything is offset by one track (p->end_sector sectors),
		 * and a translated geometry is used to reduce the number
		 * of apparent cylinders to 1024 or less.
		 *
		 * For complete compatibility with linux fdisk, we do:
		 *  1. tell the driver to offset *everything* by one track,
		 *  2. reduce the apparent disk capacity by one track,
		 *  3. adjust the geometry reported by HDIO_GETGEO (for fdisk),
		 *	(does nothing if not an IDE drive, but that's okay).
		 *  4. invalidate our in-memory copy of block zero,
		 *  5. restart the partition table hunt from scratch.
		 */
		first_sector                    += p->end_sector;
		hd->part[MINOR(dev)].start_sect += p->end_sector;
		hd->part[MINOR(dev)].nr_sects   -= p->end_sector;
		ide_xlate_1024(dev);	/* harmless if not an IDE drive */
		bh->b_dirt = 0;		/* prevent re-use of this block */
		bh->b_uptodate = 0;
		bh->b_req = 0;
		brelse(bh);
		goto read_mbr;
	}

	/*
	 *  Check for Disk Manager v6.0x DDO on a secondary drive (?)
	 */
	if (p->sys_ind == DM6_AUXPARTITION) {
		printk(" [DM6]");
		ide_xlate_1024(dev);	/* harmless if not an IDE drive */
	}

	current_minor += 4;  /* first "extra" minor (for extended partitions) */
	for (i=1 ; i<=4 ; minor++,i++,p++) {
		if (!p->nr_sects)
			continue;
		add_partition(hd, minor, first_sector+p->start_sect, p->nr_sects);
		if ((current_minor & 0x3f) >= 60)
			continue;
		if (p->sys_ind == EXTENDED_PARTITION) {
			printk(" <");
			extended_partition(hd, (hd->major << 8) | minor);
			printk(" >");
		}
	}
	/*
	 *  Check for old-style Disk Manager partition table
	 */
	if (*(unsigned short *) (bh->b_data+0xfc) == 0x55AA) {
		p = (struct partition *) (0x1be + bh->b_data);
		for (i = 4 ; i < 16 ; i++, current_minor++) {
			p--;
			if ((current_minor & mask) >= mask-2)
				break;
			if (!(p->start_sect && p->nr_sects))
				continue;
			add_partition(hd, current_minor, p->start_sect, p->nr_sects);
		}
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif /* CONFIG_MSDOS_PARTITION */

#ifdef CONFIG_OSF_PARTITION

static int osf_partition(struct gendisk *hd, unsigned int dev, unsigned long first_sector)
{
	int i;
	struct buffer_head *bh;
	struct disklabel {
		u32 d_magic;
		u16 d_type,d_subtype;
		u8 d_typename[16];
		u8 d_packname[16];
		u32 d_secsize;
		u32 d_nsectors;
		u32 d_ntracks;
		u32 d_ncylinders;
		u32 d_secpercyl;
		u32 d_secprtunit;
		u16 d_sparespertrack;
		u16 d_sparespercyl;
		u32 d_acylinders;
		u16 d_rpm, d_interleave, d_trackskew, d_cylskew;
		u32 d_headswitch, d_trkseek, d_flags;
		u32 d_drivedata[5];
		u32 d_spare[5];
		u32 d_magic2;
		u16 d_checksum;
		u16 d_npartitions;
		u32 d_bbsize, d_sbsize;
		struct d_partition {
			u32 p_size;
			u32 p_offset;
			u32 p_fsize;
			u8  p_fstype;
			u8  p_frag;
			u16 p_cpg;
		} d_partitions[8];
	} * label;
	struct d_partition * partition;
#define DISKLABELMAGIC (0x82564557UL)

	if (!(bh = bread(dev,0,1024))) {
		printk("unable to read partition table\n");
		return -1;
	}
	label = (struct disklabel *) (bh->b_data+64);
	partition = label->d_partitions;
	if (label->d_magic != DISKLABELMAGIC) {
		printk("magic: %08x\n", label->d_magic);
		brelse(bh);
		return 0;
	}
	if (label->d_magic2 != DISKLABELMAGIC) {
		printk("magic2: %08x\n", label->d_magic2);
		brelse(bh);
		return 0;
	}
	for (i = 0 ; i < label->d_npartitions; i++, partition++) {
		if (partition->p_size)
			add_partition(hd, current_minor,
				first_sector+partition->p_offset,
				partition->p_size);
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif /* CONFIG_OSF_PARTITION */

static void check_partition(struct gendisk *hd, unsigned int dev)
{
	static int first_time = 1;
	unsigned long first_sector;

	if (first_time)
		printk("Partition check:\n");
	first_time = 0;
	first_sector = hd->part[MINOR(dev)].start_sect;

	/*
	 * This is a kludge to allow the partition check to be
	 * skipped for specific drives (ie. IDE cd-rom drives)
	 */
	if ((int)first_sector == -1) {
		hd->part[MINOR(dev)].start_sect = 0;
		return;
	}

	printk("  %s%c:", hd->major_name, minor_name(hd, MINOR(dev)));
#ifdef CONFIG_MSDOS_PARTITION
	if (msdos_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_OSF_PARTITION
	if (osf_partition(hd, dev, first_sector))
		return;
#endif
	printk("unknown partition table\n");
}

/* This function is used to re-read partition tables for removable disks.
   Much of the cleanup from the old partition tables should have already been
   done */

/* This function will re-read the partition tables for a given device,
and set things back up again.  There are some important caveats,
however.  You must ensure that no one is using the device, and no one
can start using the device while this function is being executed. */

void resetup_one_dev(struct gendisk *dev, int drive)
{
	int i;
	int start = drive<<dev->minor_shift;
	int j = start + dev->max_p;
	int major = dev->major << 8;

	current_minor = 1+(drive<<dev->minor_shift);
	check_partition(dev, major+(drive<<dev->minor_shift));

	for (i=start ; i < j ; i++)
		dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
}

static void setup_dev(struct gendisk *dev)
{
	int i;
	int j = dev->max_nr * dev->max_p;
	int major = dev->major << 8;
	int drive;
	

	for (i = 0 ; i < j; i++)  {
		dev->part[i].start_sect = 0;
		dev->part[i].nr_sects = 0;
	}
	dev->init();	
	for (drive=0 ; drive<dev->nr_real ; drive++) {
		current_minor = 1+(drive<<dev->minor_shift);
		check_partition(dev, major+(drive<<dev->minor_shift));
	}
	for (i=0 ; i < j ; i++)
		dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
	blk_size[dev->major] = dev->sizes;
}
	
void device_setup(void)
{
	struct gendisk *p;
	int nr=0;

	for (p = gendisk_head ; p ; p=p->next) {
		setup_dev(p);
		nr += p->nr_real;
	}
		
	if (ramdisk_size)
		rd_load();
}
