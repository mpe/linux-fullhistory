/*
 *  linux/fs/affs/file.c
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs regular file handling primitives
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/locks.h>

#include <linux/dirent.h>

#define	NBUF	16

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <linux/fs.h>
#include <linux/affs_fs.h>

#include "amigaffs.h"

int affs_file_read(struct inode *, struct file *, char *, int);

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the affs filesystem.
 */
struct file_operations affs_file_operations = {
	NULL,			/* lseek - default */
	affs_file_read,		/* read */
	NULL,			/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	generic_file_mmap,	/* mmap */
	NULL,			/* no special open is needed */
	NULL,			/* release */
	NULL			/* can't fsync */
};

struct inode_operations affs_file_inode_operations = {
	&affs_file_operations,	/* default file operations */
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
	NULL /* affs_bmap */,		/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int affs_smap(struct inode *inode, int block)
{
	struct buffer_head *bh;
	int key;
	void *fh_data;

/* FIXME */
#define KEY_SLOTS_PER_BLOCK 72

#ifdef DEBUG
	printk ("affs_smap: ino=%d block=%d\n", inode->i_ino, block);
#endif

	if (block < 0) {
		printk("affs_smap: block < 0");
		return 0;
	}

	key = inode->i_ino;
	for (;;) {
		bh = affs_pread (inode, key, &fh_data);
		if (!bh)
			return 0;
		if (block < KEY_SLOTS_PER_BLOCK)
			break;
		block -= KEY_SLOTS_PER_BLOCK;
		key = affs_get_extension (AFFS_I2BSIZE (inode), fh_data);
#ifdef DEBUG
		printk ("affs_smap: reading extension block %d\n", key);
#endif
		brelse (bh);
	}
	key = affs_get_key_entry (AFFS_I2BSIZE (inode), fh_data,
				  (KEY_SLOTS_PER_BLOCK - 1) - block);
	brelse (bh);

#ifdef DEBUG
	printk ("affs_smap: key=%d\n", key);
#endif
	return key;
}

/*
 * affs_file_read() is also needed by the directory read-routine,
 * so it's not static. NOTE! reading directories directly is a bad idea,
 * but has to be supported for now for compatability reasons with older
 * versions.
 */
int affs_file_read(struct inode * inode, struct file * filp,
		   char * buf, int count)
{
	char *start;
	int left, offset, size, sector;
	struct buffer_head *bh;
	void *data;

	if (!inode) {
		printk("affs_file_read: inode = NULL\n");
		return -EINVAL;
	}
	if (!(S_ISREG(inode->i_mode))) {
#ifdef DEBUG
		printk("affs_file_read: mode = %07o\n",inode->i_mode);
#endif
		return -EINVAL;
	}
	if (filp->f_pos >= inode->i_size || count <= 0)
		return 0;

	start = buf;
	for (;;) {
		left = MIN (inode->i_size - filp->f_pos,
			    count - (buf - start));
		if (!left)
			break;
		sector = affs_smap (inode, filp->f_pos >> AFFS_BLOCK_BITS);
		if (!sector)
			break;
		offset = filp->f_pos & (AFFS_BLOCK_SIZE - 1);
		bh = affs_pread (inode, sector, &data);
		if (!bh)
			break;
		size = MIN (AFFS_BLOCK_SIZE - offset, left);
		filp->f_pos += size;
		memcpy_tofs (buf, data + offset, size);
		buf += size;
		brelse (bh);
	}
	if (start == buf)
		return -EIO;
	return buf - start;

#if 0
	if (filp->f_pos == 0 && count > 0) {
		put_fs_byte ('X', buf++);
		filp->f_pos++;
		return 1;
	}
	else
		return 0;
#endif
}
