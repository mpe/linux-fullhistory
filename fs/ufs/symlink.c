/*
 *  linux/fs/ufs/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@emai.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/symlink.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 symlink handling code
 */

#include <linux/fs.h>

static int ufs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	char *s = (char *)dentry->d_inode->u.ufs_i.i_u1.i_symlink;
	return vfs_readlink(dentry, buffer, buflen, s);
}

static struct dentry *ufs_follow_link(struct dentry *dentry, struct dentry *base, struct vfsmount **mnt, unsigned flags)
{
	char *s = (char *)dentry->d_inode->u.ufs_i.i_u1.i_symlink;
	return vfs_follow_link(dentry, base, mnt, flags, s);
}

struct inode_operations ufs_fast_symlink_inode_operations = {
	readlink:	ufs_readlink,
	follow_link:	ufs_follow_link,
};
