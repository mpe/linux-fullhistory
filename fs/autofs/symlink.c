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

#include <linux/string.h>
#include <linux/sched.h>
#include "autofs_i.h"

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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
