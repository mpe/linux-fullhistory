/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/symlink.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/modversions.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/auto_fs.h>

static int autofs_follow_link(struct inode *dir, struct inode *inode,
			      int flag, int mode, struct inode **res_inode)
{
	int error;
	char *link;

	*res_inode = NULL;
	if (!dir) {
		dir = current->fs->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput(dir);
		iput(inode);
		return -ELOOP;
	}
	link = ((struct autofs_symlink *)inode->u.generic_ip)->data;
	current->link_count++;
	error = open_namei(link,flag,mode,res_inode,dir);
	current->link_count--;
	iput(inode);
	return error;
}

static int autofs_readlink(struct inode *inode, char *buffer, int buflen)
{
	struct autofs_symlink *sl;
	int len;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	sl = (struct autofs_symlink *)inode->u.generic_ip;
	len = sl->len;
	if (len > buflen) len = buflen;
	copy_to_user(buffer,sl->data,len);
	iput(inode);
	return len;
}

struct inode_operations autofs_symlink_inode_operations = {
	NULL,			/* file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	autofs_readlink,	/* readlink */
	autofs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
