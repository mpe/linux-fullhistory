/*
 *  linux/fs/ext2/inode.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Goal-directed block allocation by Stephen Tweedie
 * 	(sct@dcs.ed.ac.uk), 1993, 1998
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>

static int ext2_update_inode(struct inode * inode, int do_sync);

/*
 * Called at each iput()
 */
void ext2_put_inode (struct inode * inode)
{
	ext2_discard_prealloc (inode);
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext2_delete_inode (struct inode * inode)
{
	if (inode->i_ino == EXT2_ACL_IDX_INO ||
	    inode->i_ino == EXT2_ACL_DATA_INO)
		return;
	inode->u.ext2_i.i_dtime	= CURRENT_TIME;
	mark_inode_dirty(inode);
	ext2_update_inode(inode, IS_SYNC(inode));
	inode->i_size = 0;
	if (inode->i_blocks)
		ext2_truncate (inode);
	ext2_free_inode (inode);
}

#define inode_bmap(inode, nr) ((inode)->u.ext2_i.i_data[(nr)])

static inline int block_bmap (struct buffer_head * bh, int nr)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = le32_to_cpu(((u32 *) bh->b_data)[nr]);
	brelse (bh);
	return tmp;
}

/* 
 * ext2_discard_prealloc and ext2_alloc_block are atomic wrt. the
 * superblock in the same manner as are ext2_free_blocks and
 * ext2_new_block.  We just wait on the super rather than locking it
 * here, since ext2_new_block will do the necessary locking and we
 * can't block until then.
 */
void ext2_discard_prealloc (struct inode * inode)
{
#ifdef EXT2_PREALLOCATE
	unsigned short total;

	if (inode->u.ext2_i.i_prealloc_count) {
		total = inode->u.ext2_i.i_prealloc_count;
		inode->u.ext2_i.i_prealloc_count = 0;
		ext2_free_blocks (inode, inode->u.ext2_i.i_prealloc_block, total);
	}
#endif
}

static int ext2_alloc_block (struct inode * inode, unsigned long goal, int * err)
{
#ifdef EXT2FS_DEBUG
	static unsigned long alloc_hits = 0, alloc_attempts = 0;
#endif
	unsigned long result;
	struct buffer_head * bh;

	wait_on_super (inode->i_sb);

#ifdef EXT2_PREALLOCATE
	if (inode->u.ext2_i.i_prealloc_count &&
	    (goal == inode->u.ext2_i.i_prealloc_block ||
	     goal + 1 == inode->u.ext2_i.i_prealloc_block))
	{		
		result = inode->u.ext2_i.i_prealloc_block++;
		inode->u.ext2_i.i_prealloc_count--;
		ext2_debug ("preallocation hit (%lu/%lu).\n",
			    ++alloc_hits, ++alloc_attempts);

		/* It doesn't matter if we block in getblk() since
		   we have already atomically allocated the block, and
		   are only clearing it now. */
		if (!(bh = getblk (inode->i_sb->s_dev, result,
				   inode->i_sb->s_blocksize))) {
			ext2_error (inode->i_sb, "ext2_alloc_block",
				    "cannot get block %lu", result);
			return 0;
		}
		memset(bh->b_data, 0, inode->i_sb->s_blocksize);
		mark_buffer_uptodate(bh, 1);
		mark_buffer_dirty(bh, 1);
		brelse (bh);
	} else {
		ext2_discard_prealloc (inode);
		ext2_debug ("preallocation miss (%lu/%lu).\n",
			    alloc_hits, ++alloc_attempts);
		if (S_ISREG(inode->i_mode))
			result = ext2_new_block (inode, goal, 
				 &inode->u.ext2_i.i_prealloc_count,
				 &inode->u.ext2_i.i_prealloc_block, err);
		else
			result = ext2_new_block (inode, goal, 0, 0, err);
	}
#else
	result = ext2_new_block (inode, goal, 0, 0, err);
#endif

	return result;
}


