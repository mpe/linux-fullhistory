/*
 *  Code extracted from
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  Support for DiskManager v6.0x added by Mark Lord,
 *  with information provided by OnTrack.  This now works for linux fdisk
 *  and LILO, as well as loadlin and bootln.  Note that disks other than
 *  /dev/hda *must* have a "DOS" type 0x51 partition in the first slot (hda1).
 *
 *  More flexible handling of extended partitions - aeb, 950831
 *
 *  Check partition table on IDE disks for common CHS translations
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/init.h>

#include <asm/system.h>

/*
 * Many architectures don't like unaligned accesses, which is
 * frequently the case with the nr_sects and start_sect partition
 * table entries.
 */
#include <asm/unaligned.h>

#define SYS_IND(p)	(get_unaligned(&p->sys_ind))
#define NR_SECTS(p)	({ __typeof__(p->nr_sects) __a =	\
				get_unaligned(&p->nr_sects);	\
				le32_to_cpu(__a); \
			})

#define START_SECT(p)	({ __typeof__(p->start_sect) __a =	\
				get_unaligned(&p->start_sect);	\
				le32_to_cpu(__a); \
			})

struct gendisk *gendisk_head = NULL;

static int current_minor = 0;
extern int *blk_size[];
extern void rd_load(void);
extern void initrd_load(void);

extern int chr_dev_init(void);
extern int blk_dev_init(void);
extern int scsi_dev_init(void);
extern int net_dev_init(void);

#ifdef CONFIG_PPC
extern void note_bootable_part(kdev_t dev, int part);
#endif

/*
 * disk_name() is used by genhd.c and md.c.
 * It formats the devicename of the indicated disk
 * into the supplied buffer, and returns a pointer
 * to that same buffer (for convenience).
 */
char *disk_name (struct gendisk *hd, int minor, char *buf)
{
	unsigned int part;
	const char *maj = hd->major_name;
	int unit = (minor >> hd->minor_shift) + 'a';

	/*
	 * IDE devices use multiple major numbers, but the drives
	 * are named as:  {hda,hdb}, {hdc,hdd}, {hde,hdf}, {hdg,hdh}..
	 * This requires special handling here.
	 */
	switch (hd->major) {
		case IDE5_MAJOR:
			unit += 2;
		case IDE4_MAJOR:
			unit += 2;
		case IDE3_MAJOR:
			unit += 2;
		case IDE2_MAJOR:
			unit += 2;
		case IDE1_MAJOR:
			unit += 2;
		case IDE0_MAJOR:
			maj = "hd";
			break;
	}
	part = minor & ((1 << hd->minor_shift) - 1);
	if (hd->major >= SCSI_DISK1_MAJOR && hd->major <= SCSI_DISK7_MAJOR) {
		unit = unit + (hd->major - SCSI_DISK1_MAJOR + 1) * 16;
		if (unit > 'z') {
			unit -= 'z' + 1;
			sprintf(buf, "sd%c%c", 'a' + unit / 26, 'a' + unit % 26);
			if (part)
				sprintf(buf + 4, "%d", part);
			return buf;
		}
	}
	if (part)
		sprintf(buf, "%s%c%d", maj, unit, part);
	else
		sprintf(buf, "%s%c", maj, unit);
	return buf;
}

static void add_partition (struct gendisk *hd, int minor, int start, int size)
{
	char buf[8];
	hd->part[minor].start_sect = start;
	hd->part[minor].nr_sects   = size;
	printk(" %s", disk_name(hd, minor, buf));
}

static inline int is_extended_partition(struct partition *p)
{
	return (SYS_IND(p) == DOS_EXTENDED_PARTITION ||
		SYS_IND(p) == WIN98_EXTENDED_PARTITION ||
		SYS_IND(p) == LINUX_EXTENDED_PARTITION);
}

