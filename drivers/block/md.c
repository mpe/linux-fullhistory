
/*
   md.c : Multiple Devices driver for Linux
          Copyright (C) 1994-96 Marc ZYNGIER
	  <zyngier@ufr-info-p7.ibp.fr> or
	  <maz@gloups.fdn.fr>

   A lot of inspiration came from hd.c ...

   kerneld support by Boris Tobotras <boris@xtalk.msk.su>
   boot support for linear and striped mode by Harald Hoyer <HarryH@Royal.Net>

   RAID-1/RAID-5 extensions by:
        Ingo Molnar, Miguel de Icaza, Gadi Oxman

   Changes for kmod by:
   	Cyrus Durgin
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

/*
 * Current RAID-1,4,5 parallel reconstruction speed limit is 1024 KB/sec, so
 * the extra system load does not show up that much. Increase it if your
 * system can take more.
 */
#define SPEED_LIMIT 1024

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/md.h>
#include <linux/hdreg.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/smp_lock.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <linux/errno.h>
#include <linux/init.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER

#include <linux/blk.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

#ifdef CONFIG_MD_BOOT
extern kdev_t name_to_kdev_t(char *line) __init;
#endif

static struct hd_struct md_hd_struct[MAX_MD_DEV];
static int md_blocksizes[MAX_MD_DEV];
int md_maxreadahead[MAX_MD_DEV];
#if SUPPORT_RECONSTRUCTION
static struct md_thread *md_sync_thread = NULL;
#endif /* SUPPORT_RECONSTRUCTION */

int md_size[MAX_MD_DEV]={0, };

static void md_geninit (struct gendisk *);

static struct gendisk md_gendisk=
{
  MD_MAJOR,
  "md",
  0,
  1,
  MAX_MD_DEV,
  md_geninit,
  md_hd_struct,
  md_size,
  MAX_MD_DEV,
  NULL,
  NULL
};

static struct md_personality *pers[MAX_PERSONALITY]={NULL, };
struct md_dev md_dev[MAX_MD_DEV];

int md_thread(void * arg);

static struct gendisk *find_gendisk (kdev_t dev)
{
  struct gendisk *tmp=gendisk_head;

  while (tmp != NULL)
  {
    if (tmp->major==MAJOR(dev))
      return (tmp);
    
    tmp=tmp->next;
  }

  return (NULL);
}

char *partition_name (kdev_t dev)
{
  static char name[40];		/* This should be long
				   enough for a device name ! */
  struct gendisk *hd = find_gendisk (dev);

  if (!hd)
  {
    sprintf (name, "[dev %s]", kdevname(dev));
    return (name);
  }

  return disk_name (hd, MINOR(dev), name);  /* routine in genhd.c */
}

static int legacy_raid_sb (int minor, int pnum)
{
	int i, factor;

	factor = 1 << FACTOR_SHIFT(FACTOR((md_dev+minor)));

	/*****
	 * do size and offset calculations.
	 */
	for (i=0; i<md_dev[minor].nb_dev; i++) {
		md_dev[minor].devices[i].size &= ~(factor - 1);
		md_size[minor] += md_dev[minor].devices[i].size;
		md_dev[minor].devices[i].offset=i ? (md_dev[minor].devices[i-1].offset + 
							md_dev[minor].devices[i-1].size) : 0;
	}
	if (pnum == RAID0 >> PERSONALITY_SHIFT)
		md_maxreadahead[minor] = MD_DEFAULT_DISK_READAHEAD * md_dev[minor].nb_dev;
	return 0;
}

static void free_sb (struct md_dev *mddev)
{
	int i;
	struct real_dev *realdev;

	if (mddev->sb) {
		free_page((unsigned long) mddev->sb);
		mddev->sb = NULL;
	}
	for (i = 0; i <mddev->nb_dev; i++) {
		realdev = mddev->devices + i;
		if (realdev->sb) {
			free_page((unsigned long) realdev->sb);
			realdev->sb = NULL;
		}
	}
}

/*
 * Check one RAID superblock for generic plausibility
 */

#define BAD_MAGIC KERN_ERR \
"md: %s: invalid raid superblock magic (%x) on block %u\n"

#define OUT_OF_MEM KERN_ALERT \
"md: out of memory.\n"

#define NO_DEVICE KERN_ERR \
"md: disabled device %s\n"

#define SUCCESS 0
#define FAILURE -1

static int analyze_one_sb (struct real_dev * rdev)
{
	int ret = FAILURE;
	struct buffer_head *bh;
	kdev_t dev = rdev->dev;
	md_superblock_t *sb;

	/*
	 * Read the superblock, it's at the end of the disk
	 */
	rdev->sb_offset = MD_NEW_SIZE_BLOCKS (blk_size[MAJOR(dev)][MINOR(dev)]);
	set_blocksize (dev, MD_SB_BYTES);
	bh = bread (dev, rdev->sb_offset / MD_SB_BLOCKS, MD_SB_BYTES);

	if (bh) {
		sb = (md_superblock_t *) bh->b_data;
		if (sb->md_magic != MD_SB_MAGIC) {
			printk (BAD_MAGIC, kdevname(dev),
					 sb->md_magic, rdev->sb_offset);
			goto abort;
		}
		rdev->sb = (md_superblock_t *) __get_free_page(GFP_KERNEL);
		if (!rdev->sb) {
			printk (OUT_OF_MEM);
			goto abort;
		}
		memcpy (rdev->sb, bh->b_data, MD_SB_BYTES);

		rdev->size = sb->size;
	} else
		printk (NO_DEVICE,kdevname(rdev->dev));
	ret = SUCCESS;
abort:
	if (bh)
		brelse (bh);
	return ret;
}

