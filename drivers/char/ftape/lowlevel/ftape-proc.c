/*
 *      Copyright (C) 1997 Claus-Justus Heine

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-proc.c,v $
 * $Revision: 1.11 $
 * $Date: 1997/10/24 14:47:37 $
 *
 *      This file contains the procfs interface for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/config.h>

#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)

/*  adding proc entries from inside a module is REALLY complicated 
 *  for pre-2.1.28 kernels. I don't want to care about it.
 */

#include <linux/proc_fs.h>

#include <linux/ftape.h>
#if LINUX_VERSION_CODE <= KERNEL_VER(1,2,13) /* bail out */
#error \
Please disable CONFIG_FT_PROC_FS in "MCONFIG" or upgrade to a newer kernel!
#endif
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,16)
#include <linux/init.h>
#else
#define __initdata
#define __initfunc(__arg) __arg
#endif
#include <linux/qic117.h>


#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-proc.h"
#include "../lowlevel/ftape-tracing.h"

static int ftape_read_proc(char *page, char **start, off_t off,
			   int count, int *eof, void *data);

#if LINUX_VERSION_CODE < KERNEL_VER(2,1,28)

#include <asm/segment.h> /* for memcpy_tofs() */

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long ftape_proc_read(struct inode* inode, struct file* file,
			    char* buf, unsigned long count);
#else
static int ftape_proc_read(struct inode* inode, struct file* file,
			   char* buf, int count);
#endif

#define FT_PROC_REGISTER(parent, child) proc_register_dynamic(parent, child)

/*
 *	Structures for interfacing with the /proc filesystem.
 *	Router creates its own directory /proc/net/router with the folowing
 *	entries:
 *	config		device configuration
 *	status		global device statistics
 *	<device>	entry for each WAN device
 */

/*
 *	Generic /proc/net/ftape/<file> file and inode operations
 */


static struct file_operations ftape_proc_fops =
{
	NULL,			/* lseek   */
	ftape_proc_read,	/* read	   */
	NULL,			/* write   */
	NULL,			/* readdir */
	NULL,			/* select  */
	NULL,			/* ioctl   */
	NULL,			/* mmap	   */
	NULL,			/* no special open code	   */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL,			/* can't fsync */
};

static struct inode_operations ftape_proc_inode_operations =
{
	&ftape_proc_fops,
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
};

/*
 * Proc filesystem directory entries.
 */

static int ftape_get_info(char *page, char **start, off_t off,
			  int count, int dummy)
{
	int dummy_eof;

	return ftape_read_proc(page, start, off, count, &dummy_eof, NULL);
}

static struct proc_dir_entry proc_ftape = {
	0,                            /* low_ino    */
	sizeof("ftape")-1,            /* namelen    */
	"ftape",                      /* name       */
	S_IFREG | S_IRUGO,            /* mode       */
	1,                            /* nlink      */
	0,                            /* uid        */
	0,                            /* gid        */
	0,                            /* size       */
	&ftape_proc_inode_operations, /* ops        */
	ftape_get_info,               /* get_info   */
	NULL,                         /* fill_inode */
	NULL,                         /* next       */
	NULL,                         /* parent     */
	NULL,                         /* subdir     */
	NULL                          /* data       */
};

/*  Read ftape proc directory entry.
 */

#define PROC_BLOCK_SIZE	PAGE_SIZE

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long ftape_proc_read(struct inode * inode, struct file * file,
			    char * buf, unsigned long nbytes)
#else
static int ftape_proc_read(struct inode * inode, struct file * file,
			   char * buf, int nbytes)
