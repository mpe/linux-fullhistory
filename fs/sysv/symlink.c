/*
 *  linux/fs/sysv/symlink.c
 *
 *  minix/symlink.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/symlink.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/symlink.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent symlink handling code
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sysv_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>

static int sysv_readlink(struct dentry *, char *, int);
static struct dentry *sysv_follow_link(struct dentry *, struct dentry *, unsigned int);

/*
 * symlinks can't do much...
 */
struct inode_operations sysv_symlink_inode_operations = {
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
	sysv_readlink,		/* readlink */
	sysv_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static struct dentry *sysv_follow_link(struct dentry * dentry,
					struct dentry * base,
					unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head * bh;

	bh = sysv_file_bread(inode, 0, 0);
	if (!bh) {
		dput(base);
		return ERR_PTR(-EIO);
	}
	UPDATE_ATIME(inode);
	base = lookup_dentry(bh->b_data, base, follow);
	brelse(bh);
	return base;
}

static int sysv_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head * bh;
	char * bh_data;
	int i;
	char c;

	if (buflen > inode->i_sb->sv_block_size_1)
		buflen = inode->i_sb->sv_block_size_1;
	bh = sysv_file_bread(inode, 0, 0);
	if (!bh)
		return 0;
	bh_data = bh->b_data;
	i = 0;
	while (i<buflen && (c = bh_data[i])) {
		i++;
		put_user(c,buffer++);
	}
	brelse(bh);
	return i;
}