#undef SUCCESS
#undef FAILURE

#undef BAD_MAGIC
#undef OUT_OF_MEM
#undef NO_DEVICE

/*
 * Check a full RAID array for plausibility
 */

#define INCONSISTENT KERN_ERR \
"md: superblock inconsistency -- run ckraid\n"

#define OUT_OF_DATE KERN_ERR \
"md: superblock update time inconsistenty -- using the most recent one\n"

#define OLD_VERSION KERN_ALERT \
"md: %s: unsupported raid array version %d.%d.%d\n"

#define NOT_CLEAN KERN_ERR \
"md: %s: raid array is not clean -- run ckraid\n"

#define NOT_CLEAN_IGNORE KERN_ERR \
"md: %s: raid array is not clean -- reconstructing parity\n"

#define UNKNOWN_LEVEL KERN_ERR \
"md: %s: unsupported raid level %d\n"

static int analyze_sbs (int minor, int pnum)
{
	struct md_dev *mddev = md_dev + minor;
	int i, N = mddev->nb_dev, out_of_date = 0;
	struct real_dev * disks = mddev->devices;
	md_superblock_t *sb, *freshest = NULL;

	/*
	 * RAID-0 and linear don't use a RAID superblock
	 */
	if (pnum == RAID0 >> PERSONALITY_SHIFT ||
		pnum == LINEAR >> PERSONALITY_SHIFT)
			return legacy_raid_sb (minor, pnum);

	/*
	 * Verify the RAID superblock on each real device
	 */
	for (i = 0; i < N; i++)
		if (analyze_one_sb(disks+i))
			goto abort;

	/*
	 * The superblock constant part has to be the same
	 * for all disks in the array.
	 */
	sb = NULL;
	for (i = 0; i < N; i++) {
		if (!disks[i].sb)
			continue;
		if (!sb) {
			sb = disks[i].sb;
			continue;
		}
		if (memcmp(sb,
			   disks[i].sb, MD_SB_GENERIC_CONSTANT_WORDS * 4)) {
			printk (INCONSISTENT);
			goto abort;
		}
	}

	/*
	 * OK, we have all disks and the array is ready to run. Let's
	 * find the freshest superblock, that one will be the superblock
	 * that represents the whole array.
	 */
	if ((sb = mddev->sb = (md_superblock_t *) __get_free_page (GFP_KERNEL)) == NULL)
		goto abort;
	freshest = NULL;
	for (i = 0; i < N; i++) {
		if (!disks[i].sb)
			continue;
		if (!freshest) {
			freshest = disks[i].sb;
			continue;
		}
		/*
		 * Find the newest superblock version
		 */
		if (disks[i].sb->utime != freshest->utime) {
			out_of_date = 1;
			if (disks[i].sb->utime > freshest->utime)
				freshest = disks[i].sb;
		}
	}
	if (out_of_date)
		printk(OUT_OF_DATE);
	memcpy (sb, freshest, sizeof(*freshest));

	/*
	 * Check if we can support this RAID array
	 */
	if (sb->major_version != MD_MAJOR_VERSION ||
			sb->minor_version > MD_MINOR_VERSION) {

		printk (OLD_VERSION, kdevname(MKDEV(MD_MAJOR, minor)),
				sb->major_version, sb->minor_version,
				sb->patch_version);
		goto abort;
	}

	/*
	 * We need to add this as a superblock option.
	 */
#if SUPPORT_RECONSTRUCTION
	if (sb->state != (1 << MD_SB_CLEAN)) {
		if (sb->level == 1) {
			printk (NOT_CLEAN, kdevname(MKDEV(MD_MAJOR, minor)));
			goto abort;
		} else
			printk (NOT_CLEAN_IGNORE, kdevname(MKDEV(MD_MAJOR, minor)));
	}
#else
	if (sb->state != (1 << MD_SB_CLEAN)) {
		printk (NOT_CLEAN, kdevname(MKDEV(MD_MAJOR, minor)));
		goto abort;
	}
#endif /* SUPPORT_RECONSTRUCTION */

	switch (sb->level) {
		case 1:
			md_size[minor] = sb->size;
			md_maxreadahead[minor] = MD_DEFAULT_DISK_READAHEAD;
			break;
		case 4:
		case 5:
			md_size[minor] = sb->size * (sb->raid_disks - 1);
			md_maxreadahead[minor] = MD_DEFAULT_DISK_READAHEAD * (sb->raid_disks - 1);
			break;
		default:
			printk (UNKNOWN_LEVEL, kdevname(MKDEV(MD_MAJOR, minor)),
					sb->level);
			goto abort;
	}
	return 0;
abort:
	free_sb(mddev);
	return 1;
}

