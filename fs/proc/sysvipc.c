/*
 *  linux/fs/proc/sysvipc.c
 *
 *  Copyright (c) 1999 Dragos Acostachioaie
 *
 *  This code is derived from linux/fs/proc/generic.c,
 *  which is Copyright (C) 1991, 1992 Linus Torvalds.
 *
 *  /proc/sysvipc directory handling functions
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>

#include <asm/uaccess.h>

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/* 4K page size but our output routines use some slack for overruns */
#define PROC_BLOCK_SIZE	(3*1024)

static ssize_t
proc_sysvipc_read(struct file * file, char * buf, size_t nbytes, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	char 	*page;
	ssize_t	retval=0;
	int	eof=0;
	ssize_t	n, count;
	char	*start;
	struct proc_dir_entry * dp;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while ((nbytes > 0) && !eof)
	{
		count = MIN(PROC_BLOCK_SIZE, nbytes);

		start = NULL;
		if (dp->get_info) {
			/*
			 * Handle backwards compatibility with the old net
			 * routines.
			 * 
			 * XXX What gives with the file->f_flags & O_ACCMODE
			 * test?  Seems stupid to me....
			 */
			n = dp->get_info(page, &start, *ppos, count,
				 (file->f_flags & O_ACCMODE) == O_RDWR);
			if (n < count)
				eof = 1;
		} else if (dp->read_proc) {
			n = dp->read_proc(page, &start, *ppos,
					  count, &eof, dp->data);
		} else
			break;
			
		if (!start) {
			/*
			 * For proc files that are less than 4k
			 */
			start = page + *ppos;
			n -= *ppos;
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
		
		/* This is a hack to allow mangling of file pos independent
 		 * of actual bytes read.  Simply place the data at page,
 		 * return the bytes, and set `start' to the desired offset
 		 * as an unsigned int. - Paul.Russell@rustcorp.com.au
		 */
 		n -= copy_to_user(buf, start < page ? page : start, n);
		if (n == 0) {
			if (retval == 0)
				retval = -EFAULT;
			break;
		}

		*ppos += start < page ? (long)start : n; /* Move down the file */
		nbytes -= n;
		buf += n;
		retval += n;
	}
	free_page((unsigned long) page);
	return retval;
}

static struct file_operations proc_sysvipc_operations = {
    NULL,		/* lseek   */
    proc_sysvipc_read,	/* read	   */
    NULL,		/* write   */
    NULL,		/* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_sysvipc_inode_operations = {
	&proc_sysvipc_operations,	/* default net file-ops */
	NULL,				/* create */
	NULL,				/* lookup */
	NULL,				/* link */
	NULL,				/* unlink */
	NULL,				/* symlink */
	NULL,				/* mkdir */
	NULL,				/* rmdir */
	NULL,				/* mknod */
	NULL,				/* rename */
	NULL,				/* readlink */
	NULL,				/* follow_link */
	NULL,				/* readpage */
	NULL,				/* writepage */
	NULL,				/* bmap */
	NULL,				/* truncate */
	NULL				/* permission */
};