static unsigned int get_ptable_blocksize(kdev_t dev)
{
  int ret = 1024;

  /*
   * See whether the low-level driver has given us a minumum blocksize.
   * If so, check to see whether it is larger than the default of 1024.
   */
  if (!blksize_size[MAJOR(dev)])
    {
      return ret;
    }

  /*
   * Check for certain special power of two sizes that we allow.
   * With anything larger than 1024, we must force the blocksize up to
   * the natural blocksize for the device so that we don't have to try
   * and read partial sectors.  Anything smaller should be just fine.
   */
  switch( blksize_size[MAJOR(dev)][MINOR(dev)] )
    {
    case 2048:
      ret = 2048;
      break;
    case 4096:
      ret = 4096;
      break;
    case 8192:
      ret = 8192;
      break;
    case 1024:
    case 512:
    case 256:
    case 0:
      /*
       * These are all OK.
       */
      break;
    default:
      panic("Strange blocksize for partition table\n");
    }

  return ret;

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

#define MSDOS_LABEL_MAGIC		0xAA55

static void extended_partition(struct gendisk *hd, kdev_t dev)
{
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector, first_size, this_sector, this_size;
	int mask = (1 << hd->minor_shift) - 1;
	int i;

	first_sector = hd->part[MINOR(dev)].start_sect;
	first_size = hd->part[MINOR(dev)].nr_sects;
	this_sector = first_sector;

	while (1) {
		if ((current_minor & mask) == 0)
			return;
		if (!(bh = bread(dev,0,get_ptable_blocksize(dev))))
			return;
	  /*
	   * This block is from a device that we're about to stomp on.
	   * So make sure nobody thinks this block is usable.
	   */
		bh->b_state = 0;

		if ((*(unsigned short *) (bh->b_data+510)) != cpu_to_le16(MSDOS_LABEL_MAGIC))
			goto done;

		p = (struct partition *) (0x1BE + bh->b_data);

		this_size = hd->part[MINOR(dev)].nr_sects;

		/*
		 * Usually, the first entry is the real data partition,
		 * the 2nd entry is the next extended partition, or empty,
		 * and the 3rd and 4th entries are unused.
		 * However, DRDOS sometimes has the extended partition as
		 * the first entry (when the data partition is empty),
		 * and OS/2 seems to use all four entries.
		 */

		/* 
		 * First process the data partition(s)
		 */
		for (i=0; i<4; i++, p++) {
		    if (!NR_SECTS(p) || is_extended_partition(p))
		      continue;

		    /* Check the 3rd and 4th entries -
		       these sometimes contain random garbage */
		    if (i >= 2
			&& START_SECT(p) + NR_SECTS(p) > this_size
			&& (this_sector + START_SECT(p) < first_sector ||
			    this_sector + START_SECT(p) + NR_SECTS(p) >
			     first_sector + first_size))
		      continue;

		    add_partition(hd, current_minor, this_sector+START_SECT(p), NR_SECTS(p));
		    current_minor++;
		    if ((current_minor & mask) == 0)
		      goto done;
		}
		/*
		 * Next, process the (first) extended partition, if present.
		 * (So far, there seems to be no reason to make
		 *  extended_partition()  recursive and allow a tree
		 *  of extended partitions.)
		 * It should be a link to the next logical partition.
		 * Create a minor for this just long enough to get the next
		 * partition table.  The minor will be reused for the next
		 * data partition.
		 */
		p -= 4;
		for (i=0; i<4; i++, p++)
		  if(NR_SECTS(p) && is_extended_partition(p))
		    break;
		if (i == 4)
		  goto done;	 /* nothing left to do */

		hd->part[current_minor].nr_sects = NR_SECTS(p);
		hd->part[current_minor].start_sect = first_sector + START_SECT(p);
		this_sector = first_sector + START_SECT(p);
		dev = MKDEV(hd->major, current_minor);
		brelse(bh);
	}
done:
	brelse(bh);
}
#ifdef CONFIG_SOLARIS_X86_PARTITION
static void
solaris_x86_partition(struct gendisk *hd, kdev_t dev, long offset) {

	struct buffer_head *bh;
	struct solaris_x86_vtoc *v;
	struct solaris_x86_slice *s;
	int i;

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev))))
		return;
	v = (struct solaris_x86_vtoc *)(bh->b_data + 512);
	if(v->v_sanity != SOLARIS_X86_VTOC_SANE) {
		brelse(bh);
		return;
	}
	printk(" <solaris:");
	if(v->v_version != 1) {
		printk("  cannot handle version %ld vtoc>", v->v_version);
		brelse(bh);
		return;
	}
	for(i=0; i<SOLARIS_X86_NUMSLICE; i++) {
		s = &v->v_slice[i];

		if (s->s_size == 0)
			continue;
		printk(" [s%d]", i);
		/* solaris partitions are relative to current MS-DOS
		 * one but add_partition starts relative to sector
		 * zero of the disk.  Therefore, must add the offset
		 * of the current partition */
		add_partition(hd, current_minor, s->s_start+offset, s->s_size);
		current_minor++;
	}
	brelse(bh);
	printk(" >");
}
#endif