#endif
{
	char 	*page;
	int	retval=0;
	int	eof=0;
	int	n, count;
	char	*start;
	struct proc_dir_entry * dp;

	if (nbytes < 0)
		return -EINVAL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while ((nbytes > 0) && !eof)
	{
		count = PROC_BLOCK_SIZE <= nbytes ? PROC_BLOCK_SIZE : nbytes;

		start = NULL;
		if (dp->get_info) {
			/*
			 * Handle backwards compatibility with the old net
			 * routines.
			 * 
			 * XXX What gives with the file->f_flags & O_ACCMODE
			 * test?  Seems stupid to me....
			 */
			n = dp->get_info(page, &start, file->f_pos, count,
				 (file->f_flags & O_ACCMODE) == O_RDWR);
			if (n < count)
				eof = 1;
		} else
			break;
			
		if (!start) {
			/*
			 * For proc files that are less than 4k
			 */
			start = page + file->f_pos;
			n -= file->f_pos;
			if (n <= 0)
				break;
			if (n > count)
				n = count;
		}
		if (n == 0)
			break;	/* End of file */
		if (n < 0) {
			if (retval == 0)
				retval = n;
			break;
		}
#if LINUX_VERSION_CODE > KERNEL_VER(2,1,3)
		copy_to_user(buf, start, n);
#else
		memcpy_tofs(buf, start, n);
#endif
		file->f_pos += n;	/* Move down the file */
		nbytes -= n;
		buf += n;
		retval += n;
	}
	free_page((unsigned long) page);
	return retval;
}

#else /* LINUX_VERSION_CODE < KERNEL_VER(2,1,28) */

#define FT_PROC_REGISTER(parent, child) proc_register(parent, child)

/*
 * Proc filesystem directory entries.
 */

static struct proc_dir_entry proc_ftape = {
	0,                   /* low_ino    */
	sizeof("ftape")-1,   /* namelen    */
	"ftape",             /* name       */
	S_IFREG | S_IRUGO,   /* mode       */
	1,                   /* nlink      */
	0,                   /* uid        */
	0,                   /* gid        */
	0,                   /* size       */
	NULL,                /* ops        */
	NULL,                /* get_info   */
	NULL,                /* fill_inode */
	NULL,                /* next       */
	NULL,                /* parent     */
	NULL,                /* subdir     */
	NULL,                /* data       */
	ftape_read_proc,     /* read_proc  */
	NULL                 /* write_proc */
};

#endif

static size_t get_driver_info(char *buf)
{
	const char *debug_level[] = { "bugs"  ,
				      "errors",
				      "warnings",
				      "informational",
				      "noisy",
				      "program flow",
				      "fdc and dma",
				      "data flow",
				      "anything" };

	return sprintf(buf,
		       "version       : %s\n"
		       "used data rate: %d kbit/sec\n"
		       "dma memory    : %d kb\n"
		       "debug messages: %s\n",
		       FTAPE_VERSION,
		       ft_data_rate,
		       FT_BUFF_SIZE * ft_nr_buffers >> 10,
		       debug_level[TRACE_LEVEL]);
}

static size_t get_tapedrive_info(char *buf)
{ 
	return sprintf(buf,
		       "vendor id : 0x%04x\n"
		       "drive name: %s\n"
		       "wind speed: %d ips\n"
		       "wakeup    : %s\n"
		       "max. rate : %d kbit/sec\n",
		       ft_drive_type.vendor_id,
		       ft_drive_type.name,
		       ft_drive_type.speed,
		       ((ft_drive_type.wake_up == no_wake_up)
			? "No wakeup needed" :
			((ft_drive_type.wake_up == wake_up_colorado)
			 ? "Colorado" :
			 ((ft_drive_type.wake_up == wake_up_mountain)
			  ? "Mountain" :
			  ((ft_drive_type.wake_up == wake_up_insight)
			   ? "Motor on" :
			   "Unknown")))),
		       ft_drive_max_rate);
}