#undef INCONSISTENT
#undef OUT_OF_DATE
#undef OLD_VERSION
#undef NOT_CLEAN
#undef OLD_LEVEL

int md_update_sb(int minor)
{
	struct md_dev *mddev = md_dev + minor;
	struct buffer_head *bh;
	md_superblock_t *sb = mddev->sb;
	struct real_dev *realdev;
	kdev_t dev;
	int i;
	u32 sb_offset;

	sb->utime = CURRENT_TIME;
	for (i = 0; i < mddev->nb_dev; i++) {
		realdev = mddev->devices + i;
		if (!realdev->sb)
			continue;
		dev = realdev->dev;
		sb_offset = realdev->sb_offset;
		set_blocksize(dev, MD_SB_BYTES);
		printk("md: updating raid superblock on device %s, sb_offset == %u\n", kdevname(dev), sb_offset);
		bh = getblk(dev, sb_offset / MD_SB_BLOCKS, MD_SB_BYTES);
		if (bh) {
			sb = (md_superblock_t *) bh->b_data;
			memcpy(sb, mddev->sb, MD_SB_BYTES);
			memcpy(&sb->descriptor, sb->disks + realdev->sb->descriptor.number, MD_SB_DESCRIPTOR_WORDS * 4);
			mark_buffer_uptodate(bh, 1);
			mark_buffer_dirty(bh, 1);
			ll_rw_block(WRITE, 1, &bh);
			wait_on_buffer(bh);
			bforget(bh);
			fsync_dev(dev);
			invalidate_buffers(dev);
		} else
			printk(KERN_ERR "md: getblk failed for device %s\n", kdevname(dev));
	}
	return 0;
}

static int do_md_run (int minor, int repart)
{
  int pnum, i, min, factor, err;

  if (!md_dev[minor].nb_dev)
    return -EINVAL;
  
  if (md_dev[minor].pers)
    return -EBUSY;

  md_dev[minor].repartition=repart;
  
  if ((pnum=PERSONALITY(&md_dev[minor]) >> (PERSONALITY_SHIFT))
      >= MAX_PERSONALITY)
    return -EINVAL;

  /* Only RAID-1 and RAID-5 can have MD devices as underlying devices */
  if (pnum != (RAID1 >> PERSONALITY_SHIFT) && pnum != (RAID5 >> PERSONALITY_SHIFT)){
	  for (i = 0; i < md_dev [minor].nb_dev; i++)
		  if (MAJOR (md_dev [minor].devices [i].dev) == MD_MAJOR)
			  return -EINVAL;
  }
  if (!pers[pnum])
  {
#ifdef CONFIG_KMOD
    char module_name[80];
    sprintf (module_name, "md-personality-%d", pnum);
    request_module (module_name);
    if (!pers[pnum])
#endif
      return -EINVAL;
  }
  
  factor = min = 1 << FACTOR_SHIFT(FACTOR((md_dev+minor)));
  
  for (i=0; i<md_dev[minor].nb_dev; i++)
    if (md_dev[minor].devices[i].size<min)
    {
      printk ("Dev %s smaller than %dk, cannot shrink\n",
	      partition_name (md_dev[minor].devices[i].dev), min);
      return -EINVAL;
    }

  for (i=0; i<md_dev[minor].nb_dev; i++) {
    fsync_dev(md_dev[minor].devices[i].dev);
    invalidate_buffers(md_dev[minor].devices[i].dev);
  }
  
  /* Resize devices according to the factor. It is used to align
     partitions size on a given chunk size. */
  md_size[minor]=0;

  /*
   * Analyze the raid superblock
   */ 
  if (analyze_sbs(minor, pnum))
    return -EINVAL;

  md_dev[minor].pers=pers[pnum];
  
  if ((err=md_dev[minor].pers->run (minor, md_dev+minor)))
  {
    md_dev[minor].pers=NULL;
    free_sb(md_dev + minor);
    return (err);
  }

  if (pnum != RAID0 >> PERSONALITY_SHIFT && pnum != LINEAR >> PERSONALITY_SHIFT)
  {
    md_dev[minor].sb->state &= ~(1 << MD_SB_CLEAN);
    md_update_sb(minor);
  }

  /* FIXME : We assume here we have blocks
     that are twice as large as sectors.
     THIS MAY NOT BE TRUE !!! */
  md_hd_struct[minor].start_sect=0;
  md_hd_struct[minor].nr_sects=md_size[minor]<<1;
  
  read_ahead[MD_MAJOR] = 128;
  return (0);
}