int ext2_bmap (struct inode * inode, int block)
{
	int i;
	int addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int addr_per_block_bits = EXT2_ADDR_PER_BLOCK_BITS(inode->i_sb);

	if (block < 0) {
		ext2_warning (inode->i_sb, "ext2_bmap", "block < 0");
		return 0;
	}
	if (block >= EXT2_NDIR_BLOCKS + addr_per_block +
		(1 << (addr_per_block_bits * 2)) +
		((1 << (addr_per_block_bits * 2)) << addr_per_block_bits)) {
		ext2_warning (inode->i_sb, "ext2_bmap", "block > big");
		return 0;
	}
	if (block < EXT2_NDIR_BLOCKS)
		return inode_bmap (inode, block);
	block -= EXT2_NDIR_BLOCKS;
	if (block < addr_per_block) {
		i = inode_bmap (inode, EXT2_IND_BLOCK);
		if (!i)
			return 0;
		return block_bmap (bread (inode->i_dev, i,
					  inode->i_sb->s_blocksize), block);
	}
	block -= addr_per_block;
	if (block < (1 << (addr_per_block_bits * 2))) {
		i = inode_bmap (inode, EXT2_DIND_BLOCK);
		if (!i)
			return 0;
		i = block_bmap (bread (inode->i_dev, i,
				       inode->i_sb->s_blocksize),
				block >> addr_per_block_bits);
		if (!i)
			return 0;
		return block_bmap (bread (inode->i_dev, i,
					  inode->i_sb->s_blocksize),
				   block & (addr_per_block - 1));
	}
	block -= (1 << (addr_per_block_bits * 2));
	i = inode_bmap (inode, EXT2_TIND_BLOCK);
	if (!i)
		return 0;
	i = block_bmap (bread (inode->i_dev, i, inode->i_sb->s_blocksize),
			block >> (addr_per_block_bits * 2));
	if (!i)
		return 0;
	i = block_bmap (bread (inode->i_dev, i, inode->i_sb->s_blocksize),
			(block >> addr_per_block_bits) & (addr_per_block - 1));
	if (!i)
		return 0;
	return block_bmap (bread (inode->i_dev, i, inode->i_sb->s_blocksize),
			   block & (addr_per_block - 1));
}

static struct buffer_head * inode_getblk (struct inode * inode, int nr,
					  int create, int new_block, int * err)
{
	u32 * p;
	int tmp, goal = 0;
	struct buffer_head * result;
	int blocks = inode->i_sb->s_blocksize / 512;

	p = inode->u.ext2_i.i_data + nr;
repeat:
	tmp = *p;
	if (tmp) {
		struct buffer_head * result = getblk (inode->i_dev, tmp, inode->i_sb->s_blocksize);
		if (tmp == *p)
			return result;
		brelse (result);
		goto repeat;
	}
	*err = -EFBIG;
	if (!create)
		goto dont_create;

	/* Check file limits.. */
	{
		unsigned long limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
		if (limit < RLIM_INFINITY) {
			limit >>= EXT2_BLOCK_SIZE_BITS(inode->i_sb);
			if (new_block >= limit) {
				send_sig(SIGXFSZ, current, 0);
dont_create:
				*err = -EFBIG;
				return NULL;
			}
		}
	}

	if (inode->u.ext2_i.i_next_alloc_block == new_block)
		goal = inode->u.ext2_i.i_next_alloc_goal;

	ext2_debug ("hint = %d,", goal);

	if (!goal) {
		for (tmp = nr - 1; tmp >= 0; tmp--) {
			if (inode->u.ext2_i.i_data[tmp]) {
				goal = inode->u.ext2_i.i_data[tmp];
				break;
			}
		}
		if (!goal)
			goal = (inode->u.ext2_i.i_block_group * 
				EXT2_BLOCKS_PER_GROUP(inode->i_sb)) +
			       le32_to_cpu(inode->i_sb->u.ext2_sb.s_es->s_first_data_block);
	}

	ext2_debug ("goal = %d.\n", goal);

	tmp = ext2_alloc_block (inode, goal, err);
	if (!tmp)
		return NULL;
	result = getblk (inode->i_dev, tmp, inode->i_sb->s_blocksize);
	if (*p) {
		ext2_free_blocks (inode, tmp, 1);
		brelse (result);
		goto repeat;
	}
	*p = tmp;
	inode->u.ext2_i.i_next_alloc_block = new_block;
	inode->u.ext2_i.i_next_alloc_goal = tmp;
	inode->i_ctime = CURRENT_TIME;
	inode->i_blocks += blocks;
	if (IS_SYNC(inode) || inode->u.ext2_i.i_osync)
		ext2_sync_inode (inode);
	else
		mark_inode_dirty(inode);
	return result;
}