static size_t get_cartridge_info(char *buf)
{
	if (ftape_init_drive_needed) {
		return sprintf(buf, "uninitialized\n");
	}
	if (ft_no_tape) {
		return sprintf(buf, "no cartridge inserted\n");
	}
	return sprintf(buf,
		       "segments  : %5d\n"
		       "tracks    : %5d\n"
		       "length    : %5dft\n"
		       "formatted : %3s\n"
		       "writable  : %3s\n"
		       "QIC spec. : QIC-%s\n"
		       "fmt-code  : %1d\n",
		       ft_segments_per_track,
		       ft_tracks_per_tape,
		       ftape_tape_len,
		       (ft_formatted == 1) ? "yes" : "no",
		       (ft_write_protected == 1) ? "no" : "yes",
		       ((ft_qic_std == QIC_TAPE_QIC40) ? "40" :
			((ft_qic_std == QIC_TAPE_QIC80) ? "80" :
			 ((ft_qic_std == QIC_TAPE_QIC3010) ? "3010" :
			  ((ft_qic_std == QIC_TAPE_QIC3020) ? "3020" :
			   "???")))),
		       ft_format_code);
}

static size_t get_controller_info(char *buf)
{
	const char  *fdc_name[] = { "no fdc",
				    "i8272",
				    "i82077",
				    "i82077AA",
				    "Colorado FC-10 or FC-20",
				    "i82078",
				    "i82078_1" };

	return sprintf(buf,
		       "FDC type  : %s\n"
		       "FDC base  : 0x%03x\n"
		       "FDC irq   : %d\n"
		       "FDC dma   : %d\n"
		       "FDC thr.  : %d\n"
		       "max. rate : %d kbit/sec\n",
		       ft_mach2 ? "Mountain MACH-2" : fdc_name[fdc.type],
		       fdc.sra, fdc.irq, fdc.dma,
		       ft_fdc_threshold, ft_fdc_max_rate);
}

static size_t get_history_info(char *buf)
{
        size_t len;

	len  = sprintf(buf,
		       "\nFDC isr statistics\n"
		       " id_am_errors     : %3d\n"
		       " id_crc_errors    : %3d\n"
		       " data_am_errors   : %3d\n"
		       " data_crc_errors  : %3d\n"
		       " overrun_errors   : %3d\n"
		       " no_data_errors   : %3d\n"
		       " retries          : %3d\n",
		       ft_history.id_am_errors,   ft_history.id_crc_errors,
		       ft_history.data_am_errors, ft_history.data_crc_errors,
		       ft_history.overrun_errors, ft_history.no_data_errors,
		       ft_history.retries);
	len += sprintf(buf + len,
		       "\nECC statistics\n"
		       " crc_errors       : %3d\n"
		       " crc_failures     : %3d\n"
		       " ecc_failures     : %3d\n"
		       " sectors corrected: %3d\n",
		       ft_history.crc_errors,   ft_history.crc_failures,
		       ft_history.ecc_failures, ft_history.corrected);
	len += sprintf(buf + len,
		       "\ntape quality statistics\n"
		       " media defects    : %3d\n",
		       ft_history.defects);
	len += sprintf(buf + len,
		       "\ntape motion statistics\n"
		       " repositions      : %3d\n",
		       ft_history.rewinds);
	return len;
}

int ftape_read_proc(char *page, char **start, off_t off,
		    int count, int *eof, void *data)
{
	char *ptr = page;
	size_t len;
	
	ptr += sprintf(ptr, "Kernel Driver\n\n");
	ptr += get_driver_info(ptr);
	ptr += sprintf(ptr, "\nTape Drive\n\n");
	ptr += get_tapedrive_info(ptr);
	ptr += sprintf(ptr, "\nFDC Controller\n\n");
	ptr += get_controller_info(ptr);
	ptr += sprintf(ptr, "\nTape Cartridge\n\n");
	ptr += get_cartridge_info(ptr);
	ptr += sprintf(ptr, "\nHistory Record\n\n");
	ptr += get_history_info(ptr);

	len = strlen(page);
	*start = 0;
	if (off+count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	}
	return len;
}

__initfunc(int ftape_proc_init(void))
{
	return FT_PROC_REGISTER(&proc_root, &proc_ftape);
}

#ifdef MODULE
void ftape_proc_destroy(void)
{
	proc_unregister(&proc_root, proc_ftape.low_ino);
}
#endif

#endif /* defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS) */
