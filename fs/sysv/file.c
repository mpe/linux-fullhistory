/*
 *  linux/fs/sysv/file.c
 *
 *  minix/file.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/file.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/file.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent regular file handling primitives
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>

#define	NBUF	32

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/*
 * Write to a file (through the page cache).
 */
static ssize_t
sysv_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	return generic_file_write(file, buf, count,
				  ppos, block_write_partial_page);
}

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the coh filesystem.
 */
static struct file_operations sysv_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	sysv_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	sysv_sync_file,		/* fsync */
	NULL,			/* fasync */
};

struct inode_operations sysv_file_inode_operations = {
	&sysv_file_operations,	/* default file operations */
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
	sysv_get_block,		/* get_block */
	block_read_full_page,	/* readpage */
	block_write_full_page,	/* writepage */
	sysv_truncate,		/* truncate */
	NULL,   		/* permission */
	NULL			/* revalidate */
};