#ifdef CONFIG_BSD_DISKLABEL
static void check_and_add_bsd_partition(struct gendisk *hd, struct bsd_partition *bsd_p)
{
	struct hd_struct *lin_p;
		/* check relative position of partitions.  */
	for (lin_p = hd->part + 1; lin_p - hd->part < current_minor; lin_p++) {
			/* no relationship -> try again */
		if (lin_p->start_sect + lin_p->nr_sects <= bsd_p->p_offset 
			|| lin_p->start_sect >= bsd_p->p_offset + bsd_p->p_size)
			continue;	
			/* equal -> no need to add */
		if (lin_p->start_sect == bsd_p->p_offset && 
			lin_p->nr_sects == bsd_p->p_size) 
			return;
			/* bsd living within dos partition */
		if (lin_p->start_sect <= bsd_p->p_offset && lin_p->start_sect 
			+ lin_p->nr_sects >= bsd_p->p_offset + bsd_p->p_size) {
#ifdef DEBUG_BSD_DISKLABEL
			printk("w: %d %ld+%ld,%d+%d", 
				lin_p - hd->part, 
				lin_p->start_sect, lin_p->nr_sects, 
				bsd_p->p_offset, bsd_p->p_size);
#endif
			break;
		}
	 /* ouch: bsd and linux overlap. Don't even try for that partition */
#ifdef DEBUG_BSD_DISKLABEL
		printk("???: %d %ld+%ld,%d+%d",
			lin_p - hd->part, lin_p->start_sect, lin_p->nr_sects,
			bsd_p->p_offset, bsd_p->p_size);
#endif
		printk("???");
		return;
	} /* if the bsd partition is not currently known to linux, we end
	   * up here 
	   */
	add_partition(hd, current_minor, bsd_p->p_offset, bsd_p->p_size);
	current_minor++;
}
/* 
 * Create devices for BSD partitions listed in a disklabel, under a
 * dos-like partition. See extended_partition() for more information.
 */
static void bsd_disklabel_partition(struct gendisk *hd, kdev_t dev, 
   int max_partitions)
{
	struct buffer_head *bh;
	struct bsd_disklabel *l;
	struct bsd_partition *p;
	int mask = (1 << hd->minor_shift) - 1;

	if (!(bh = bread(dev,0,get_ptable_blocksize(dev))))
		return;
	bh->b_state = 0;
	l = (struct bsd_disklabel *) (bh->b_data+512);
	if (l->d_magic != BSD_DISKMAGIC) {
		brelse(bh);
		return;
	}

	if (l->d_npartitions < max_partitions)
		max_partitions = l->d_npartitions;
	for (p = l->d_partitions; p - l->d_partitions <  max_partitions; p++) {
		if ((current_minor & mask) >= (4 + hd->max_p))
			break;

		if (p->p_fstype != BSD_FS_UNUSED) 
			check_and_add_bsd_partition(hd, p);
	}
	brelse(bh);

}
#endif

#ifdef CONFIG_UNIXWARE_DISKLABEL
/*
 * Create devices for Unixware partitions listed in a disklabel, under a
 * dos-like partition. See extended_partition() for more information.
 */
static void unixware_partition(struct gendisk *hd, kdev_t dev)
{
	struct buffer_head *bh;
	struct unixware_disklabel *l;
	struct unixware_slice *p;
	int mask = (1 << hd->minor_shift) - 1;

	if (!(bh = bread(dev, 14, get_ptable_blocksize(dev))))
		return;
	bh->b_state = 0;
	l = (struct unixware_disklabel *) (bh->b_data+512);
	if (le32_to_cpu(l->d_magic) != UNIXWARE_DISKMAGIC ||
	    le32_to_cpu(l->vtoc.v_magic) != UNIXWARE_DISKMAGIC2) {
		brelse(bh);
		return;
	}
	printk(" <unixware:");
	p = &l->vtoc.v_slice[1];
	/* I omit the 0th slice as it is the same as whole disk. */
	while (p - &l->vtoc.v_slice[0] < UNIXWARE_NUMSLICE) {
		if ((current_minor & mask) == 0)
			break;

		if (p->s_label != UNIXWARE_FS_UNUSED) {
			add_partition(hd, current_minor, START_SECT(p), NR_SECTS(p));
			current_minor++;
		}
		p++;
	}
	brelse(bh);
	printk(" >");
}
#endif

static int msdos_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, minor = current_minor;
	struct buffer_head *bh;
	struct partition *p;
	unsigned char *data;
	int mask = (1 << hd->minor_shift) - 1;
#ifdef CONFIG_BSD_DISKLABEL
	/* no bsd disklabel as a default */
	kdev_t bsd_kdev = 0;
	int bsd_maxpart = BSD_MAXPARTITIONS;
#endif
#ifdef CONFIG_BLK_DEV_IDE
	int tested_for_xlate = 0;

read_mbr:
#endif
	if (!(bh = bread(dev,0,get_ptable_blocksize(dev)))) {
		printk(" unable to read partition table\n");
		return -1;
	}
	data = bh->b_data;
	/* In some cases we modify the geometry    */
	/*  of the drive (below), so ensure that   */
	/*  nobody else tries to re-use this data. */
	bh->b_state = 0;
