/*
 *  linux/fs/ext2/truncate.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/truncate.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 *  General cleanup and race fixes, wsh, 1998
 */

/*
 * Real random numbers for secure rm added 94/02/18
 * Idea from Pierre del Perugia <delperug@gla.ecoledoc.ibp.fr>
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/string.h>

#if 0

/*
 * Secure deletion currently doesn't work. It interacts very badly
 * with buffers shared with memory mappings, and for that reason
 * can't be done in the truncate() routines. It should instead be
 * done separately in "release()" before calling the truncate routines
 * that will release the actual file blocks.
 *
 *		Linus
 */
static int ext2_secrm_seed = 152;	/* Random generator base */

#define RANDOM_INT (ext2_secrm_seed = ext2_secrm_seed * 69069l +1)
#endif

/*
 * Macros to return the block number for the inode size and offset.
 * Currently we always hold the inode semaphore during truncate, so
 * there's no need to test for changes during the operation.
 */
#define DIRECT_BLOCK(inode) \
	((inode->i_size + inode->i_sb->s_blocksize - 1) / \
			  inode->i_sb->s_blocksize)
#define INDIRECT_BLOCK(inode,offset) ((int)DIRECT_BLOCK(inode) - offset)
#define DINDIRECT_BLOCK(inode,offset) \
	(INDIRECT_BLOCK(inode,offset) / addr_per_block)
#define TINDIRECT_BLOCK(inode,offset) \
	(INDIRECT_BLOCK(inode,offset) / (addr_per_block*addr_per_block))

/*
 * Truncate has the most races in the whole filesystem: coding it is
 * a pain in the a**. Especially as I don't do any locking...
 *
 * The code may look a bit weird, but that's just because I've tried to
 * handle things like file-size changes in a somewhat graceful manner.
 * Anyway, truncating a file at the same time somebody else writes to it
 * is likely to result in pretty weird behaviour...
 *
 * The new code handles normal truncates (size = 0) as well as the more
 * general case (size = XXX). I hope.
 *
 *
 * Truncate operations have been rewritten to avoid various races. The
 * previous code was allowing blocking operations to precede a call to
 * bforget(), possible allowing the buffer to be used again.
 *
 * We now ensure that b_count == 1 before calling bforget() and that the
 * parent buffer (if any) is unlocked before clearing the block pointer.
 * The operations are always performed in this order:
 *	(1) Make sure that the parent buffer is unlocked.
 *	(2) Use find_buffer() to find the block buffer without blocking,
 *	    and set 'retry' if the buffer is locked or b_count > 1.
 *	(3) Clear the block pointer in the parent (buffer or inode).
 *	(4) Update the inode block count and mark the inode dirty.
 *	(5) Forget the block buffer, if any. This call won't block, as
 *	    we know the buffer is unlocked from (2).
 *	(6) If the block pointer is in a (parent) buffer, mark the buffer
 *	    dirty. (Note that this can block on a loop device.)
 *	(7) Accumulate the blocks to free and/or update the block bitmap.
 *	    (This operation will frequently block.)
 *
 * The requirement that parent buffers be unlocked follows from the general
 * principle of not modifying a buffer that may be undergoing I/O. With the
 * the present kernels there's no problem with modifying a locked inode, as
 * the I_DIRTY bit is cleared before setting I_LOCK.
 *		-- WSH, 1998
 */

/*
 * Check whether any of the slots in an indirect block are
 * still in use, and if not free the block.
 */
static int check_block_empty(struct inode *inode, struct buffer_head *bh,
				u32 *p, struct buffer_head *ind_bh)
{
	int addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	u32 * ind = (u32 *) bh->b_data;
	int i, retry;

	/* Make sure both buffers are unlocked */
	do {
		retry = 0;
		if (buffer_locked(bh)) {
			__wait_on_buffer(bh);
			retry = 1;
		}
		if (ind_bh && buffer_locked(ind_bh)) {
			__wait_on_buffer(ind_bh);
			retry = 1;
		}
	} while (retry);

	for (i = 0; i < addr_per_block; i++)
		if (*(ind++))
			goto in_use;

	if (bh->b_count == 1) {
		int tmp;
		if (ind_bh)
			tmp = le32_to_cpu(*p);
		else
			tmp = *p;
		*p = 0;
		inode->i_blocks -= (inode->i_sb->s_blocksize / 512);
		mark_inode_dirty(inode);
		/*
		 * Forget the buffer, then mark the parent buffer dirty.
		 */
		bforget(bh);
		if (ind_bh)
			mark_buffer_dirty(ind_bh, 1);
		ext2_free_blocks (inode, tmp, 1);
		goto out;
	}
	retry = 1;

in_use:
	if (IS_SYNC(inode) && buffer_dirty(bh)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);

out:
	return retry;
}