static int do_md_stop (int minor, struct inode *inode)
{
	int i;
  
	if (inode->i_count>1 || md_dev[minor].busy>1) {
		/*
		 * ioctl : one open channel
		 */
		printk ("STOP_MD md%x failed : i_count=%d, busy=%d\n",
				minor, inode->i_count, md_dev[minor].busy);
		return -EBUSY;
	}
  
	if (md_dev[minor].pers) {
		/*
		 * It is safe to call stop here, it only frees private
		 * data. Also, it tells us if a device is unstoppable
		 * (eg. resyncing is in progress)
		 */
		if (md_dev[minor].pers->stop (minor, md_dev+minor))
			return -EBUSY;
		/*
		 *  The device won't exist anymore -> flush it now
		 */
		fsync_dev (inode->i_rdev);
		invalidate_buffers (inode->i_rdev);
		if (md_dev[minor].sb) {
			md_dev[minor].sb->state |= 1 << MD_SB_CLEAN;
			md_update_sb(minor);
		}
	}
  
	/* Remove locks. */
	if (md_dev[minor].sb)
	free_sb(md_dev + minor);
	for (i=0; i<md_dev[minor].nb_dev; i++)
		clear_inode (md_dev[minor].devices[i].inode);

	md_dev[minor].nb_dev=md_size[minor]=0;
	md_hd_struct[minor].nr_sects=0;
	md_dev[minor].pers=NULL;
  
	read_ahead[MD_MAJOR] = 128;
  
	return (0);
}

static int do_md_add (int minor, kdev_t dev)
{
	int i;
	int hot_add=0;
	struct real_dev *realdev;

	if (md_dev[minor].nb_dev==MAX_REAL)
		return -EINVAL;

	if (!fs_may_mount (dev))
		return -EBUSY;

	if (blk_size[MAJOR(dev)] == NULL || blk_size[MAJOR(dev)][MINOR(dev)] == 0) {
		printk("md_add(): zero device size, huh, bailing out.\n");
		return -EINVAL;
	}

	if (md_dev[minor].pers) {
		/*
		 * The array is already running, hot-add the drive, or
		 * bail out:
		 */
		if (!md_dev[minor].pers->hot_add_disk)
			return -EBUSY;
		else
			hot_add=1;
	}

	/*
	 * Careful. We cannot increase nb_dev for a running array.
	 */
	i=md_dev[minor].nb_dev;
	realdev = &md_dev[minor].devices[i];
	realdev->dev=dev;
  
	/* Lock the device by inserting a dummy inode. This doesn't
	   smell very good, but I need to be consistent with the
	   mount stuff, specially with fs_may_mount. If someone have
	   a better idea, please help ! */
  
	realdev->inode=get_empty_inode ();
	realdev->inode->i_dev=dev; 	/* don't care about other fields */
	insert_inode_hash (realdev->inode);
  
	/* Sizes are now rounded at run time */
  
/*  md_dev[minor].devices[i].size=gen_real->sizes[MINOR(dev)]; HACKHACK*/

	realdev->size=blk_size[MAJOR(dev)][MINOR(dev)];

	if (hot_add) {
		/*
		 * Check the superblock for consistency.
		 * The personality itself has to check whether it's getting
		 * added with the proper flags.  The personality has to be
                 * checked too. ;)
		 */
		if (analyze_one_sb (realdev))
			return -EINVAL;
		/*
		 * hot_add has to bump up nb_dev itself
		 */
		if (md_dev[minor].pers->hot_add_disk (&md_dev[minor], dev)) {
			/*
			 * FIXME: here we should free up the inode and stuff
			 */
			printk ("FIXME\n");
			return -EINVAL;
		}
	} else
		md_dev[minor].nb_dev++;

	printk ("REGISTER_DEV %s to md%x done\n", partition_name(dev), minor);
	return (0);
}

static int md_ioctl (struct inode *inode, struct file *file,
                     unsigned int cmd, unsigned long arg)
{
  int minor, err;
  struct hd_geometry *loc = (struct hd_geometry *) arg;

  if (!capable(CAP_SYS_ADMIN))
    return -EACCES;

  if (((minor=MINOR(inode->i_rdev)) & 0x80) &&
      (minor & 0x7f) < MAX_PERSONALITY &&
      pers[minor & 0x7f] &&
      pers[minor & 0x7f]->ioctl)
    return (pers[minor & 0x7f]->ioctl (inode, file, cmd, arg));
  
  if (minor >= MAX_MD_DEV)
    return -EINVAL;

  switch (cmd)
  {
    case REGISTER_DEV:
      return do_md_add (minor, to_kdev_t ((dev_t) arg));

    case START_MD:
      return do_md_run (minor, (int) arg);

    case STOP_MD:
      return do_md_stop (minor, inode);
      
    case BLKGETSIZE:   /* Return device size */
    if  (!arg)  return -EINVAL;
    err = put_user (md_hd_struct[MINOR(inode->i_rdev)].nr_sects, (long *) arg);
    if (err)
      return err;
    break;

    case BLKFLSBUF:
    fsync_dev (inode->i_rdev);
    invalidate_buffers (inode->i_rdev);
    break;

    case BLKRASET:
    if (arg > 0xff)
      return -EINVAL;
    read_ahead[MAJOR(inode->i_rdev)] = arg;
    return 0;
    
    case BLKRAGET:
    if  (!arg)  return -EINVAL;
    err = put_user (read_ahead[MAJOR(inode->i_rdev)], (long *) arg);
    if (err)
      return err;
    break;

    /* We have a problem here : there is no easy way to give a CHS
       virtual geometry. We currently pretend that we have a 2 heads
       4 sectors (with a BIG number of cylinders...). This drives dosfs
       just mad... ;-) */
    
    case HDIO_GETGEO:
    if (!loc)  return -EINVAL;
    err = put_user (2, (char *) &loc->heads);
    if (err)
      return err;
    err = put_user (4, (char *) &loc->sectors);
    if (err)
      return err;
    err = put_user (md_hd_struct[minor].nr_sects/8, (short *) &loc->cylinders);
    if (err)
      return err;
    err = put_user (md_hd_struct[MINOR(inode->i_rdev)].start_sect,
		(long *) &loc->start);
    if (err)
      return err;
    break;
    
    RO_IOCTLS(inode->i_rdev,arg);
    
    default:
    return -EINVAL;
  }

  return (0);
}