#ifdef CONFIG_BLK_DEV_IDE
check_table:
#endif
	if (*(unsigned short *)  (0x1fe + data) != cpu_to_le16(MSDOS_LABEL_MAGIC)) {
		brelse(bh);
		return 0;
	}
	p = (struct partition *) (0x1be + data);

#ifdef CONFIG_BLK_DEV_IDE
	if (!tested_for_xlate++) {	/* Do this only once per disk */
		/*
		 * Look for various forms of IDE disk geometry translation
		 */
		extern int ide_xlate_1024(kdev_t, int, const char *);
		unsigned int sig = le16_to_cpu(*(unsigned short *)(data + 2));
		if (SYS_IND(p) == EZD_PARTITION) {
			/*
			 * The remainder of the disk must be accessed using
			 * a translated geometry that reduces the number of 
			 * apparent cylinders to less than 1024 if possible.
			 *
			 * ide_xlate_1024() will take care of the necessary
			 * adjustments to fool fdisk/LILO and partition check.
			 */
			if (ide_xlate_1024(dev, -1, " [EZD]")) {
				data += 512;
				goto check_table;
			}
		} else if (SYS_IND(p) == DM6_PARTITION) {

			/*
			 * Everything on the disk is offset by 63 sectors,
			 * including a "new" MBR with its own partition table,
			 * and the remainder of the disk must be accessed using
			 * a translated geometry that reduces the number of 
			 * apparent cylinders to less than 1024 if possible.
			 *
			 * ide_xlate_1024() will take care of the necessary
			 * adjustments to fool fdisk/LILO and partition check.
			 */
			if (ide_xlate_1024(dev, 1, " [DM6:DDO]")) {
				brelse(bh);
				goto read_mbr;	/* start over with new MBR */
			}
		} else if (sig <= 0x1ae &&
			   *(unsigned short *)(data + sig) == cpu_to_le16(0x55AA) &&
			   (1 & *(unsigned char *)(data + sig + 2))) {
			/* DM6 signature in MBR, courtesy of OnTrack */
			(void) ide_xlate_1024 (dev, 0, " [DM6:MBR]");
		} else if (SYS_IND(p) == DM6_AUX1PARTITION || SYS_IND(p) == DM6_AUX3PARTITION) {
			/*
			 * DM6 on other than the first (boot) drive
			 */
			(void) ide_xlate_1024(dev, 0, " [DM6:AUX]");
		} else {
			/*
			 * Examine the partition table for common translations.
			 * This is useful for drives in situations where the
			 * translated geometry is unavailable from the BIOS.
			 */
			for (i = 0; i < 4; i++) {
				struct partition *q = &p[i];
				if (NR_SECTS(q)
				   && (q->sector & 63) == 1
				   && (q->end_sector & 63) == 63) {
					unsigned int heads = q->end_head + 1;
					if (heads == 32 || heads == 64 ||
					    heads == 128 || heads == 240 ||
					    heads == 255) {
						(void) ide_xlate_1024(dev, heads, " [PTBL]");
						break;
					}
				}
			}
		}
	}
#endif	/* CONFIG_BLK_DEV_IDE */

	current_minor += 4;  /* first "extra" minor (for extended partitions) */
	for (i=1 ; i<=4 ; minor++,i++,p++) {
		if (!NR_SECTS(p))
			continue;
		add_partition(hd, minor, first_sector+START_SECT(p), NR_SECTS(p));
		if (is_extended_partition(p)) {
			printk(" <");
			/*
			 * If we are rereading the partition table, we need
			 * to set the size of the partition so that we will
			 * be able to bread the block containing the extended
			 * partition info.
			 */
			hd->sizes[minor] = hd->part[minor].nr_sects 
			  	>> (BLOCK_SIZE_BITS - 9);
			extended_partition(hd, MKDEV(hd->major, minor));
			printk(" >");
			/* prevent someone doing mkfs or mkswap on an
			   extended partition, but leave room for LILO */
			if (hd->part[minor].nr_sects > 2)
				hd->part[minor].nr_sects = 2;
		}
#ifdef CONFIG_BSD_DISKLABEL
			/* tag first disklabel for late recognition */
		if (SYS_IND(p) == BSD_PARTITION || SYS_IND(p) == NETBSD_PARTITION) {
			printk("!");
			if (!bsd_kdev)
				bsd_kdev = MKDEV(hd->major, minor);
		} else if (SYS_IND(p) == OPENBSD_PARTITION) {
			printk("!");
			if (!bsd_kdev) {
				bsd_kdev = MKDEV(hd->major, minor);
				bsd_maxpart = OPENBSD_MAXPARTITIONS;
			}
		}
#endif
#ifdef CONFIG_UNIXWARE_DISKLABEL
		if (SYS_IND(p) == UNIXWARE_PARTITION)
			unixware_partition(hd, MKDEV(hd->major, minor));
#endif
#ifdef CONFIG_SOLARIS_X86_PARTITION

		/* james@bpgc.com: Solaris has a nasty indicator: 0x82
		 * which also means linux swap.  For that reason, all
		 * of the prints are done inside the
		 * solaris_x86_partition routine */

		if(SYS_IND(p) == SOLARIS_X86_PARTITION) {
			solaris_x86_partition(hd, MKDEV(hd->major, minor),
					      first_sector+START_SECT(p));
		}
#endif
	}
