/*
 * proc/fs/generic.c --- generic routines for the proc-fs
 *
 * This file contains generic proc-fs routines for handling
 * directories and files.
 * 
 * Copyright (C) 1991, 1992 Linus Torvalds.
 * Copyright (C) 1997 Theodore Ts'o
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <asm/bitops.h>

static long proc_file_read(struct inode * inode, struct file * file,
			   char * buf, unsigned long nbytes);
static long proc_file_write(struct inode * inode, struct file * file,
			    const char * buffer, unsigned long count);
static long long proc_file_lseek(struct inode * inode, struct file * file, 
				 long long offset, int orig);

static struct file_operations proc_file_operations = {
    proc_file_lseek,	/* lseek   */
    proc_file_read,	/* read	   */
    proc_file_write,	/* write   */
    NULL,		/* readdir */
    NULL,		/* poll    */
    NULL,		/* ioctl   */
    NULL,		/* mmap	   */
    NULL,		/* no special open code	   */
    NULL,		/* no special release code */
    NULL		/* can't fsync */
};

/*
 * proc files can do almost nothing..
 */
struct inode_operations proc_file_inode_operations = {
    &proc_file_operations,  /* default scsi directory file-ops */
    NULL,	    /* create	   */
    NULL,	    /* lookup	   */
    NULL,	    /* link	   */
    NULL,	    /* unlink	   */
    NULL,	    /* symlink	   */
    NULL,	    /* mkdir	   */
    NULL,	    /* rmdir	   */
    NULL,	    /* mknod	   */
    NULL,	    /* rename	   */
    NULL,	    /* readlink	   */
    NULL,	    /* follow_link */
    NULL,	    /* readpage	   */
    NULL,	    /* writepage   */
    NULL,	    /* bmap	   */
    NULL,	    /* truncate	   */
    NULL	    /* permission  */
};

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/* 4K page size but our output routines use some slack for overruns */
#define PROC_BLOCK_SIZE	(3*1024)

static long proc_file_read(struct inode * inode, struct file * file,
			   char * buf, unsigned long nbytes)
{
	char 	*page;
	int	retval=0;
	int	n;
	char	*start;
	struct proc_dir_entry * dp;

	if (nbytes < 0)
		return -EINVAL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while (nbytes > 0)
	{
		n = MIN(PROC_BLOCK_SIZE, nbytes);

		if (dp->get_info) {
			/*
			 * Handle backwards compatibility with the old net
			 * routines.
			 * 
			 * XXX What gives with the file->f_flags & O_ACCMODE
			 * test?  Seems stupid to me....
			 */
			n = dp->get_info(page, &start, file->f_pos, n,
				 (file->f_flags & O_ACCMODE) == O_RDWR);
		} else if (dp->read_proc) {
			n = dp->read_proc(page, &start, file->f_pos,
					  n, dp->data);
		} else
			break;
			
		if (n == 0)
			break;	/* End of file */
		if (n < 0) {
			if (retval == 0)
				retval = n;
			break;
		}
		
		n -= copy_to_user(buf, start, n);
		if (n == 0) {
			if (retval == 0)
				retval = -EFAULT;
			break;
		}
		
		file->f_pos += n;	/* Move down the file */
		nbytes -= n;
		buf += n;
		retval += n;
	}
	free_page((unsigned long) page);
	return retval;
}

static long
proc_file_write(struct inode * inode, struct file * file,
		const char * buffer, unsigned long count)
{
	struct proc_dir_entry * dp;
	char 	*page;
	
	if (count < 0)
		return -EINVAL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	if (!dp->write_proc)
		return -EIO;

	return dp->write_proc(file, buffer, count, dp->data);
}



static long long proc_file_lseek(struct inode * inode, struct file * file, 
				 long long offset, int orig)
{
    switch (orig) {
    case 0:
	file->f_pos = offset;
	return(file->f_pos);
    case 1:
	file->f_pos += offset;
	return(file->f_pos);
    case 2:
	return(-EINVAL);
    default:
	return(-EINVAL);
    }
}

struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
					 struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ent;

	ent = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
	if (!ent)
		return NULL;
	memset(ent, 0, sizeof(struct proc_dir_entry));

	if (mode == S_IFDIR)
		mode |= S_IRUGO | S_IXUGO;
	else if (mode == 0)
		mode = S_IFREG | S_IRUGO;
	
	ent->name = name;
	ent->namelen = strlen(ent->name);
	ent->mode = mode;
	if (S_ISDIR(mode)) 
		ent->nlink = 2;
	else
		ent->nlink = 1;

	if (parent)
		proc_register(parent, ent);
	
	return ent;
}