static int md_open (struct inode *inode, struct file *file)
{
  int minor=MINOR(inode->i_rdev);

  md_dev[minor].busy++;
  return (0);			/* Always succeed */
}


static int md_release (struct inode *inode, struct file *file)
{
  int minor=MINOR(inode->i_rdev);

  sync_dev (inode->i_rdev);
  md_dev[minor].busy--;
  return 0;
}


static ssize_t md_read (struct file *file, char *buf, size_t count,
			loff_t *ppos)
{
  int minor=MINOR(file->f_dentry->d_inode->i_rdev);

  if (!md_dev[minor].pers)	/* Check if device is being run */
    return -ENXIO;

  return block_read(file, buf, count, ppos);
}

static ssize_t md_write (struct file *file, const char *buf,
			 size_t count, loff_t *ppos)
{
  int minor=MINOR(file->f_dentry->d_inode->i_rdev);

  if (!md_dev[minor].pers)	/* Check if device is being run */
    return -ENXIO;

  return block_write(file, buf, count, ppos);
}

static struct file_operations md_fops=
{
  NULL,
  md_read,
  md_write,
  NULL,
  NULL,
  md_ioctl,
  NULL,
  md_open,
  NULL,
  md_release,
  block_fsync
};

int md_map (int minor, kdev_t *rdev, unsigned long *rsector, unsigned long size)
{
  if ((unsigned int) minor >= MAX_MD_DEV)
  {
    printk ("Bad md device %d\n", minor);
    return (-1);
  }
  
  if (!md_dev[minor].pers)
  {
    printk ("Oops ! md%d not running, giving up !\n", minor);
    return (-1);
  }

  return (md_dev[minor].pers->map(md_dev+minor, rdev, rsector, size));
}
  
int md_make_request (int minor, int rw, struct buffer_head * bh)
{
	if (md_dev [minor].pers->make_request) {
		if (buffer_locked(bh))
			return 0;
		set_bit(BH_Lock, &bh->b_state);
		if (rw == WRITE || rw == WRITEA) {
			if (!buffer_dirty(bh)) {
				bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
				return 0;
			}
		}
		if (rw == READ || rw == READA) {
			if (buffer_uptodate(bh)) {
				bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
				return 0;
			}
		}
		return (md_dev[minor].pers->make_request(md_dev+minor, rw, bh));
	} else {
		make_request (MAJOR(bh->b_rdev), rw, bh);
		return 0;
	}
}

static void do_md_request (void)
{
  printk ("Got md request, not good...");
  return;
}

void md_wakeup_thread(struct md_thread *thread)
{
	set_bit(THREAD_WAKEUP, &thread->flags);
	wake_up(&thread->wqueue);
}

struct md_thread *md_register_thread (void (*run) (void *), void *data)
{
	struct md_thread *thread = (struct md_thread *)
		kmalloc(sizeof(struct md_thread), GFP_KERNEL);
	int ret;
	struct semaphore sem = MUTEX_LOCKED;
	
	if (!thread) return NULL;
	
	memset(thread, 0, sizeof(struct md_thread));
	init_waitqueue(&thread->wqueue);
	
	thread->sem = &sem;
	thread->run = run;
	thread->data = data;
	ret = kernel_thread(md_thread, thread, 0);
	if (ret < 0) {
		kfree(thread);
		return NULL;
	}
	down(&sem);
	return thread;
}

void md_unregister_thread (struct md_thread *thread)
{
	struct semaphore sem = MUTEX_LOCKED;
	
	thread->sem = &sem;
	thread->run = NULL;
	if (thread->tsk)
		printk("Killing md_thread %d %p %s\n",
		       thread->tsk->pid, thread->tsk, thread->tsk->comm);
	else
		printk("Aiee. md_thread has 0 tsk\n");
	send_sig(SIGKILL, thread->tsk, 1);
	printk("downing on %p\n", &sem);
	down(&sem);
}

#define SHUTDOWN_SIGS   (sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM))