#ifdef CONFIG_BSD_DISKLABEL
	if (bsd_kdev) {
		printk(" <");
		bsd_disklabel_partition(hd, bsd_kdev, bsd_maxpart);
		printk(" >");
	}
#endif
	/*
	 *  Check for old-style Disk Manager partition table
	 */
	if (*(unsigned short *) (data+0xfc) == cpu_to_le16(MSDOS_LABEL_MAGIC)) {
		p = (struct partition *) (0x1be + data);
		for (i = 4 ; i < 16 ; i++, current_minor++) {
			p--;
			if ((current_minor & mask) == 0)
				break;
			if (!(START_SECT(p) && NR_SECTS(p)))
				continue;
			add_partition(hd, current_minor, START_SECT(p), NR_SECTS(p));
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
	int mask = (1 << hd->minor_shift) - 1;
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

	if (!(bh = bread(dev,0,get_ptable_blocksize(dev)))) {
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
		if ((current_minor & mask) == 0)
		        break;
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

#ifdef CONFIG_SUN_PARTITION

static int sun_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, csum;
	unsigned short *ush;
	struct buffer_head *bh;
	struct sun_disklabel {
		unsigned char info[128];   /* Informative text string */
		unsigned char spare[292];  /* Boot information etc. */
		unsigned short rspeed;     /* Disk rotational speed */
		unsigned short pcylcount;  /* Physical cylinder count */
		unsigned short sparecyl;   /* extra sects per cylinder */
		unsigned char spare2[4];   /* More magic... */
		unsigned short ilfact;     /* Interleave factor */
		unsigned short ncyl;       /* Data cylinder count */
		unsigned short nacyl;      /* Alt. cylinder count */
		unsigned short ntrks;      /* Tracks per cylinder */
		unsigned short nsect;      /* Sectors per track */
		unsigned char spare3[4];   /* Even more magic... */
		struct sun_partition {
			__u32 start_cylinder;
			__u32 num_sectors;
		} partitions[8];
		unsigned short magic;      /* Magic number */
		unsigned short csum;       /* Label xor'd checksum */
	} * label;		
	struct sun_partition *p;
	unsigned long spc;
#define SUN_LABEL_MAGIC          0xDABE

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev)))) {
		printk("Dev %s: unable to read partition table\n",
		       kdevname(dev));
		return -1;
	}
	label = (struct sun_disklabel *) bh->b_data;
	p = label->partitions;
	if (be16_to_cpu(label->magic) != SUN_LABEL_MAGIC) {
		printk("Dev %s Sun disklabel: bad magic %04x\n",
		       kdevname(dev), be16_to_cpu(label->magic));
		brelse(bh);
		return 0;
	}
	/* Look at the checksum */
	ush = ((unsigned short *) (label+1)) - 1;
	for(csum = 0; ush >= ((unsigned short *) label);)
		csum ^= *ush--;
	if(csum) {
		printk("Dev %s Sun disklabel: Csum bad, label corrupted\n",
		       kdevname(dev));
		brelse(bh);
		return 0;
	}
	/* All Sun disks have 8 partition entries */
	spc = be16_to_cpu(label->ntrks) * be16_to_cpu(label->nsect);
	for(i=0; i < 8; i++, p++) {
		unsigned long st_sector;
		int num_sectors;

		st_sector = first_sector + be32_to_cpu(p->start_cylinder) * spc;
		num_sectors = be32_to_cpu(p->num_sectors);
		if (num_sectors)
			add_partition(hd, current_minor, st_sector, num_sectors);
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif /* CONFIG_SUN_PARTITION */

#ifdef CONFIG_SGI_PARTITION

static int sgi_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, csum;
	unsigned int *ui;
	struct buffer_head *bh;
	struct sgi_disklabel {
		int magic_mushroom;         /* Big fat spliff... */
		short root_part_num;        /* Root partition number */
		short swap_part_num;        /* Swap partition number */
		char boot_file[16];         /* Name of boot file for ARCS */
		unsigned char _unused0[48]; /* Device parameter useless crapola.. */
		struct sgi_volume {
			char name[8];       /* Name of volume */
			int  block_num;     /* Logical block number */
			int  num_bytes;     /* How big, in bytes */
		} volume[15];
		struct sgi_partition {
			int num_blocks;     /* Size in logical blocks */
			int first_block;    /* First logical block */
			int type;           /* Type of this partition */
		} partitions[16];
		int csum;                   /* Disk label checksum */
		int _unused1;               /* Padding */
	} *label;
	struct sgi_partition *p;
#define SGI_LABEL_MAGIC 0x0be5a941

	if(!(bh = bread(dev, 0, 1024))) {
		printk("Dev %s: unable to read partition table\n", kdevname(dev));
		return -1;
	}
	label = (struct sgi_disklabel *) bh->b_data;
	p = &label->partitions[0];
	if(label->magic_mushroom != SGI_LABEL_MAGIC) {
		printk("Dev %s SGI disklabel: bad magic %08x\n",
		       kdevname(dev), label->magic_mushroom);
		brelse(bh);
		return 0;
	}
	ui = ((unsigned int *) (label + 1)) - 1;
	for(csum = 0; ui >= ((unsigned int *) label);)
		csum += *ui--;
	if(csum) {
		printk("Dev %s SGI disklabel: csum bad, label corrupted\n",
		       kdevname(dev));
		brelse(bh);
		return 0;
	}
	/* All SGI disk labels have 16 partitions, disks under Linux only
	 * have 15 minor's.  Luckily there are always a few zero length
	 * partitions which we don't care about so we never overflow the
	 * current_minor.
	 */
	for(i = 0; i < 16; i++, p++) {
		if(!(p->num_blocks))
			continue;
		add_partition(hd, current_minor, p->first_block, p->num_blocks);
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif

#ifdef CONFIG_AMIGA_PARTITION
#include <asm/byteorder.h>
#include <linux/affs_hardblocks.h>

static __inline__ u32
checksum_block(u32 *m, int size)
{
	u32 sum = 0;

	while (size--)
		sum += htonl(*m++);
	return sum;
}

static int
amiga_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	struct buffer_head	*bh;
	struct RigidDiskBlock	*rdb;
	struct PartitionBlock	*pb;
	int			 start_sect;
	int			 nr_sects;
	int			 blk;
	int			 part, res;

	set_blocksize(dev,512);
	res = 0;

	for (blk = 0; blk < RDB_ALLOCATION_LIMIT; blk++) {
		if(!(bh = bread(dev,blk,512))) {
			printk("Dev %s: unable to read RDB block %d\n",
			       kdevname(dev),blk);
			goto rdb_done;
		}
		if (*(u32 *)bh->b_data == htonl(IDNAME_RIGIDDISK)) {
			rdb = (struct RigidDiskBlock *)bh->b_data;
			if (checksum_block((u32 *)bh->b_data,htonl(rdb->rdb_SummedLongs) & 0x7F)) {
				printk("Dev %s: RDB in block %d has bad checksum\n",
				       kdevname(dev),blk);
				brelse(bh);
				continue;
			}
			printk(" RDSK");
			blk = htonl(rdb->rdb_PartitionList);
			brelse(bh);
			for (part = 1; blk > 0 && part <= 16; part++) {
				if (!(bh = bread(dev,blk,512))) {
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
						continue;
					}
					start_sect = htonl(pb->pb_Environment[9]) *
						     htonl(pb->pb_Environment[3]) *
						     htonl(pb->pb_Environment[5]);
					add_partition(hd,current_minor,start_sect,nr_sects);
					current_minor++;
					res = 1;
				}
				brelse(bh);
			}
			printk("\n");
			break;
		}
	}

rdb_done:
	set_blocksize(dev,BLOCK_SIZE);
	return res;
}
#endif /* CONFIG_AMIGA_PARTITION */

