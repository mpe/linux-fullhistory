/*
 *  linux/fs/isofs/file.c
 *
 *  (C) 1992, 1993, 1994  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  isofs regular file handling primitives
 */

#include <linux/sched.h>
#include <linux/iso_fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/fs.h>
#include <linux/iso_fs.h>

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the isofs filesystem.
 */
static struct file_operations isofs_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	NULL,			/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	NULL			/* fsync */
};

struct inode_operations isofs_file_inode_operations = {
	&isofs_file_operations,	/* default file operations */
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
	isofs_bmap,		/* bmap */
	NULL,	       		/* truncate */
	NULL			/* permission */
};