int md_thread(void * arg)
{
	struct md_thread *thread = arg;

	lock_kernel();
	exit_mm(current);
	exit_files(current);
	exit_fs(current);
	
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "md_thread");
	siginitsetinv(&current->blocked, SHUTDOWN_SIGS);
	thread->tsk = current;
	up(thread->sem);

	for (;;) {
		cli();
		if (!test_bit(THREAD_WAKEUP, &thread->flags)) {
			do {
			        spin_lock(&current->sigmask_lock);
				flush_signals(current);
	  			spin_unlock(&current->sigmask_lock);
				interruptible_sleep_on(&thread->wqueue);
				cli();
				if (test_bit(THREAD_WAKEUP, &thread->flags))
					break;
				if (!thread->run) {
					sti();
					up(thread->sem);
					return 0;
				}
			} while (signal_pending(current));
		}
		sti();
		clear_bit(THREAD_WAKEUP, &thread->flags);
		if (thread->run) {
			thread->run(thread->data);
			run_task_queue(&tq_disk);
		}
	}
}

EXPORT_SYMBOL(md_size);
EXPORT_SYMBOL(md_maxreadahead);
EXPORT_SYMBOL(register_md_personality);
EXPORT_SYMBOL(unregister_md_personality);
EXPORT_SYMBOL(partition_name);
EXPORT_SYMBOL(md_dev);
EXPORT_SYMBOL(md_error);
EXPORT_SYMBOL(md_register_thread);
EXPORT_SYMBOL(md_unregister_thread);
EXPORT_SYMBOL(md_update_sb);
EXPORT_SYMBOL(md_map);
EXPORT_SYMBOL(md_wakeup_thread);
EXPORT_SYMBOL(md_do_sync);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_md = {
	PROC_MD, 6, "mdstat",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations,
};
#endif

static void md_geninit (struct gendisk *gdisk)
{
  int i;
  
  for(i=0;i<MAX_MD_DEV;i++)
  {
    md_blocksizes[i] = 1024;
    md_maxreadahead[i] = MD_DEFAULT_DISK_READAHEAD;
    md_gendisk.part[i].start_sect=-1; /* avoid partition check */
    md_gendisk.part[i].nr_sects=0;
    md_dev[i].pers=NULL;
  }

  blksize_size[MD_MAJOR] = md_blocksizes;
  max_readahead[MD_MAJOR] = md_maxreadahead;

#ifdef CONFIG_PROC_FS
  proc_register(&proc_root, &proc_md);
#endif
}

int md_error (kdev_t mddev, kdev_t rdev)
{
    unsigned int minor = MINOR (mddev);
    int rc;

    if (MAJOR(mddev) != MD_MAJOR || minor > MAX_MD_DEV)
	panic ("md_error gets unknown device\n");
    if (!md_dev [minor].pers)
	panic ("md_error gets an error for an unknown device\n");
    if (md_dev [minor].pers->error_handler) {
	rc = md_dev [minor].pers->error_handler (md_dev+minor, rdev);
#if SUPPORT_RECONSTRUCTION
	md_wakeup_thread(md_sync_thread);
#endif /* SUPPORT_RECONSTRUCTION */
	return rc;
    }
    return 0;
}

int get_md_status (char *page)
{
  int sz=0, i, j, size;

  sz+=sprintf( page+sz, "Personalities : ");
  for (i=0; i<MAX_PERSONALITY; i++)
    if (pers[i])
      sz+=sprintf (page+sz, "[%d %s] ", i, pers[i]->name);

  page[sz-1]='\n';

  sz+=sprintf (page+sz, "read_ahead ");
  if (read_ahead[MD_MAJOR]==INT_MAX)
    sz+=sprintf (page+sz, "not set\n");
  else
    sz+=sprintf (page+sz, "%d sectors\n", read_ahead[MD_MAJOR]);
  
  for (i=0; i<MAX_MD_DEV; i++)
  {
    sz+=sprintf (page+sz, "md%d : %sactive", i, md_dev[i].pers ? "" : "in");

    if (md_dev[i].pers)
      sz+=sprintf (page+sz, " %s", md_dev[i].pers->name);

    size=0;
    for (j=0; j<md_dev[i].nb_dev; j++)
    {
      sz+=sprintf (page+sz, " %s",
		   partition_name(md_dev[i].devices[j].dev));
      size+=md_dev[i].devices[j].size;
    }

    if (md_dev[i].nb_dev) {
      if (md_dev[i].pers)
        sz+=sprintf (page+sz, " %d blocks", md_size[i]);
      else
        sz+=sprintf (page+sz, " %d blocks", size);
    }

    if (!md_dev[i].pers)
    {
      sz+=sprintf (page+sz, "\n");
      continue;
    }

    if (md_dev[i].pers->max_invalid_dev)
      sz+=sprintf (page+sz, " maxfault=%ld", MAX_FAULT(md_dev+i));

    sz+=md_dev[i].pers->status (page+sz, i, md_dev+i);
    sz+=sprintf (page+sz, "\n");
  }

  return (sz);
}

int register_md_personality (int p_num, struct md_personality *p)
{
  int i=(p_num >> PERSONALITY_SHIFT);

  if (i >= MAX_PERSONALITY)
    return -EINVAL;

  if (pers[i])
    return -EBUSY;
  
  pers[i]=p;
  printk ("%s personality registered\n", p->name);
  return 0;
}

