/*
 *	fs/bfs/file.c
 *	BFS file operations.
 *	Copyright (C) 1999 Tigran Aivazian <tigran@ocston.org>
 */

#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/bfs_fs.h>
#include <linux/smp_lock.h>
#include "bfs_defs.h"

#undef DEBUG

#ifdef DEBUG
#define dprintf(x...)	printf(x)
#else
#define dprintf(x...)
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
};

static int bfs_move_block(unsigned long from, unsigned long to, kdev_t dev)
{
	struct buffer_head *bh, *new = NULL;

	bh = bread(dev, from, BFS_BSIZE);
	if (!bh)
		return -EIO;
	new = getblk(dev, to, BFS_BSIZE);
	memcpy(new->b_data, bh->b_data, bh->b_size);
	mark_buffer_dirty(new, 1);
	bforget(bh);
	brelse(new);
	return 0;
}

static int bfs_move_blocks(kdev_t dev, unsigned long start, unsigned long end, 
				unsigned long where)
{
	unsigned long i;

	dprintf("%08lx-%08lx->%08lx\n", start, end, where);
	for (i = start; i <= end; i++)
		if(bfs_move_block(i, where + i, dev)) {
			dprintf("failed to move block %08lx -> %08lx\n", i, where + i);
			return -EIO;
		}
	return 0;
}

static int bfs_get_block(struct inode * inode, long block, 
	struct buffer_head * bh_result, int create)
{
	long phys, next_free_block;
	int err;
	struct super_block *s = inode->i_sb;

	if (block < 0 || block > s->su_blocks)
		return -EIO;

	phys = inode->iu_sblock + block;
	if (!create) {
		if (phys <= inode->iu_eblock) {
			dprintf("c=%d, b=%08lx, phys=%08lx (granted)\n", create, block, phys);
			bh_result->b_dev = inode->i_dev;
			bh_result->b_blocknr = phys;
			bh_result->b_state |= (1UL << BH_Mapped);
		}
		return 0;
	}

	/* if the file is not empty and the requested block is within the range
	   of blocks allocated for this file, we can grant it */
	if (inode->i_size && phys <= inode->iu_eblock) {
		dprintf("c=%d, b=%08lx, phys=%08lx (interim block granted)\n", create, block, phys);
		bh_result->b_dev = inode->i_dev;
		bh_result->b_blocknr = phys;
		bh_result->b_state |= (1UL << BH_Mapped);
		return 0;
	}

	/* the rest has to be protected against itself */
	lock_kernel();

	/* if the last data block for this file is the last allocated block, we can
	   extend the file trivially, without moving it anywhere */
	if (inode->iu_eblock == s->su_lf_eblk) {
		dprintf("c=%d, b=%08lx, phys=%08lx (simple extension)\n", create, block, phys);
		bh_result->b_dev = inode->i_dev;
		bh_result->b_blocknr = phys;
		bh_result->b_state |= (1UL << BH_Mapped);
		s->su_lf_eblk = inode->iu_eblock = inode->iu_sblock + block;
		mark_inode_dirty(inode);
		mark_buffer_dirty(s->su_sbh, 1);
		err = 0;
		goto out;
	}

	/* Ok, we have to move this entire file to the next free block */
	next_free_block = s->su_lf_eblk + 1;
	if (inode->iu_sblock) { /* if data starts on block 0 then there is no data */
		err = bfs_move_blocks(inode->i_dev, inode->iu_sblock, inode->iu_eblock, next_free_block);
		if (err) {
			dprintf("failed to move ino=%08lx -> possible fs corruption\n", inode->i_ino);
			goto out;
		}
	} else
		err = 0;

	inode->iu_sblock = next_free_block;
	s->su_lf_eblk = inode->iu_eblock = next_free_block + block;
	mark_inode_dirty(inode);
	mark_buffer_dirty(s->su_sbh, 1);
	bh_result->b_dev = inode->i_dev;
	bh_result->b_blocknr = inode->iu_sblock + block;
	bh_result->b_state |= (1UL << BH_Mapped);
out:
	unlock_kernel();
	return err;
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
