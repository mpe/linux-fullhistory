/*
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  minix symlink handling code
 *
 *  Code removed. 1999, AV ;-)
 */

#include <linux/fs.h>
#include <linux/minix_fs.h>

/*
 * symlinks can't do much...
 */
struct inode_operations minix_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	get_block:	minix_get_block,
	readpage:	block_read_full_page
};
