/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include <linux/fs.h>
#include <linux/minix_fs.h>

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the minix filesystem.
 */
struct file_operations minix_file_operations = {
	read:		generic_file_read,
	write:		generic_file_write,
	mmap:		generic_file_mmap,
	fsync:		minix_sync_file,
};

struct inode_operations minix_file_inode_operations = {
	truncate:	minix_truncate,
};
