/*
 *	fs/bfs/file.c
 *	BFS file operations.
 *	Copyright (C) 1999 Tigran Aivazian <tigran@ocston.org>
 */

#include <linux/fs.h>
#include <linux/bfs_fs.h>
#include "bfs_defs.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

static ssize_t bfs_file_write(struct file * f, const char * buf, size_t count, loff_t *ppos)
{
	return generic_file_write(f, buf, count, ppos, block_write_partial_page);
}

static struct file_operations bfs_file_operations = {
	llseek:			NULL,
	read:			generic_file_read,
	write:			bfs_file_write,
	readdir:		NULL,
	poll:			NULL,
	ioctl:			NULL,
	mmap:			generic_file_mmap,
	open:			NULL,
	flush:			NULL,
	release:		NULL,
	fsync:			NULL,
	fasync:			NULL,
	check_media_change:	NULL,
	revalidate:		NULL,
};

static int bfs_get_block(struct inode * inode, long block, 
	struct buffer_head * bh_result, int create)
{
	long phys = inode->iu_sblock + block;
	if (!create || phys <= inode->iu_eblock) {
		bh_result->b_dev = inode->i_dev;
		bh_result->b_blocknr = phys;
		bh_result->b_state |= (1UL << BH_Mapped);
		return 0;
	} 
	/* no support for file migration, working on it */
	return -EIO;
}

struct inode_operations bfs_file_inops = {
	default_file_ops:	&bfs_file_operations,
	create:			NULL,
	lookup:			NULL,
	link:			NULL,
	unlink:			NULL,
	symlink:		NULL,
	mkdir:			NULL,
	rmdir:			NULL,
	mknod:			NULL,
	rename:			NULL,
	readlink:		NULL,
	follow_link:		NULL,
	get_block:		bfs_get_block,	
	readpage:		block_read_full_page,
	writepage:		block_write_full_page,
	truncate:		NULL,
	permission:		NULL,
	revalidate:		NULL
};
