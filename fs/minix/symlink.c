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

static int minix_readlink(struct dentry *, char *, int);
static struct dentry *minix_follow_link(struct dentry *, struct dentry *, unsigned int);

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
	minix_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct dentry * minix_follow_link(struct dentry * dentry,
					struct dentry * base,
					unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head * bh;

	bh = minix_bread(inode, 0, 0);
	if (!bh) {
		dput(base);
		return ERR_PTR(-EIO);
	}
	UPDATE_ATIME(inode);
	base = lookup_dentry(bh->b_data, base, follow);
	brelse(bh);
	return base;
}

static int minix_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct buffer_head * bh;
	int i;
	char c;

	if (buflen > 1023)
		buflen = 1023;
	bh = minix_bread(dentry->d_inode, 0, 0);
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
