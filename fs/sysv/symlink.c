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

#include <linux/sysv_fs.h>

/*
 * symlinks can't do much...
 */
struct inode_operations sysv_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	get_block:	sysv_get_block,
	readpage:	block_read_full_page
};