static struct buffer_head * block_getblk (struct inode * inode,
					  struct buffer_head * bh, int nr,
					  int create, int blocksize, 
					  int new_block, int * err)
{
	int tmp, goal = 0;
	u32 * p;
	struct buffer_head * result;
	int blocks = inode->i_sb->s_blocksize / 512;

	if (!bh)
		return NULL;
	if (!buffer_uptodate(bh)) {
		ll_rw_block (READ, 1, &bh);
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh)) {
			brelse (bh);
			return NULL;
		}
	}
	p = (u32 *) bh->b_data + nr;
repeat:
	tmp = le32_to_cpu(*p);
	if (tmp) {
		result = getblk (bh->b_dev, tmp, blocksize);
		if (tmp == le32_to_cpu(*p)) {
			brelse (bh);
			return result;
		}
		brelse (result);
		goto repeat;
	}
	if (!create || new_block >= 
	    (current->rlim[RLIMIT_FSIZE].rlim_cur >> 
	     EXT2_BLOCK_SIZE_BITS(inode->i_sb))) {
		brelse (bh);
		*err = -EFBIG;
		return NULL;
	}
	if (inode->u.ext2_i.i_next_alloc_block == new_block)
		goal = inode->u.ext2_i.i_next_alloc_goal;
	if (!goal) {
		for (tmp = nr - 1; tmp >= 0; tmp--) {
			if (le32_to_cpu(((u32 *) bh->b_data)[tmp])) {
				goal = le32_to_cpu(((u32 *)bh->b_data)[tmp]);
				break;
			}
		}
		if (!goal)
			goal = bh->b_blocknr;
	}
	tmp = ext2_alloc_block (inode, goal, err);
	if (!tmp) {
		brelse (bh);
		return NULL;
	}
	result = getblk (bh->b_dev, tmp, blocksize);
	if (le32_to_cpu(*p)) {
		ext2_free_blocks (inode, tmp, 1);
		brelse (result);
		goto repeat;
	}
	*p = le32_to_cpu(tmp);
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(inode) || inode->u.ext2_i.i_osync) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	inode->i_ctime = CURRENT_TIME;
	inode->i_blocks += blocks;
	mark_inode_dirty(inode);
	inode->u.ext2_i.i_next_alloc_block = new_block;
	inode->u.ext2_i.i_next_alloc_goal = tmp;
	brelse (bh);
	return result;
}

