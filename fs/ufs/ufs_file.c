/*
 *  linux/fs/ufs/ufs_file.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_file.c,v 1.8 1997/06/05 01:29:09 davem Exp $
 *
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>

static struct file_operations ufs_file_operations = {
	NULL,			/* lseek */
	generic_file_read,	/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* poll */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
};

struct inode_operations ufs_file_inode_operations = {
	&ufs_file_operations,	/* default directory file operations */
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
	ufs_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};