#ifdef CONFIG_MAC_PARTITION
#include <linux/ctype.h>

/*
 * Code to understand MacOS partition tables.
 */

#define MAC_PARTITION_MAGIC	0x504d

/* type field value for A/UX or other Unix partitions */
#define APPLE_AUX_TYPE	"Apple_UNIX_SVR2"

struct mac_partition {
	__u16	signature;	/* expected to be MAC_PARTITION_MAGIC */
	__u16	res1;
	__u32	map_count;	/* # blocks in partition map */
	__u32	start_block;	/* absolute starting block # of partition */
	__u32	block_count;	/* number of blocks in partition */
	char	name[32];	/* partition name */
	char	type[32];	/* string type description */
	__u32	data_start;	/* rel block # of first data block */
	__u32	data_count;	/* number of data blocks */
	__u32	status;		/* partition status bits */
	__u32	boot_start;
	__u32	boot_size;
	__u32	boot_load;
	__u32	boot_load2;
	__u32	boot_entry;
	__u32	boot_entry2;
	__u32	boot_cksum;
	char	processor[16];	/* identifies ISA of boot */
	/* there is more stuff after this that we don't need */
};

#define MAC_STATUS_BOOTABLE	8	/* partition is bootable */

#define MAC_DRIVER_MAGIC	0x4552