struct buffer_head * ext2_getblk (struct inode * inode, long block,
				  int create, int * err)
{
	struct buffer_head * bh;
	unsigned long b;
	unsigned long addr_per_block = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int addr_per_block_bits = EXT2_ADDR_PER_BLOCK_BITS(inode->i_sb);

	*err = -EIO;
	if (block < 0) {
		ext2_warning (inode->i_sb, "ext2_getblk", "block < 0");
		return NULL;
	}
	if (block > EXT2_NDIR_BLOCKS + addr_per_block +
		(1 << (addr_per_block_bits * 2)) +
		((1 << (addr_per_block_bits * 2)) << addr_per_block_bits)) {
		ext2_warning (inode->i_sb, "ext2_getblk", "block > big");
		return NULL;
	}
	/*
	 * If this is a sequential block allocation, set the next_alloc_block
	 * to this block now so that all the indblock and data block
	 * allocations use the same goal zone
	 */

	ext2_debug ("block %lu, next %lu, goal %lu.\n", block, 
		    inode->u.ext2_i.i_next_alloc_block,
		    inode->u.ext2_i.i_next_alloc_goal);

	if (block == inode->u.ext2_i.i_next_alloc_block + 1) {
		inode->u.ext2_i.i_next_alloc_block++;
		inode->u.ext2_i.i_next_alloc_goal++;
	}

	*err = -ENOSPC;
	b = block;
	if (block < EXT2_NDIR_BLOCKS)
		return inode_getblk (inode, block, create, b, err);
	block -= EXT2_NDIR_BLOCKS;
	if (block < addr_per_block) {
		bh = inode_getblk (inode, EXT2_IND_BLOCK, create, b, err);
		return block_getblk (inode, bh, block, create,
				     inode->i_sb->s_blocksize, b, err);
	}
	block -= addr_per_block;
	if (block < (1 << (addr_per_block_bits * 2))) {
		bh = inode_getblk (inode, EXT2_DIND_BLOCK, create, b, err);
		bh = block_getblk (inode, bh, block >> addr_per_block_bits,
				   create, inode->i_sb->s_blocksize, b, err);
		return block_getblk (inode, bh, block & (addr_per_block - 1),
				     create, inode->i_sb->s_blocksize, b, err);
	}
	block -= (1 << (addr_per_block_bits * 2));
	bh = inode_getblk (inode, EXT2_TIND_BLOCK, create, b, err);
	bh = block_getblk (inode, bh, block >> (addr_per_block_bits * 2),
			   create, inode->i_sb->s_blocksize, b, err);
	bh = block_getblk (inode, bh, (block >> addr_per_block_bits) & (addr_per_block - 1),
			   create, inode->i_sb->s_blocksize, b, err);
	return block_getblk (inode, bh, block & (addr_per_block - 1), create,
			     inode->i_sb->s_blocksize, b, err);
}

