/*
 *  linux/fs/isofs/file.c
 *
 *  (C) 1992, 1993, 1994  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  isofs regular file handling primitives
 */

#include <linux/sched.h>
#include <linux/iso_fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/fs.h>

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the isofs filesystem.
 */
static struct file_operations isofs_file_operations = {
	read:		generic_file_read,
	mmap:		generic_file_mmap,
};

struct inode_operations isofs_file_inode_operations = {
	&isofs_file_operations,	/* default file operations */
};