int unregister_md_personality (int p_num)
{
  int i=(p_num >> PERSONALITY_SHIFT);

  if (i >= MAX_PERSONALITY)
    return -EINVAL;

  printk ("%s personality unregistered\n", pers[i]->name);
  pers[i]=NULL;
  return 0;
} 

static md_descriptor_t *get_spare(struct md_dev *mddev)
{
	int i;
	md_superblock_t *sb = mddev->sb;
	md_descriptor_t *descriptor;
	struct real_dev *realdev;
	
  	for (i = 0; i < mddev->nb_dev; i++) {
  		realdev = &mddev->devices[i];
		if (!realdev->sb)
			continue;
		descriptor = &sb->disks[realdev->sb->descriptor.number];
		if (descriptor->state & (1 << MD_FAULTY_DEVICE))
			continue;
		if (descriptor->state & (1 << MD_ACTIVE_DEVICE))
			continue;
		return descriptor;
	}
	return NULL;
}

/*
 * parallel resyncing thread. 
 *
 * FIXME: - make it abort with a dirty array on mdstop, now it just blocks
 *        - fix read error handing
 */

int md_do_sync(struct md_dev *mddev)
{
        struct buffer_head *bh;
	int max_blocks, blocksize, curr_bsize, percent=1, j;
	kdev_t read_disk = MKDEV(MD_MAJOR, mddev - md_dev);
	int major = MAJOR(read_disk), minor = MINOR(read_disk);
	unsigned long starttime;

	blocksize = blksize_size[major][minor];
	max_blocks = blk_size[major][minor] / (blocksize >> 10);

	printk("... resync log\n");
	printk(" ....   mddev->nb_dev: %d\n", mddev->nb_dev);
	printk(" ....   raid array: %s\n", kdevname(read_disk));
	printk(" ....   max_blocks: %d blocksize: %d\n", max_blocks, blocksize);
	printk("md: syncing RAID array %s\n", kdevname(read_disk));

	mddev->busy++;

	starttime=jiffies;
	for (j = 0; j < max_blocks; j++) {

		/*
		 * B careful. When some1 mounts a non-'blocksize' filesystem
		 * then we get the blocksize changed right under us. Go deal
		 * with it transparently, recalculate 'blocksize', 'j' and
		 * 'max_blocks':
		 */
		curr_bsize = blksize_size[major][minor];
		if (curr_bsize != blocksize) {
		diff_blocksize:
			if (curr_bsize > blocksize)
				/*
				 * this is safe, rounds downwards.
				 */
				j /= curr_bsize/blocksize;
			else
				j *= blocksize/curr_bsize;

			blocksize = curr_bsize;
			max_blocks = blk_size[major][minor] / (blocksize >> 10);
		}
        	if ((bh = breada (read_disk, j, blocksize, j * blocksize,
					max_blocks * blocksize)) != NULL) {
			mark_buffer_dirty(bh, 1);
			brelse(bh);
		} else {
			/*
			 * FIXME: Ugly, but set_blocksize() isnt safe ...
			 */
			curr_bsize = blksize_size[major][minor];
			if (curr_bsize != blocksize)
				goto diff_blocksize;

			/*
			 * It's a real read problem. FIXME, handle this
			 * a better way.
			 */
			printk ( KERN_ALERT
				 "read error, stopping reconstruction.\n");
			mddev->busy--;
			return 1;
		}

		/*
		 * Let's sleep some if we are faster than our speed limit:
		 */
		while (blocksize*j/(jiffies-starttime+1)*HZ/1024 > SPEED_LIMIT)
		{
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}

		/*
		 * FIXME: put this status bar thing into /proc
		 */
		if (!(j%(max_blocks/100))) {
			if (!(percent%10))
				printk (" %03d%% done.\n",percent);
			else
				printk (".");
			percent++;
		}
	}
	fsync_dev(read_disk);
	printk("md: %s: sync done.\n", kdevname(read_disk));
	mddev->busy--;
	return 0;
}

/*
 * This is a kernel thread which: syncs a spare disk with the active array
 *
 * the amount of foolproofing might seem to be a tad excessive, but an
 * early (not so error-safe) version of raid1syncd synced the first 0.5 gigs
 * of my root partition with the first 0.5 gigs of my /home partition ... so
 * i'm a bit nervous ;)
 */
