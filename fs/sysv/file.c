/*
 *  linux/fs/sysv/file.c
 *
 *  minix/file.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/file.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/file.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent regular file handling primitives
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/pagemap.h>

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the coh filesystem.
 */
static struct file_operations sysv_file_operations = {
	read:		generic_file_read,
	write:		generic_file_write,
	mmap:		generic_file_mmap,
	fsync:		sysv_sync_file,
};

struct inode_operations sysv_file_inode_operations = {
	&sysv_file_operations,
	truncate:	sysv_truncate,
};