static int trunc_direct (struct inode * inode)
{
	struct buffer_head * bh;
	int i, retry = 0;
	unsigned long block_to_free = 0, free_count = 0;
	int blocks = inode->i_sb->s_blocksize / 512;
	int direct_block = DIRECT_BLOCK(inode);

	for (i = direct_block ; i < EXT2_NDIR_BLOCKS ; i++) {
		u32 * p = inode->u.ext2_i.i_data + i;
		int tmp = *p;

		if (!tmp)
			continue;

		bh = find_buffer(inode->i_dev, tmp, inode->i_sb->s_blocksize);
		if (bh) {
			bh->b_count++;
			if(bh->b_count != 1 || buffer_locked(bh)) {
				brelse(bh);
				retry = 1;
				continue;
			}
		}

		*p = 0;
		inode->i_blocks -= blocks;
		mark_inode_dirty(inode);
		bforget(bh);

		/* accumulate blocks to free if they're contiguous */
		if (free_count == 0)
			goto free_this;
		else if (block_to_free == tmp - free_count)
			free_count++;
		else {
			ext2_free_blocks (inode, block_to_free, free_count);
		free_this:
			block_to_free = tmp;
			free_count = 1;
		}
	}
	if (free_count > 0)
		ext2_free_blocks (inode, block_to_free, free_count);
	return retry;
}

static int trunc_indirect (struct inode * inode, int offset, u32 * p,
			struct buffer_head *dind_bh)
{
	struct buffer_head * ind_bh;
	int i, tmp, retry = 0;
	unsigned long block_to_free = 0, free_count = 0;
	int indirect_block, addr_per_block, blocks;

	tmp = dind_bh ? le32_to_cpu(*p) : *p;
	if (!tmp)
		return 0;
	ind_bh = bread (inode->i_dev, tmp, inode->i_sb->s_blocksize);
	if (tmp != (dind_bh ? le32_to_cpu(*p) : *p)) {
		brelse (ind_bh);
		return 1;
	}
	/* A read failure? Report error and clear slot (should be rare). */ 
	if (!ind_bh) {
		ext2_error(inode->i_sb, "trunc_indirect",
			"Read failure, inode=%ld, block=%d",
			inode->i_ino, tmp);
		*p = 0;
		if (dind_bh)
			mark_buffer_dirty(dind_bh, 1);
		else
			mark_inode_dirty(inode);
		return 0;
	}

	blocks = inode->i_sb->s_blocksize / 512;
	addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	indirect_block = INDIRECT_BLOCK(inode, offset);
	if (indirect_block < 0)
		indirect_block = 0;
	for (i = indirect_block ; i < addr_per_block ; i++) {
		u32 * ind = i + (u32 *) ind_bh->b_data;
		struct buffer_head * bh;

		wait_on_buffer(ind_bh);
		tmp = le32_to_cpu(*ind);
		if (!tmp)
			continue;
		/*
		 * Use find_buffer so we don't block here.
		 */
		bh = find_buffer(inode->i_dev, tmp, inode->i_sb->s_blocksize);
		if (bh) {
			bh->b_count++;
			if (bh->b_count != 1 || buffer_locked(bh)) {
				brelse (bh);
				retry = 1;
				continue;
			}
		}

		*ind = 0;
		inode->i_blocks -= blocks;
		mark_inode_dirty(inode);
		bforget(bh);
		mark_buffer_dirty(ind_bh, 1);

		/* accumulate blocks to free if they're contiguous */
		if (free_count == 0)
			goto free_this;
		else if (block_to_free == tmp - free_count)
			free_count++;
		else {
			ext2_free_blocks (inode, block_to_free, free_count);
		free_this:
			block_to_free = tmp;
			free_count = 1;
		}
	}
	if (free_count > 0)
		ext2_free_blocks (inode, block_to_free, free_count);
	/*
	 * Check the block and dispose of the ind_bh buffer.
	 */
	retry |= check_block_empty(inode, ind_bh, p, dind_bh);

	return retry;
}

static int trunc_dindirect (struct inode * inode, int offset, u32 * p,
			struct buffer_head * tind_bh)
{
	struct buffer_head * dind_bh;
	int i, tmp, retry = 0;
	int dindirect_block, addr_per_block;

