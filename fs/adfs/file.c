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

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the adfs filesystem.
 */
static struct file_operations adfs_file_operations = {
	read:		generic_file_read,
	mmap:		generic_file_mmap,
	fsync:		file_fsync,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	write:		generic_file_write,
#endif
};

struct inode_operations adfs_file_inode_operations = {
	&adfs_file_operations,	/* default file operations */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
	readpage:	generic_readpage,
	bmap:		adfs_bmap,
#endif
};
