/*
 *  linux/fs/adfs/file.c
 *
 * Copyright (C) 1997 Russell King
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

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the adfs filesystem.
 */
static struct file_operations adfs_file_operations = {
	NULL,			/* lseek - default	*/
	generic_file_read,	/* read			*/
	NULL,			/* write		*/
	NULL,			/* readdir - bad	*/
	NULL,			/* select - default	*/
	NULL,			/* ioctl		*/
	generic_file_mmap,	/* mmap			*/
	NULL,			/* open - not special	*/
	NULL,			/* flush		*/
	NULL,			/* release		*/
	file_fsync,		/* fsync		*/
	NULL,			/* fasync		*/
	NULL,			/* check_media_change	*/
	NULL			/* revalidate		*/
};

struct inode_operations adfs_file_inode_operations = {
	&adfs_file_operations,	/* default file operations	*/
	NULL,			/* create			*/
	NULL,			/* lookup			*/
	NULL,			/* link				*/
	NULL,			/* unlink			*/
	NULL,			/* symlink			*/
	NULL,			/* mkdir			*/
	NULL,			/* rmdir			*/
	NULL,			/* mknod			*/
	NULL,			/* rename			*/
	NULL,			/* readlink			*/
	NULL,			/* follow_link			*/
	generic_readpage,	/* readpage			*/
	NULL,			/* writepage			*/
	adfs_bmap,		/* bmap				*/
	NULL,			/* truncate			*/
	NULL,			/* permission			*/
	NULL			/* smap				*/
};