struct buffer_head * ext2_bread (struct inode * inode, int block, 
				 int create, int *err)
{
	struct buffer_head * bh;
	int prev_blocks;
	
	prev_blocks = inode->i_blocks;
	
	bh = ext2_getblk (inode, block, create, err);
	if (!bh)
		return bh;
	
	/*
	 * If the inode has grown, and this is a directory, then perform
	 * preallocation of a few more blocks to try to keep directory
	 * fragmentation down.
	 */
	if (create && 
	    S_ISDIR(inode->i_mode) && 
	    inode->i_blocks > prev_blocks &&
	    EXT2_HAS_COMPAT_FEATURE(inode->i_sb,
				    EXT2_FEATURE_COMPAT_DIR_PREALLOC)) {
		int i;
		struct buffer_head *tmp_bh;
		
		for (i = 1;
		     i < EXT2_SB(inode->i_sb)->s_es->s_prealloc_dir_blocks;
		     i++) {
			/* 
			 * ext2_getblk will zero out the contents of the
			 * directory for us
			 */
			tmp_bh = ext2_getblk(inode, block+i, create, err);
			if (!tmp_bh) {
				brelse (bh);
				return 0;
			}
			brelse (tmp_bh);
		}
	}
	
	if (buffer_uptodate(bh))
		return bh;
	ll_rw_block (READ, 1, &bh);
	wait_on_buffer (bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse (bh);
	*err = -EIO;
	return NULL;
}

void ext2_read_inode (struct inode * inode)
{
	struct buffer_head * bh;
	struct ext2_inode * raw_inode;
	unsigned long block_group;
	unsigned long group_desc;
	unsigned long desc;
	unsigned long block;
	unsigned long offset;
	struct ext2_group_desc * gdp;

	if ((inode->i_ino != EXT2_ROOT_INO && inode->i_ino != EXT2_ACL_IDX_INO &&
	     inode->i_ino != EXT2_ACL_DATA_INO &&
	     inode->i_ino < EXT2_FIRST_INO(inode->i_sb)) ||
	    inode->i_ino > le32_to_cpu(inode->i_sb->u.ext2_sb.s_es->s_inodes_count)) {
		ext2_error (inode->i_sb, "ext2_read_inode",
			    "bad inode number: %lu", inode->i_ino);
		goto bad_inode;
	}
	block_group = (inode->i_ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
	if (block_group >= inode->i_sb->u.ext2_sb.s_groups_count) {
		ext2_error (inode->i_sb, "ext2_read_inode",
			    "group >= groups count");
		goto bad_inode;
	}
	group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(inode->i_sb);
	desc = block_group & (EXT2_DESC_PER_BLOCK(inode->i_sb) - 1);
	bh = inode->i_sb->u.ext2_sb.s_group_desc[group_desc];
	if (!bh) {
		ext2_error (inode->i_sb, "ext2_read_inode",
			    "Descriptor not loaded");
		goto bad_inode;
	}

	gdp = (struct ext2_group_desc *) bh->b_data;
	/*
	 * Figure out the offset within the block group inode table
	 */
	offset = ((inode->i_ino - 1) % EXT2_INODES_PER_GROUP(inode->i_sb)) *
		EXT2_INODE_SIZE(inode->i_sb);
	block = le32_to_cpu(gdp[desc].bg_inode_table) +
		(offset >> EXT2_BLOCK_SIZE_BITS(inode->i_sb));
	if (!(bh = bread (inode->i_dev, block, inode->i_sb->s_blocksize))) {
		ext2_error (inode->i_sb, "ext2_read_inode",
			    "unable to read inode block - "
			    "inode=%lu, block=%lu", inode->i_ino, block);
		goto bad_inode;
	}
	offset &= (EXT2_BLOCK_SIZE(inode->i_sb) - 1);
	raw_inode = (struct ext2_inode *) (bh->b_data + offset);

	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	inode->i_uid = le16_to_cpu(raw_inode->i_uid);
	inode->i_gid = le16_to_cpu(raw_inode->i_gid);
	inode->i_nlink = le16_to_cpu(raw_inode->i_links_count);
	inode->i_size = le32_to_cpu(raw_inode->i_size);
	inode->i_atime = le32_to_cpu(raw_inode->i_atime);
	inode->i_ctime = le32_to_cpu(raw_inode->i_ctime);
	inode->i_mtime = le32_to_cpu(raw_inode->i_mtime);
	inode->u.ext2_i.i_dtime = le32_to_cpu(raw_inode->i_dtime);
	inode->i_blksize = PAGE_SIZE;	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	inode->i_version = ++event;
	inode->u.ext2_i.i_new_inode = 0;
	inode->u.ext2_i.i_flags = le32_to_cpu(raw_inode->i_flags);
	inode->u.ext2_i.i_faddr = le32_to_cpu(raw_inode->i_faddr);
	inode->u.ext2_i.i_frag_no = raw_inode->i_frag;
	inode->u.ext2_i.i_frag_size = raw_inode->i_fsize;
	inode->u.ext2_i.i_osync = 0;
	inode->u.ext2_i.i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	if (S_ISDIR(inode->i_mode))
		inode->u.ext2_i.i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);
	else {
		inode->u.ext2_i.i_dir_acl = 0;
		inode->u.ext2_i.i_high_size =
			le32_to_cpu(raw_inode->i_size_high);
#if BITS_PER_LONG < 64
		if (raw_inode->i_size_high)
			inode->i_size = (__u32)-1;
#else
		inode->i_size |= ((__u64)le32_to_cpu(raw_inode->i_size_high))
			<< 32;
#endif
	}
	inode->u.ext2_i.i_version = le32_to_cpu(raw_inode->i_version);
	inode->u.ext2_i.i_block_group = block_group;
	inode->u.ext2_i.i_next_alloc_block = 0;
	inode->u.ext2_i.i_next_alloc_goal = 0;
	if (inode->u.ext2_i.i_prealloc_count)
		ext2_error (inode->i_sb, "ext2_read_inode",
			    "New inode has non-zero prealloc count!");
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		inode->i_rdev = to_kdev_t(le32_to_cpu(raw_inode->i_block[0]));
	else if (S_ISLNK(inode->i_mode) && !inode->i_blocks)
		for (block = 0; block < EXT2_N_BLOCKS; block++)
			inode->u.ext2_i.i_data[block] = raw_inode->i_block[block];
	else for (block = 0; block < EXT2_N_BLOCKS; block++)
		inode->u.ext2_i.i_data[block] = le32_to_cpu(raw_inode->i_block[block]);
	brelse (bh);
	inode->i_op = NULL;
	if (inode->i_ino == EXT2_ACL_IDX_INO ||
	    inode->i_ino == EXT2_ACL_DATA_INO)
		/* Nothing to do */ ;
	else if (S_ISREG(inode->i_mode))
		inode->i_op = &ext2_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &ext2_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &ext2_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
	inode->i_attr_flags = 0;
	if (inode->u.ext2_i.i_flags & EXT2_SYNC_FL) {
		inode->i_attr_flags |= ATTR_FLAG_SYNCRONOUS;
		inode->i_flags |= MS_SYNCHRONOUS;
	}
	if (inode->u.ext2_i.i_flags & EXT2_APPEND_FL) {
		inode->i_attr_flags |= ATTR_FLAG_APPEND;
		inode->i_flags |= S_APPEND;
	}
	if (inode->u.ext2_i.i_flags & EXT2_IMMUTABLE_FL) {
		inode->i_attr_flags |= ATTR_FLAG_IMMUTABLE;
		inode->i_flags |= S_IMMUTABLE;
	}
	if (inode->u.ext2_i.i_flags & EXT2_NOATIME_FL) {
		inode->i_attr_flags |= ATTR_FLAG_NOATIME;
		inode->i_flags |= MS_NOATIME;
	}
	return;
	
bad_inode:
	make_bad_inode(inode);
	return;
}

static int ext2_update_inode(struct inode * inode, int do_sync)
{
	struct buffer_head * bh;
	struct ext2_inode * raw_inode;
	unsigned long block_group;
	unsigned long group_desc;
	unsigned long desc;
	unsigned long block;
	unsigned long offset;
	int err = 0;
	struct ext2_group_desc * gdp;

	if ((inode->i_ino != EXT2_ROOT_INO &&
	     inode->i_ino < EXT2_FIRST_INO(inode->i_sb)) ||
	    inode->i_ino > le32_to_cpu(inode->i_sb->u.ext2_sb.s_es->s_inodes_count)) {
		ext2_error (inode->i_sb, "ext2_write_inode",
			    "bad inode number: %lu", inode->i_ino);
		return -EIO;
	}
	block_group = (inode->i_ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
	if (block_group >= inode->i_sb->u.ext2_sb.s_groups_count) {
		ext2_error (inode->i_sb, "ext2_write_inode",
			    "group >= groups count");
		return -EIO;
	}
	group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(inode->i_sb);
	desc = block_group & (EXT2_DESC_PER_BLOCK(inode->i_sb) - 1);
	bh = inode->i_sb->u.ext2_sb.s_group_desc[group_desc];
	if (!bh) {
		ext2_error (inode->i_sb, "ext2_write_inode",
			    "Descriptor not loaded");
		return -EIO;
	}
	gdp = (struct ext2_group_desc *) bh->b_data;
	/*
	 * Figure out the offset within the block group inode table
	 */
	offset = ((inode->i_ino - 1) % EXT2_INODES_PER_GROUP(inode->i_sb)) *
		EXT2_INODE_SIZE(inode->i_sb);
	block = le32_to_cpu(gdp[desc].bg_inode_table) +
		(offset >> EXT2_BLOCK_SIZE_BITS(inode->i_sb));
	if (!(bh = bread (inode->i_dev, block, inode->i_sb->s_blocksize))) {
		ext2_error (inode->i_sb, "ext2_write_inode",
			    "unable to read inode block - "
			    "inode=%lu, block=%lu", inode->i_ino, block);
		return -EIO;
	}
	offset &= EXT2_BLOCK_SIZE(inode->i_sb) - 1;
	raw_inode = (struct ext2_inode *) (bh->b_data + offset);

	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	raw_inode->i_uid = cpu_to_le16(inode->i_uid);
	raw_inode->i_gid = cpu_to_le16(inode->i_gid);
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
	raw_inode->i_size = cpu_to_le32(inode->i_size);
	raw_inode->i_atime = cpu_to_le32(inode->i_atime);
	raw_inode->i_ctime = cpu_to_le32(inode->i_ctime);
	raw_inode->i_mtime = cpu_to_le32(inode->i_mtime);
	raw_inode->i_blocks = cpu_to_le32(inode->i_blocks);
	raw_inode->i_dtime = cpu_to_le32(inode->u.ext2_i.i_dtime);
	raw_inode->i_flags = cpu_to_le32(inode->u.ext2_i.i_flags);
	raw_inode->i_faddr = cpu_to_le32(inode->u.ext2_i.i_faddr);
	raw_inode->i_frag = inode->u.ext2_i.i_frag_no;
	raw_inode->i_fsize = inode->u.ext2_i.i_frag_size;
	raw_inode->i_file_acl = cpu_to_le32(inode->u.ext2_i.i_file_acl);
	if (S_ISDIR(inode->i_mode))
		raw_inode->i_dir_acl = cpu_to_le32(inode->u.ext2_i.i_dir_acl);
	else { 
#if BITS_PER_LONG < 64
		raw_inode->i_size_high =
			cpu_to_le32(inode->u.ext2_i.i_high_size);
#else
		raw_inode->i_size_high = cpu_to_le32(inode->i_size >> 32);
#endif
	}
	raw_inode->i_version = cpu_to_le32(inode->u.ext2_i.i_version);
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_block[0] = cpu_to_le32(kdev_t_to_nr(inode->i_rdev));
	else if (S_ISLNK(inode->i_mode) && !inode->i_blocks)
		for (block = 0; block < EXT2_N_BLOCKS; block++)
			raw_inode->i_block[block] = inode->u.ext2_i.i_data[block];
	else for (block = 0; block < EXT2_N_BLOCKS; block++)
		raw_inode->i_block[block] = cpu_to_le32(inode->u.ext2_i.i_data[block]);
	mark_buffer_dirty(bh, 1);
	if (do_sync) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing ext2 inode ["
				"%s:%08lx]\n",
				kdevname(inode->i_dev), inode->i_ino);
			err = -EIO;
		}
	}
	brelse (bh);
	return err;
}

