/*
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  minix symlink handling code
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>

static int minix_readlink(struct inode *, char *, int);

/*
 * symlinks can't do much...
 */
struct inode_operations minix_symlink_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	minix_readlink,		/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int minix_readlink(struct inode * inode, char * buffer, int buflen)
{
	struct buffer_head * bh;
	int i;
	char c;

	if (buflen > 1023)
		buflen = 1023;
	bh = minix_bread(inode, 0, 0);
	iput(inode);
	if (!bh)
		return 0;
	i = 0;
	while (i<buflen && (c = bh->b_data[i])) {
		i++;
		put_user(c,buffer++);
	}
	brelse(bh);
	return i;
}
