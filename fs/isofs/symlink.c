/*
 *  linux/fs/isofs/symlink.c
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  isofs symlink handling code.  This is only used with the Rock Ridge
 *  extensions to iso9660
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/iso_fs.h>
#include <linux/stat.h>
#include <linux/malloc.h>

/*
 * symlinks can't do much...
 */
struct inode_operations isofs_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	readpage:	rock_ridge_symlink_readpage
};
