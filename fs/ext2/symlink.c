/*
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
#include <linux/ext2_fs.h>

static int ext2_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	char *s = (char *)dentry->d_inode->u.ext2_i.i_data;
	return vfs_readlink(dentry, buffer, buflen, s);
}

static struct dentry *ext2_follow_link(struct dentry *dentry, struct dentry *base, unsigned flags)
{
	char *s = (char *)dentry->d_inode->u.ext2_i.i_data;
	return vfs_follow_link(dentry, base, flags, s);
}

struct inode_operations ext2_fast_symlink_inode_operations = {
	readlink:	ext2_readlink,
	follow_link:	ext2_follow_link,
};

struct inode_operations ext2_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	get_block:	ext2_get_block,
	readpage:	block_read_full_page,
};
