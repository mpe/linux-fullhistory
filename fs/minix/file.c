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

static ssize_t minix_file_write(struct file *, const char *, size_t, loff_t *);

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
	minix_sync_file		/* fsync */
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
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	minix_bmap,		/* bmap */
	minix_truncate,		/* truncate */
	NULL			/* permission */
};

static ssize_t minix_file_write(struct file * filp, const char * buf,
				size_t count, loff_t *ppos)
{
	struct inode * inode = filp->f_dentry->d_inode;
	off_t pos;
	ssize_t written, c;
	struct buffer_head * bh;
	char * p;

	if (!inode) {
		printk("minix_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("minix_file_write: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = *ppos;
	written = 0;
	while (written < count) {
		bh = minix_getblk(inode,pos/BLOCK_SIZE,1);
		if (!bh) {
			if (!written)
				written = -ENOSPC;
			break;
		}
		c = BLOCK_SIZE - (pos % BLOCK_SIZE);
		if (c > count-written)
			c = count-written;
		if (c != BLOCK_SIZE && !buffer_uptodate(bh)) {
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh)) {
				brelse(bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		p = (pos % BLOCK_SIZE) + bh->b_data;
		c -= copy_from_user(p,buf,c);
		if (!c) {
			brelse(bh);
			if (!written)
				written = -EFAULT;
			break;
		}
		update_vm_cache(inode, pos, p, c);
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 0);
		brelse(bh);
		pos += c;
		written += c;
		buf += c;
	}
	if (pos > inode->i_size)
		inode->i_size = pos;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	*ppos = pos;
	mark_inode_dirty(inode);
	return written;
}
