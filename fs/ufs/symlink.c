/*
 *  linux/fs/ufs/symlink.c
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
#include <linux/ufs_fs.h>

static int ufs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	char *s = (char *)dentry->d_inode->u.ufs_i.i_u1.i_symlink;
	return vfs_readlink(dentry, buffer, buflen, s);
}

static struct dentry *ufs_follow_link(struct dentry *dentry, struct dentry *base, unsigned flags)
{
	char *s = (char *)dentry->d_inode->u.ufs_i.i_u1.i_symlink;
	return vfs_follow_link(dentry, base, flags, s);
}

struct inode_operations ufs_fast_symlink_inode_operations = {
	readlink:	ufs_readlink,
	follow_link:	ufs_follow_link,
};

struct inode_operations ufs_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	get_block:	ufs_getfrag_block,
	readpage:	block_read_full_page
};
