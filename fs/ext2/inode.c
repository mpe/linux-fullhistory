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

#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>



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
	if (is_bad_inode(inode) ||
	    inode->i_ino == EXT2_ACL_IDX_INO ||
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

#define inode_bmap(inode, nr) (le32_to_cpu((inode)->u.ext2_i.i_data[(nr)]))

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

static int ext2_alloc_block (struct inode * inode, unsigned long goal, int *err)
{
#ifdef EXT2FS_DEBUG
	static unsigned long alloc_hits = 0, alloc_attempts = 0;
#endif
	unsigned long result;

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

static inline long ext2_block_map (struct inode * inode, long block)
{
	int i, ret;
	int ptrs = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int ptrs_bits = EXT2_ADDR_PER_BLOCK_BITS(inode->i_sb);

	ret = 0;
	lock_kernel();
	if (block < 0) {
		ext2_warning (inode->i_sb, "ext2_block_map", "block < 0");
		goto out;
	}
	if (block >= EXT2_NDIR_BLOCKS + ptrs +
		(1 << (ptrs_bits * 2)) +
		((1 << (ptrs_bits * 2)) << ptrs_bits)) {
		ext2_warning (inode->i_sb, "ext2_block_map", "block > big");
		goto out;
	}
	if (block < EXT2_NDIR_BLOCKS) {
		ret = inode_bmap (inode, block);
		goto out;
	}
	block -= EXT2_NDIR_BLOCKS;
	if (block < ptrs) {
		i = inode_bmap (inode, EXT2_IND_BLOCK);
		if (!i)
			goto out;
		ret = block_bmap (bread (inode->i_dev, i,
					  inode->i_sb->s_blocksize), block);
		goto out;
	}
	block -= ptrs;
	if (block < (1 << (ptrs_bits * 2))) {
		i = inode_bmap (inode, EXT2_DIND_BLOCK);
		if (!i)
			goto out;
		i = block_bmap (bread (inode->i_dev, i,
				       inode->i_sb->s_blocksize),
				block >> ptrs_bits);
		if (!i)
			goto out;
		ret = block_bmap (bread (inode->i_dev, i,
					  inode->i_sb->s_blocksize),
				block & (ptrs - 1));
		goto out;
	}
	block -= (1 << (ptrs_bits * 2));
	i = inode_bmap (inode, EXT2_TIND_BLOCK);
	if (!i)
		goto out;
	i = block_bmap (bread (inode->i_dev, i, inode->i_sb->s_blocksize),
			block >> (ptrs_bits * 2));
	if (!i)
		goto out;
	i = block_bmap (bread (inode->i_dev, i, inode->i_sb->s_blocksize),
			(block >> ptrs_bits) & (ptrs - 1));
	if (!i)
		goto out;
	ret = block_bmap (bread (inode->i_dev, i, inode->i_sb->s_blocksize),
			   block & (ptrs - 1));
out:
	unlock_kernel();
	return ret;
}

static struct buffer_head * inode_getblk (struct inode * inode, int nr,
	int new_block, int * err, int metadata, long *phys, int *new)
{
	u32 * p;
	int tmp, goal = 0;
	struct buffer_head * result;
	int blocksize = inode->i_sb->s_blocksize;

	p = inode->u.ext2_i.i_data + nr;
repeat:
	tmp = le32_to_cpu(*p);
	if (tmp) {
		if (metadata) {
			result = getblk (inode->i_dev, tmp, blocksize);
			if (tmp == le32_to_cpu(*p))
				return result;
			brelse (result);
			goto repeat;
		} else {
			*phys = tmp;
			return NULL;
		}
	}
	*err = -EFBIG;

	/* Check file limits.. */
	{
		unsigned long limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
		if (limit < RLIM_INFINITY) {
			limit >>= EXT2_BLOCK_SIZE_BITS(inode->i_sb);
			if (new_block >= limit) {
				send_sig(SIGXFSZ, current, 0);
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
				goal = le32_to_cpu(inode->u.ext2_i.i_data[tmp]);
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
	if (!tmp) {
		*err = -ENOSPC;
		return NULL;
	}
	if (metadata) {
		result = getblk (inode->i_dev, tmp, blocksize);
		memset(result->b_data, 0, blocksize);
		mark_buffer_uptodate(result, 1);
		mark_buffer_dirty(result, 1);
		if (*p) {
			ext2_free_blocks (inode, tmp, 1);
			bforget (result);
			goto repeat;
		}
	} else {
		if (*p) {
			/*
			 * Nobody is allowed to change block allocation
			 * state from under us:
			 */
			ext2_error (inode->i_sb, "block_getblk",
				    "data block filled under us");
			BUG();
			ext2_free_blocks (inode, tmp, 1);
			goto repeat;
		}
		*phys = tmp;
		result = NULL;
		*err = 0;
		*new = 1;
	}
	*p = cpu_to_le32(tmp);

	inode->u.ext2_i.i_next_alloc_block = new_block;
	inode->u.ext2_i.i_next_alloc_goal = tmp;
	inode->i_ctime = CURRENT_TIME;
	inode->i_blocks += blocksize/512;
	if (IS_SYNC(inode) || inode->u.ext2_i.i_osync)
		ext2_sync_inode (inode);
	else
		mark_inode_dirty(inode);
	return result;
}

/*
 *   metadata / data
 *   possibly create / access
 *   can fail due to: - not present
 *                    - out of space
 *
 *   NULL return in the data case is mandatory.
 */
static struct buffer_head * block_getblk (struct inode * inode,
	  struct buffer_head * bh, int nr,
	  int new_block, int * err, int metadata, long *phys, int *new)
{
	int tmp, goal = 0;
	u32 * p;
	struct buffer_head * result;
	int blocksize = inode->i_sb->s_blocksize;
	unsigned long limit;

	result = NULL;	
	if (!bh)
		goto out;
	if (!buffer_uptodate(bh)) {
		ll_rw_block (READ, 1, &bh);
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh))
			goto out;
	}
	p = (u32 *) bh->b_data + nr;
repeat:
	tmp = le32_to_cpu(*p);
	if (tmp) {
		if (metadata) {
			result = getblk (bh->b_dev, tmp, blocksize);
			if (tmp == le32_to_cpu(*p))
				goto out;
			brelse (result);
			goto repeat;
		} else {
			*phys = tmp;
			/* result == NULL */
			goto out;
		}
	}
	*err = -EFBIG;

	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit < RLIM_INFINITY) {
		limit >>= EXT2_BLOCK_SIZE_BITS(inode->i_sb);
		if (new_block >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
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
	if (!tmp)
		goto out;
	if (metadata) {
		result = getblk (bh->b_dev, tmp, blocksize);
		memset(result->b_data, 0, inode->i_sb->s_blocksize);
		mark_buffer_uptodate(result, 1);
		mark_buffer_dirty(result, 1);
		if (*p) {
			ext2_free_blocks (inode, tmp, 1);
			bforget (result);
			goto repeat;
		}
	} else {
		if (*p) {
			/*
			 * Nobody is allowed to change block allocation
			 * state from under us:
			 */
			ext2_error (inode->i_sb, "block_getblk",
				    "data block filled under us");
			BUG();
			ext2_free_blocks (inode, tmp, 1);
			goto repeat;
		}
		*phys = tmp;
		*new = 1;
	}
	*p = le32_to_cpu(tmp);
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(inode) || inode->u.ext2_i.i_osync) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	inode->i_ctime = CURRENT_TIME;
	inode->i_blocks += blocksize/512;
	mark_inode_dirty(inode);
	inode->u.ext2_i.i_next_alloc_block = new_block;
	inode->u.ext2_i.i_next_alloc_goal = tmp;
	*err = 0;
out:
	brelse (bh);
	return result;
}

int ext2_get_block(struct inode *inode, long iblock, struct buffer_head *bh_result, int create)
{
	int ret, err, new;
	struct buffer_head *bh;
	unsigned long ptr, phys;
	/*
	 * block pointers per block
	 */
	unsigned long ptrs = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int ptrs_bits = EXT2_ADDR_PER_BLOCK_BITS(inode->i_sb);
	const int direct_blocks = EXT2_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2)),
		triple_blocks = (1 << (ptrs_bits * 3));

	if (!create) {
		/*
		 * Will clean this up further, ext2_block_map() should use the
		 * bh instead of an integer block-number interface.
		 */
		phys = ext2_block_map(inode, iblock);
		if (phys) {
			bh_result->b_dev = inode->i_dev;
			bh_result->b_blocknr = phys;
			bh_result->b_state |= (1UL << BH_Mapped);
		}
		return 0;
	}

	err = -EIO;
	new = 0;
	ret = 0;
	bh = NULL;

	lock_kernel();

	if (iblock < 0)
		goto abort_negative;
	if (iblock > direct_blocks + indirect_blocks +
					 double_blocks + triple_blocks)
		goto abort_too_big;

	/*
	 * If this is a sequential block allocation, set the next_alloc_block
	 * to this block now so that all the indblock and data block
	 * allocations use the same goal zone
	 */

	ext2_debug ("block %lu, next %lu, goal %lu.\n", iblock, 
		    inode->u.ext2_i.i_next_alloc_block,
		    inode->u.ext2_i.i_next_alloc_goal);

	if (iblock == inode->u.ext2_i.i_next_alloc_block + 1) {
		inode->u.ext2_i.i_next_alloc_block++;
		inode->u.ext2_i.i_next_alloc_goal++;
	}

	err = 0;
	ptr = iblock;

	/*
	 * ok, these macros clean the logic up a bit and make
	 * it much more readable:
	 */
#define GET_INODE_DATABLOCK(x) \
		inode_getblk(inode, x, iblock, &err, 0, &phys, &new)
#define GET_INODE_PTR(x) \
		inode_getblk(inode, x, iblock, &err, 1, NULL, NULL)
#define GET_INDIRECT_DATABLOCK(x) \
		block_getblk (inode, bh, x, iblock, &err, 0, &phys, &new);
#define GET_INDIRECT_PTR(x) \
		block_getblk (inode, bh, x, iblock, &err, 1, NULL, NULL);

	if (ptr < direct_blocks) {
		bh = GET_INODE_DATABLOCK(ptr);
		goto out;
	}
	ptr -= direct_blocks;
	if (ptr < indirect_blocks) {
		bh = GET_INODE_PTR(EXT2_IND_BLOCK);
		goto get_indirect;
	}
	ptr -= indirect_blocks;
	if (ptr < double_blocks) {
		bh = GET_INODE_PTR(EXT2_DIND_BLOCK);
		goto get_double;
	}
	ptr -= double_blocks;
	bh = GET_INODE_PTR(EXT2_TIND_BLOCK);
	bh = GET_INDIRECT_PTR(ptr >> (ptrs_bits * 2));
get_double:
	bh = GET_INDIRECT_PTR((ptr >> ptrs_bits) & (ptrs - 1));
get_indirect:
	bh = GET_INDIRECT_DATABLOCK(ptr & (ptrs - 1));

#undef GET_INODE_DATABLOCK
#undef GET_INODE_PTR
#undef GET_INDIRECT_DATABLOCK
#undef GET_INDIRECT_PTR

out:
	if (bh)
		BUG();	// temporary debugging check
	if (err)
		goto abort;
	if (!phys)
		BUG();	// must not happen either

	bh_result->b_dev = inode->i_dev;
	bh_result->b_blocknr = phys;
	bh_result->b_state |= (1UL << BH_Mapped); /* safe */
	if (new)
		bh_result->b_state |= (1UL << BH_New);
abort:
	unlock_kernel();
	return err;

abort_negative:
	ext2_warning (inode->i_sb, "ext2_get_block", "block < 0");
	goto abort;

abort_too_big:
	ext2_warning (inode->i_sb, "ext2_get_block", "block > big");
	goto abort;
}

struct buffer_head * ext2_getblk(struct inode * inode, long block, int create, int * err)
{
	struct buffer_head dummy;
	int error;

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	error = ext2_get_block(inode, block, &dummy, create);
	*err = error;
	if (!error && buffer_mapped(&dummy)) {
		struct buffer_head *bh;
		bh = getblk(dummy.b_dev, dummy.b_blocknr, inode->i_sb->s_blocksize);
		if (buffer_new(&dummy)) {
			memset(bh->b_data, 0, inode->i_sb->s_blocksize);
			mark_buffer_uptodate(bh, 1);
			mark_buffer_dirty(bh, 1);
		}
		return bh;
	}
	return NULL;
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
	/* We now have enough fields to check if the inode was active or not.
	 * This is needed because nfsd might try to access dead inodes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (inode->i_nlink == 0 && (inode->i_mode == 0 || inode->u.ext2_i.i_dtime)) {
		/* this inode is deleted */
		brelse (bh);
		goto bad_inode;
	}
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
		inode->u.ext2_i.i_high_size = le32_to_cpu(raw_inode->i_size_high);
		inode->i_size |= ((__u64)le32_to_cpu(raw_inode->i_size_high)) << 32;
	}
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);
	inode->u.ext2_i.i_block_group = block_group;
	inode->u.ext2_i.i_next_alloc_block = 0;
	inode->u.ext2_i.i_next_alloc_goal = 0;
	if (inode->u.ext2_i.i_prealloc_count)
		ext2_error (inode->i_sb, "ext2_read_inode",
			    "New inode has non-zero prealloc count!");

	/*
	 * NOTE! The in-memory inode i_blocks array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (block = 0; block < EXT2_N_BLOCKS; block++)
		inode->u.ext2_i.i_data[block] = raw_inode->i_block[block];

	if (inode->i_ino == EXT2_ACL_IDX_INO ||
	    inode->i_ino == EXT2_ACL_DATA_INO)
		/* Nothing to do */ ;
	else if (S_ISREG(inode->i_mode))
		inode->i_op = &ext2_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &ext2_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = inode->i_blocks
				?&ext2_symlink_inode_operations
				:&ext2_fast_symlink_inode_operations;
	else 
		init_special_inode(inode, inode->i_mode,
				   le32_to_cpu(raw_inode->i_block[0]));
	brelse (bh);
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
	else
		raw_inode->i_size_high = cpu_to_le32(inode->i_size >> 32);

	raw_inode->i_generation = cpu_to_le32(inode->i_generation);
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_block[0] = cpu_to_le32(kdev_t_to_nr(inode->i_rdev));
	else for (block = 0; block < EXT2_N_BLOCKS; block++)
		raw_inode->i_block[block] = inode->u.ext2_i.i_data[block];
	mark_buffer_dirty(bh, 1);
	if (do_sync) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing ext2 inode ["
				"%s:%08lx]\n",
				bdevname(inode->i_dev), inode->i_ino);
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
	if (iattr->ia_valid & ATTR_ATTR_FLAG &&
	    ((!(iattr->ia_attr_flags & ATTR_FLAG_APPEND) !=
	      !(inode->u.ext2_i.i_flags & EXT2_APPEND_FL)) ||
	     (!(iattr->ia_attr_flags & ATTR_FLAG_IMMUTABLE) !=
	      !(inode->u.ext2_i.i_flags & EXT2_IMMUTABLE_FL)))) {
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

