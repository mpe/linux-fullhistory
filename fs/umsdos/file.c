/*
 *  linux/fs/umsdos/file.c
 *
 *  Written 1993 by Jacques Gelinas
 *      inspired from linux/fs/msdos/file.c Werner Almesberger
 *
 *  Extended MS-DOS regular file handling primitives
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>

#include <asm/uaccess.h>
#include <asm/system.h>

/*
 * Read a file into user space memory
 */
static ssize_t UMSDOS_file_read (
					struct file *filp,
					char *buf,
					size_t count,
					loff_t * ppos
)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;

	int ret = fat_file_read (filp, buf, count, ppos);

	/* We have to set the access time because msdos don't care */
	if (!IS_RDONLY (inode)) {
		inode->i_atime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
	return ret;
}


/*
 * Write a file from user space memory
 */
static ssize_t UMSDOS_file_write (
					 struct file *filp,
					 const char *buf,
					 size_t count,
					 loff_t * ppos)
{
	return fat_file_write (filp, buf, count, ppos);
}


/*
 * Truncate a file to 0 length.
 */
static void UMSDOS_truncate (struct inode *inode)
{
	Printk (("UMSDOS_truncate\n"));
	if (!IS_RDONLY (inode)) {
		fat_truncate (inode);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
}

/* Function for normal file system (512 bytes hardware sector size) */
struct file_operations umsdos_file_operations =
{
	NULL,			/* lseek - default */
	UMSDOS_file_read,	/* read */
	UMSDOS_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	file_fsync		/* fsync */
};

struct inode_operations umsdos_file_inode_operations =
{
	&umsdos_file_operations,	/* default file operations */
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
	NULL,			/* follow_link */
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	fat_bmap,		/* bmap */
	UMSDOS_truncate,	/* truncate */
	NULL,			/* permission */
	fat_smap		/* smap */
};

/* For other with larger and unaligned file system */
struct file_operations umsdos_file_operations_no_bmap =
{
	NULL,			/* lseek - default */
	UMSDOS_file_read,	/* read */
	UMSDOS_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	fat_mmap,		/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	file_fsync		/* fsync */
};

struct inode_operations umsdos_file_inode_operations_no_bmap =
{
	&umsdos_file_operations_no_bmap,	/* default file operations */
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
	NULL,			/* follow link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	UMSDOS_truncate,	/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};

/* For other with larger and unaligned file system with readpage */
struct file_operations umsdos_file_operations_readpage =
{
	NULL,			/* lseek - default */
	UMSDOS_file_read,	/* read */
	UMSDOS_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	file_fsync		/* fsync */
};

struct inode_operations umsdos_file_inode_operations_readpage =
{
	&umsdos_file_operations_readpage,	/* default file operations */
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
	NULL,			/* follow link */
	fat_readpage,		/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	UMSDOS_truncate,	/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};