void ext2_write_inode (struct inode * inode)
{
	ext2_update_inode (inode, 0);
}

int ext2_sync_inode (struct inode *inode)
{
	return ext2_update_inode (inode, 1);
}

int ext2_notify_change(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int		retval;
	unsigned int	flags;
	
	retval = -EPERM;
	if ((iattr->ia_attr_flags &
	     (ATTR_FLAG_APPEND | ATTR_FLAG_IMMUTABLE)) ^
	    (inode->u.ext2_i.i_flags &
	     (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL))) {
		if (!capable(CAP_LINUX_IMMUTABLE))
			goto out;
	} else if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
		goto out;

	retval = inode_change_ok(inode, iattr);
	if (retval != 0)
		goto out;

	inode_setattr(inode, iattr);
	
	flags = iattr->ia_attr_flags;
	if (flags & ATTR_FLAG_SYNCRONOUS) {
		inode->i_flags |= MS_SYNCHRONOUS;
		inode->u.ext2_i.i_flags = EXT2_SYNC_FL;
	} else {
		inode->i_flags &= ~MS_SYNCHRONOUS;
		inode->u.ext2_i.i_flags &= ~EXT2_SYNC_FL;
	}
	if (flags & ATTR_FLAG_NOATIME) {
		inode->i_flags |= MS_NOATIME;
		inode->u.ext2_i.i_flags = EXT2_NOATIME_FL;
	} else {
		inode->i_flags &= ~MS_NOATIME;
		inode->u.ext2_i.i_flags &= ~EXT2_NOATIME_FL;
	}
	if (flags & ATTR_FLAG_APPEND) {
		inode->i_flags |= S_APPEND;
		inode->u.ext2_i.i_flags = EXT2_APPEND_FL;
	} else {
		inode->i_flags &= ~S_APPEND;
		inode->u.ext2_i.i_flags &= ~EXT2_APPEND_FL;
	}
	if (flags & ATTR_FLAG_IMMUTABLE) {
		inode->i_flags |= S_IMMUTABLE;
		inode->u.ext2_i.i_flags = EXT2_IMMUTABLE_FL;
	} else {
		inode->i_flags &= ~S_IMMUTABLE;
		inode->u.ext2_i.i_flags &= ~EXT2_IMMUTABLE_FL;
	}
	mark_inode_dirty(inode);
out:
	return retval;
}

