/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#define	NBUF	32

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <linux/fs.h>
#include <linux/minix_fs.h>

static int minix_writepage(struct file *file, struct page *page)
{
	return block_write_full_page(file, page, minix_getblk_block);
}

static long minix_write_one_page(struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char *buf)
{
	return block_write_partial_page(file, page, offset, bytes, buf, minix_getblk_block);
}

/*
 * Write to a file (through the page cache).
 */
static ssize_t
minix_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	return generic_file_write(file, buf, count, ppos, minix_write_one_page);
}

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the minix filesystem.
 */
static struct file_operations minix_file_operations = {
	NULL,			/* lseek - default */
	generic_file_read,	/* read */
	minix_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* flush */
	NULL,			/* release */
	minix_sync_file,	/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations minix_file_inode_operations = {
	&minix_file_operations,	/* default file operations */
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
	minix_bmap,		/* bmap */
	block_read_full_page,	/* readpage */
	minix_writepage,	/* writepage */
	block_flushpage,	/* flushpage */
	minix_truncate,		/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* revalidate */
};
