/*
 *  linux/fs/ufs/file.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
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
 *  ext2 fs regular file handling primitives
 */

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define	NBUF	32

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/*
 * Make sure the offset never goes beyond the 32-bit mark..
 */
static long long ufs_file_lseek(
	struct file *file,
	long long offset,
	int origin )
{
	long long retval;
	struct inode *inode = file->f_dentry->d_inode;

	switch (origin) {
		case 2:
			offset += inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	/* make sure the offset fits in 32 bits */
	if (((unsigned long long) offset >> 32) == 0) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_reada = 0;
			file->f_version = ++event;
		}
		retval = offset;
	}
	return retval;
}

static inline void remove_suid(struct inode *inode)
{
	unsigned int mode;

	/* set S_IGID if S_IXGRP is set, and always set S_ISUID */
	mode = (inode->i_mode & S_IXGRP)*(S_ISGID/S_IXGRP) | S_ISUID;

	/* was any of the uid bits set? */
	mode &= inode->i_mode;
	if (mode && !suser()) {
		inode->i_mode &= ~mode;
		mark_inode_dirty(inode);
	}
}

/*
 * Write to a file (through the page cache).
 */
static ssize_t
ufs_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = generic_file_write(file, buf, count,
				    ppos, block_write_partial_page);
	if (retval > 0) {
		struct inode *inode = file->f_dentry->d_inode;
		remove_suid(inode);
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
	return retval;
}

/*
 * Called when an inode is released. Note that this is different
 * from ufs_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ufs_release_file (struct inode * inode, struct file * filp)
{
	return 0;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ufs filesystem.
 */
static struct file_operations ufs_file_operations = {
	ufs_file_lseek,	/* lseek */
	generic_file_read,	/* read */
	ufs_file_write, 	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL, 			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	ufs_release_file,	/* release */
	NULL, 			/* fsync */
	NULL,			/* fasync */
};

struct inode_operations ufs_file_inode_operations = {
	&ufs_file_operations,/* default file operations */
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
	ufs_getfrag_block,	/* get_block */
	block_read_full_page,	/* readpage */
	block_write_full_page,	/* writepage */
	ufs_truncate,		/* truncate */
	NULL, 			/* permission */
	NULL			/* revalidate */
};