/* Driver descriptor structure, in block 0 */
struct mac_driver_desc {
	__u16	signature;	/* expected to be MAC_DRIVER_MAGIC */
	__u16	block_size;
	__u32	block_count;
    /* ... more stuff */
};

static int mac_partition(struct gendisk *hd, kdev_t dev, unsigned long fsec)
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
		add_partition(hd, current_minor,
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

		++current_minor;
	}
	brelse(bh);
	printk("\n");
	return 1;
}

#endif /* CONFIG_MAC_PARTITION */

#ifdef CONFIG_ATARI_PARTITION
#include <asm/atari_rootsec.h>

/* ++guenther: this should be settable by the user ("make config")?.
 */
#define ICD_PARTS

static int atari_partition (struct gendisk *hd, kdev_t dev,
			    unsigned long first_sector)
{
  int minor = current_minor, m_lim = current_minor + hd->max_p;
  struct buffer_head *bh;
  struct rootsector *rs;
  struct partition_info *pi;
  ulong extensect;
#ifdef ICD_PARTS
  int part_fmt = 0; /* 0:unknown, 1:AHDI, 2:ICD/Supra */
#endif

  bh = bread (dev, 0, get_ptable_blocksize(dev));
  if (!bh)
    {
      printk (" unable to read block 0\n");
      return -1;
    }

  rs = (struct rootsector *) bh->b_data;
  pi = &rs->part[0];
  printk (" AHDI");
  for (; pi < &rs->part[4] && minor < m_lim; minor++, pi++)
    {
      if (pi->flg & 1)
	/* active partition */
	{
	  if (memcmp (pi->id, "XGM", 3) == 0)
	    /* extension partition */
	    {
	      struct rootsector *xrs;
	      struct buffer_head *xbh;
	      ulong partsect;

#ifdef ICD_PARTS
	      part_fmt = 1;
#endif
	      printk(" XGM<");
	      partsect = extensect = pi->st;
	      while (1)
		{
		  xbh = bread (dev, partsect / 2, 1024);
		  if (!xbh)
		    {
		      printk (" block %ld read failed\n", partsect);
		      brelse(bh);
		      return 0;
		    }
		  if (partsect & 1)
		    xrs = (struct rootsector *) &xbh->b_data[512];
		  else
		    xrs = (struct rootsector *) &xbh->b_data[0];

		  /* ++roman: sanity check: bit 0 of flg field must be set */
		  if (!(xrs->part[0].flg & 1)) {
		    printk( "\nFirst sub-partition in extended partition is not valid!\n" );
		    break;
		  }

		  add_partition(hd, minor, partsect + xrs->part[0].st,
				xrs->part[0].siz);

		  if (!(xrs->part[1].flg & 1)) {
		    /* end of linked partition list */
		    brelse( xbh );
		    break;
		  }
		  if (memcmp( xrs->part[1].id, "XGM", 3 ) != 0) {
		    printk( "\nID of extended partition is not XGM!\n" );
		    brelse( xbh );
		    break;
		  }

		  partsect = xrs->part[1].st + extensect;
		  brelse (xbh);
		  minor++;
		  if (minor >= m_lim) {
		    printk( "\nMaximum number of partitions reached!\n" );
		    break;
		  }
		}
	      printk(" >");
	    }
	  else
	    {
	      /* we don't care about other id's */
	      add_partition (hd, minor, pi->st, pi->siz);
	    }
	}
    }
#ifdef ICD_PARTS
  if ( part_fmt!=1 ) /* no extended partitions -> test ICD-format */
  {
    pi = &rs->icdpart[0];
    /* sanity check: no ICD format if first partition invalid */
    if (memcmp (pi->id, "GEM", 3) == 0 ||
        memcmp (pi->id, "BGM", 3) == 0 ||
        memcmp (pi->id, "LNX", 3) == 0 ||
        memcmp (pi->id, "SWP", 3) == 0 ||
        memcmp (pi->id, "RAW", 3) == 0 )
    {
      printk(" ICD<");
      for (; pi < &rs->icdpart[8] && minor < m_lim; minor++, pi++)
      {
        /* accept only GEM,BGM,RAW,LNX,SWP partitions */
        if (pi->flg & 1 && 
            (memcmp (pi->id, "GEM", 3) == 0 ||
             memcmp (pi->id, "BGM", 3) == 0 ||
             memcmp (pi->id, "LNX", 3) == 0 ||
             memcmp (pi->id, "SWP", 3) == 0 ||
             memcmp (pi->id, "RAW", 3) == 0) )
        {
          part_fmt = 2;
	  add_partition (hd, minor, pi->st, pi->siz);
        }
      }
      printk(" >");
    }
  }
#endif
  brelse (bh);

  printk ("\n");

  return 1;
}
#endif /* CONFIG_ATARI_PARTITION */

