/*
 * lowlevel.c
 *
 * PURPOSE
 *  Low Level Device Routines for the UDF filesystem
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999 Ben Fennema
 *
 * HISTORY
 *
 *  03/26/99 blf  Created.
 */

#include "udfdecl.h"

#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <asm/uaccess.h>
#include <scsi/scsi.h>

typedef struct scsi_device Scsi_Device;
typedef struct scsi_cmnd   Scsi_Cmnd;

#include <scsi/scsi_ioctl.h>

#include <linux/udf_fs.h>
#include "udf_sb.h"

unsigned int 
udf_get_last_session(kdev_t dev)
{
	struct cdrom_multisession ms_info;
	unsigned int vol_desc_start;
	struct inode inode_fake;
	extern struct file_operations * get_blkfops(unsigned int);
	int i;

	vol_desc_start=0;
	if (get_blkfops(MAJOR(dev))->ioctl!=NULL)
	{
		/* Whoops.  We must save the old FS, since otherwise
		 * we would destroy the kernels idea about FS on root
		 * mount in read_super... [chexum]
		 */
		mm_segment_t old_fs=get_fs();
		inode_fake.i_rdev=dev;
		ms_info.addr_format=CDROM_LBA;
		set_fs(KERNEL_DS);
		i=get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
						NULL,
						CDROMMULTISESSION,
						(unsigned long) &ms_info);
		set_fs(old_fs);

#define WE_OBEY_THE_WRITTEN_STANDARDS 1

		if (i == 0)
		{
			udf_debug("XA disk: %s, vol_desc_start=%d\n",
				(ms_info.xa_flag ? "yes" : "no"), ms_info.addr.lba);
#if WE_OBEY_THE_WRITTEN_STANDARDS
			if (ms_info.xa_flag) /* necessary for a valid ms_info.addr */
#endif
				vol_desc_start = ms_info.addr.lba;
		}
		else
		{
			udf_debug("CDROMMULTISESSION not supported: rc=%d\n", i);
		}
	}
	else
	{
		udf_debug("Device doesn't know how to ioctl?\n");
	}
	return vol_desc_start;
}

unsigned int
udf_get_last_block(kdev_t dev, int *flags)
{
	extern int *blksize_size[];
	struct inode inode_fake;
	extern struct file_operations * get_blkfops(unsigned int);
	int ret;
	unsigned long lblock;
	unsigned int hbsize = get_hardblocksize(dev);
	unsigned int secsize = 512;
	unsigned int mult = 0;
	unsigned int div = 0;

	if (!hbsize)
		hbsize = blksize_size[MAJOR(dev)][MINOR(dev)];

	if (secsize > hbsize)
		mult = secsize / hbsize;
	else if (hbsize > secsize)
		div = hbsize / secsize;

	if (get_blkfops(MAJOR(dev))->ioctl!=NULL)
	{
      /* Whoops.  We must save the old FS, since otherwise
       * we would destroy the kernels idea about FS on root
       * mount in read_super... [chexum]
       */
		mm_segment_t old_fs=get_fs();
		inode_fake.i_rdev=dev;
		set_fs(KERNEL_DS);

		lblock = 0;
		ret = get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
			NULL,
			BLKGETSIZE,
			(unsigned long) &lblock);

		if (!ret && lblock != 0x7FFFFFFF) /* Hard Disk */
		{
			if (mult)
				lblock *= mult;
			else if (div)
				lblock /= div;
		}
		else /* CDROM */
		{
			ret = get_blkfops(MAJOR(dev))->ioctl(&inode_fake,
				NULL,
				CDROM_LAST_WRITTEN,
				(unsigned long) &lblock);
		}

		set_fs(old_fs);
		if (!ret && lblock)
			return lblock - 1;
	}
	else
	{
		udf_debug("Device doesn't know how to ioctl?\n");
	}
	return 0;
}