	tmp = tind_bh ? le32_to_cpu(*p) : *p;
	if (!tmp)
		return 0;
	dind_bh = bread (inode->i_dev, tmp, inode->i_sb->s_blocksize);
	if (tmp != (tind_bh ? le32_to_cpu(*p) : *p)) {
		brelse (dind_bh);
		return 1;
	}
	/* A read failure? Report error and clear slot (should be rare). */ 
	if (!dind_bh) {
		ext2_error(inode->i_sb, "trunc_dindirect",
			"Read failure, inode=%ld, block=%d",
			inode->i_ino, tmp);
		*p = 0;
		if (tind_bh)
			mark_buffer_dirty(tind_bh, 1);
		else
			mark_inode_dirty(inode);
		return 0;
	}

	addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	dindirect_block = DINDIRECT_BLOCK(inode, offset);
	if (dindirect_block < 0)
		dindirect_block = 0;
	for (i = dindirect_block ; i < addr_per_block ; i++) {
		u32 * dind = i + (u32 *) dind_bh->b_data;

		retry |= trunc_indirect(inode,
					offset + (i * addr_per_block),
					dind, dind_bh);
	}
	/*
	 * Check the block and dispose of the dind_bh buffer.
	 */
	retry |= check_block_empty(inode, dind_bh, p, tind_bh);

	return retry;
}

static int trunc_tindirect (struct inode * inode)
{
	u32 * p = inode->u.ext2_i.i_data + EXT2_TIND_BLOCK;
	struct buffer_head * tind_bh;
	int i, tmp, retry = 0;
	int tindirect_block, addr_per_block, offset;

	if (!(tmp = *p))
		return 0;
	tind_bh = bread (inode->i_dev, tmp, inode->i_sb->s_blocksize);
	if (tmp != *p) {
		brelse (tind_bh);
		return 1;
	}
	/* A read failure? Report error and clear slot (should be rare). */ 
	if (!tind_bh) {
		ext2_error(inode->i_sb, "trunc_tindirect",
			"Read failure, inode=%ld, block=%d",
			inode->i_ino, tmp);
		*p = 0;
		mark_inode_dirty(inode);
		return 0;
	}

	addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	offset = EXT2_NDIR_BLOCKS + addr_per_block + 
		(addr_per_block * addr_per_block);
	tindirect_block = TINDIRECT_BLOCK(inode, offset);
	if (tindirect_block < 0)
		tindirect_block = 0;
	for (i = tindirect_block ; i < addr_per_block ; i++) {
		u32 * tind = i + (u32 *) tind_bh->b_data;

		retry |= trunc_dindirect(inode,
				offset + (i * addr_per_block * addr_per_block),
				tind, tind_bh);
	}
	/*
	 * Check the block and dispose of the tind_bh buffer.
	 */
	retry |= check_block_empty(inode, tind_bh, p, NULL);

	return retry;
}
		
void ext2_truncate (struct inode * inode)
{
	int err, offset, retry;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	    S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;
	ext2_discard_prealloc(inode);
	while (1) {
		retry = trunc_direct(inode);
		retry |= trunc_indirect (inode, 
				EXT2_IND_BLOCK,
				(u32 *) &inode->u.ext2_i.i_data[EXT2_IND_BLOCK],
				NULL);
		retry |= trunc_dindirect (inode,
				EXT2_IND_BLOCK+EXT2_ADDR_PER_BLOCK(inode->i_sb),
				(u32 *)&inode->u.ext2_i.i_data[EXT2_DIND_BLOCK],
				NULL);
		retry |= trunc_tindirect (inode);
		if (!retry)
			break;
		if (IS_SYNC(inode) && (inode->i_state & I_DIRTY))
			ext2_sync_inode (inode);
		current->counter = 0;
		schedule ();
	}
	/*
	 * If the file is not being truncated to a block boundary, the
	 * contents of the partial block following the end of the file
	 * must be zeroed in case it ever becomes accessible again due
	 * to subsequent file growth.
	 */
	offset = inode->i_size & (inode->i_sb->s_blocksize - 1);
	if (offset) {
		struct buffer_head * bh;
		bh = ext2_bread (inode,
				 inode->i_size >> EXT2_BLOCK_SIZE_BITS(inode->i_sb),
				 0, &err);
		if (bh) {
			memset (bh->b_data + offset, 0,
				inode->i_sb->s_blocksize - offset);
			mark_buffer_dirty (bh, 0);
			brelse (bh);
		}
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
}