static void check_partition(struct gendisk *hd, kdev_t dev)
{
	static int first_time = 1;
	unsigned long first_sector;
	char buf[8];

	if (first_time)
		printk("Partition check:\n");
	first_time = 0;
	first_sector = hd->part[MINOR(dev)].start_sect;

	/*
	 * This is a kludge to allow the partition check to be
	 * skipped for specific drives (e.g. IDE CD-ROM drives)
	 */
	if ((int)first_sector == -1) {
		hd->part[MINOR(dev)].start_sect = 0;
		return;
	}

	printk(" %s:", disk_name(hd, MINOR(dev), buf));
#ifdef CONFIG_MSDOS_PARTITION
	if (msdos_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_OSF_PARTITION
	if (osf_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_SUN_PARTITION
	if(sun_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_AMIGA_PARTITION
	if(amiga_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_ATARI_PARTITION
	if(atari_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_MAC_PARTITION
	if (mac_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_SGI_PARTITION
	if(sgi_partition(hd, dev, first_sector))
		return;
#endif
	printk(" unknown partition table\n");
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
	int first_minor	= drive << dev->minor_shift;
	int end_minor	= first_minor + dev->max_p;

	blk_size[dev->major] = NULL;
	current_minor = 1 + first_minor;
	check_partition(dev, MKDEV(dev->major, first_minor));

 	/*
 	 * We need to set the sizes array before we will be able to access
 	 * any of the partitions on this device.
 	 */
	if (dev->sizes != NULL) {	/* optional safeguard in ll_rw_blk.c */
		for (i = first_minor; i < end_minor; i++)
			dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
		blk_size[dev->major] = dev->sizes;
	}
}

static inline void setup_dev(struct gendisk *dev)
{
	int i, drive;
	int end_minor	= dev->max_nr * dev->max_p;

	blk_size[dev->major] = NULL;
	for (i = 0 ; i < end_minor; i++) {
		dev->part[i].start_sect = 0;
		dev->part[i].nr_sects = 0;
	}
	dev->init(dev);	
	for (drive = 0 ; drive < dev->nr_real ; drive++) {
		int first_minor	= drive << dev->minor_shift;
		current_minor = 1 + first_minor;
		check_partition(dev, MKDEV(dev->major, first_minor));
	}
	if (dev->sizes != NULL) {	/* optional safeguard in ll_rw_blk.c */
		for (i = 0; i < end_minor; i++)
			dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
		blk_size[dev->major] = dev->sizes;
	}
}

__initfunc(void device_setup(void))
{
	extern void console_map_init(void);
#ifdef CONFIG_PARPORT
	extern int parport_init(void);
#endif
#ifdef CONFIG_MD_BOOT
        extern void md_setup_drive(void) __init;
#endif
#ifdef CONFIG_FC4_SOC
	extern int soc_probe(void);
#endif
	struct gendisk *p;

#ifdef CONFIG_PARPORT
	parport_init();
#endif
	chr_dev_init();
	blk_dev_init();
	sti();
#ifdef CONFIG_FC4_SOC
	/* This has to be done before scsi_dev_init */
	soc_probe();
#endif
#ifdef CONFIG_SCSI
	scsi_dev_init();
#endif
#ifdef CONFIG_INET
	net_dev_init();
#endif
#ifdef CONFIG_VT
	console_map_init();
#endif

	for (p = gendisk_head ; p ; p=p->next)
		setup_dev(p);

#ifdef CONFIG_BLK_DEV_RAM
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && mount_initrd) initrd_load();
	else
#endif
	rd_load();
#endif
#ifdef CONFIG_MD_BOOT
        md_setup_drive();
#endif
}

#ifdef CONFIG_PROC_FS
int get_partition_list(char * page)
{
	struct gendisk *p;
	char buf[32];
	int n, len;

	len = sprintf(page, "major minor  #blocks  name\n\n");
	for (p = gendisk_head; p; p = p->next) {
		for (n=0; n < (p->nr_real << p->minor_shift); n++) {
			if (p->part[n].nr_sects && len < PAGE_SIZE - 80) {
				len += sprintf(page+len,
					       "%4d  %4d %10d %s\n",
					       p->major, n, p->sizes[n],
					       disk_name(p, n, buf));
			}
		}
	}
	return len;
}
#endif