void mdsyncd (void *data)
{
	int i;
	struct md_dev *mddev;
	md_superblock_t *sb;
	md_descriptor_t *spare;
	unsigned long flags;

	for (i = 0, mddev = md_dev; i < MAX_MD_DEV; i++, mddev++) {
		if ((sb = mddev->sb) == NULL)
			continue;
		if (sb->active_disks == sb->raid_disks)
			continue;
		if (!sb->spare_disks)
			continue;
		if ((spare = get_spare(mddev)) == NULL)
			continue;
		if (!mddev->pers->mark_spare)
			continue;
		if (mddev->pers->mark_spare(mddev, spare, SPARE_WRITE))
			continue;
		if (md_do_sync(mddev) || (spare->state & (1 << MD_FAULTY_DEVICE))) {
			mddev->pers->mark_spare(mddev, spare, SPARE_INACTIVE);
			continue;
		}
		save_flags(flags);
		cli();
		mddev->pers->mark_spare(mddev, spare, SPARE_ACTIVE);
		spare->state |= (1 << MD_SYNC_DEVICE);
		spare->state |= (1 << MD_ACTIVE_DEVICE);
		sb->spare_disks--;
		sb->active_disks++;
		mddev->sb_dirty = 1;
		md_update_sb(mddev - md_dev);
		restore_flags(flags);
	}
	
}

#ifdef CONFIG_MD_BOOT
struct {
	int set;
	int ints[100];
	char str[100];
} md_setup_args __initdata = {
	0,{0},{0}
};

/* called from init/main.c */
__initfunc(void md_setup(char *str,int *ints))
{
	int i;
	for(i=0;i<=ints[0];i++) {
		md_setup_args.ints[i] = ints[i];
		strcpy(md_setup_args.str, str);
/*      printk ("md: ints[%d]=%d.\n", i, ints[i]);*/
	}
	md_setup_args.set=1;
	return;
}

__initfunc(void do_md_setup(char *str,int *ints))
{
	int minor, pers, factor, fault;
	kdev_t dev;
	int i=1;

	if(ints[0] < 4) {
		printk ("md: Too few Arguments (%d).\n", ints[0]);
		return;
	}
   
	minor=ints[i++];
   
	if (minor >= MAX_MD_DEV) {
		printk ("md: Minor device number too high.\n");
		return;
	}

	pers = 0;
	
	switch(ints[i++]) {  /* Raidlevel  */
	case -1:
#ifdef CONFIG_MD_LINEAR
		pers = LINEAR;
		printk ("md: Setting up md%d as linear device.\n",minor);
#else 
	        printk ("md: Linear mode not configured." 
			"Recompile the kernel with linear mode enabled!\n");
#endif
		break;
	case 0:
		pers = STRIPED;
#ifdef CONFIG_MD_STRIPED
		printk ("md: Setting up md%d as a striped device.\n",minor);
#else 
	        printk ("md: Striped mode not configured." 
			"Recompile the kernel with striped mode enabled!\n");
#endif
		break;
/*      not supported yet
	case 1:
		pers = RAID1;
		printk ("md: Setting up md%d as a raid1 device.\n",minor);
		break;
	case 5:
		pers = RAID5;
		printk ("md: Setting up md%d as a raid5 device.\n",minor);
		break;
*/
	default:	   
		printk ("md: Unknown or not supported raid level %d.\n", ints[--i]);
		return;
	}

	if(pers) {

	  factor=ints[i++]; /* Chunksize  */
	  fault =ints[i++]; /* Faultlevel */
   
	  pers=pers | factor | (fault << FAULT_SHIFT);   
   
	  while( str && (dev = name_to_kdev_t(str))) {
	    do_md_add (minor, dev);
	    if((str = strchr (str, ',')) != NULL)
	      str++;
	  }

	  do_md_run (minor, pers);
	  printk ("md: Loading md%d.\n",minor);
	}
   
}
#endif

void linear_init (void);
void raid0_init (void);
void raid1_init (void);
void raid5_init (void);

__initfunc(int md_init (void))
{
  printk ("md driver %d.%d.%d MAX_MD_DEV=%d, MAX_REAL=%d\n",
    MD_MAJOR_VERSION, MD_MINOR_VERSION, MD_PATCHLEVEL_VERSION,
    MAX_MD_DEV, MAX_REAL);

  if (register_blkdev (MD_MAJOR, "md", &md_fops))
  {
    printk ("Unable to get major %d for md\n", MD_MAJOR);
    return (-1);
  }

  blk_dev[MD_MAJOR].request_fn=DEVICE_REQUEST;
  blk_dev[MD_MAJOR].current_request=NULL;
  read_ahead[MD_MAJOR]=INT_MAX;
  memset(md_dev, 0, MAX_MD_DEV * sizeof (struct md_dev));
  md_gendisk.next=gendisk_head;

  gendisk_head=&md_gendisk;

#if SUPPORT_RECONSTRUCTION
  if ((md_sync_thread = md_register_thread(mdsyncd, NULL)) == NULL)
    printk("md: bug: md_sync_thread == NULL\n");
#endif /* SUPPORT_RECONSTRUCTION */

#ifdef CONFIG_MD_LINEAR
  linear_init ();
#endif
#ifdef CONFIG_MD_STRIPED
  raid0_init ();
#endif
#ifdef CONFIG_MD_MIRRORING
  raid1_init ();
#endif
#ifdef CONFIG_MD_RAID5
  raid5_init ();
#endif
  return (0);
}

#ifdef CONFIG_MD_BOOT
__initfunc(void md_setup_drive(void))
{
	if(md_setup_args.set)
		do_md_setup(md_setup_args.str, md_setup_args.ints);
}
#endif
