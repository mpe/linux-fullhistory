/*
 *  linux/fs/adfs/file.c
 *
 * Copyright (C) 1997-1999 Russell King
 * from:
 *
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  adfs regular file handling primitives           
 */
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>

#include "adfs.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
/*
 * Write to a file (through the page cache).
 */
static ssize_t
adfs_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = generic_file_write(file, buf, count, ppos,
				    block_write_partial_page);

	if (retval > 0) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}

	return retval;
}
#endif

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the adfs filesystem.
 */
static struct file_operations adfs_file_operations = {
	NULL,			/* lseek		*/
	generic_file_read,	/* read			*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	adfs_file_write,	/* write		*/
#else
	NULL,
#endif
	NULL,			/* readdir		*/
	NULL,			/* poll			*/
	NULL,			/* ioctl		*/
	generic_file_mmap,	/* mmap			*/
	NULL,			/* open			*/
	NULL,			/* flush		*/
	NULL,			/* release		*/
	file_fsync,		/* fsync		*/
	NULL,			/* fasync		*/
};

struct inode_operations adfs_file_inode_operations = {
	&adfs_file_operations,	/* default file operations */
	NULL,			/* create		*/
	NULL,			/* lookup		*/
	NULL,			/* link			*/
	NULL,			/* unlink		*/
	NULL,			/* symlink		*/
	NULL,			/* mkdir		*/
	NULL,			/* rmdir		*/
	NULL,			/* mknod		*/
	NULL,			/* rename		*/
	NULL,			/* readlink		*/
	NULL,			/* follow_link		*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	adfs_get_block,		/* bmap			*/
	block_read_full_page,	/* readpage		*/
	block_write_full_page,	/* writepage		*/
#else
	generic_readpage,	/* readpage		*/
	NULL,			/* writepage		*/
	adfs_bmap,		/* bmap			*/
#endif
	NULL,			/* truncate		*/
	NULL,			/* permission		*/
	NULL,			/* revalidate		*/
};
