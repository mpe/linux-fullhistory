/*
 *  linux/fs/ext2/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by 
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/config.h>
#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/random.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

/*
 * ialloc.c contains the inodes allocation and deallocation routines
 */

/*
 * The free inodes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.
 */


/*
 * Read the inode allocation bitmap for a given block_group, reading
 * into the specified slot in the superblock's bitmap cache.
 *
 * Return buffer_head of bitmap on success or NULL.
 */
static struct buffer_head *
read_inode_bitmap(struct super_block * sb, unsigned long block_group)
{
	struct ext2_group_desc *desc;
	struct buffer_head *bh = NULL;

	desc = ext2_get_group_desc(sb, block_group, NULL);
	if (!desc)
		goto error_out;

	bh = sb_bread(sb, le32_to_cpu(desc->bg_inode_bitmap));
	if (!bh)
		ext2_error(sb, "read_inode_bitmap",
			    "Cannot read inode bitmap - "
			    "block_group = %lu, inode_bitmap = %lu",
			    block_group, (unsigned long) desc->bg_inode_bitmap);
error_out:
	return bh;
}

/*
 * NOTE! When we get the inode, we're the only people
 * that have access to it, and as such there are no
 * race conditions we have to worry about. The inode
 * is not on the hash-lists, and it cannot be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 *
 * HOWEVER: we must make sure that we get no aliases,
 * which means that we have to call "clear_inode()"
 * _before_ we mark the inode not in use in the inode
 * bitmaps. Otherwise a newly created file might use
 * the same inode number (not actually the same pointer
 * though), and then we'd have two inodes sharing the
 * same inode number and space on the harddisk.
 */
void ext2_free_inode (struct inode * inode)
{
	struct super_block * sb = inode->i_sb;
	int is_directory;
	unsigned long ino;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	unsigned long block_group;
	unsigned long bit;
	struct ext2_group_desc * desc;
	struct ext2_super_block * es;

	ino = inode->i_ino;
	ext2_debug ("freeing inode %lu\n", ino);

	/*
	 * Note: we must free any quota before locking the superblock,
	 * as writing the quota to disk may need the lock as well.
	 */
	if (!is_bad_inode(inode)) {
		/* Quota is already initialized in iput() */
		ext2_xattr_delete_inode(inode);
	    	DQUOT_FREE_INODE(inode);
		DQUOT_DROP(inode);
	}

	lock_super (sb);
	es = EXT2_SB(sb)->s_es;
	is_directory = S_ISDIR(inode->i_mode);

	/* Do this BEFORE marking the inode not in use or returning an error */
	clear_inode (inode);

	if (ino < EXT2_FIRST_INO(sb) ||
	    ino > le32_to_cpu(es->s_inodes_count)) {
		ext2_error (sb, "ext2_free_inode",
			    "reserved or nonexistent inode %lu", ino);
		goto error_return;
	}
	block_group = (ino - 1) / EXT2_INODES_PER_GROUP(sb);
	bit = (ino - 1) % EXT2_INODES_PER_GROUP(sb);
	brelse(bitmap_bh);
	bitmap_bh = read_inode_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;

	/* Ok, now we can actually update the inode bitmaps.. */
	if (!ext2_clear_bit(bit, bitmap_bh->b_data))
		ext2_error (sb, "ext2_free_inode",
			      "bit already cleared for inode %lu", ino);
	else {
		desc = ext2_get_group_desc (sb, block_group, &bh2);
		if (desc) {
			desc->bg_free_inodes_count =
				cpu_to_le16(le16_to_cpu(desc->bg_free_inodes_count) + 1);
			if (is_directory) {
				desc->bg_used_dirs_count =
					cpu_to_le16(le16_to_cpu(desc->bg_used_dirs_count) - 1);
				EXT2_SB(sb)->s_dir_count--;
			}
		}
		mark_buffer_dirty(bh2);
		es->s_free_inodes_count =
			cpu_to_le32(le32_to_cpu(es->s_free_inodes_count) + 1);
		mark_buffer_dirty(EXT2_SB(sb)->s_sbh);
	}
	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ll_rw_block(WRITE, 1, &bitmap_bh);
		wait_on_buffer(bitmap_bh);
	}
	sb->s_dirt = 1;
error_return:
	brelse(bitmap_bh);
	unlock_super (sb);
}

/*
 * We perform asynchronous prereading of the new inode's inode block when
 * we create the inode, in the expectation that the inode will be written
 * back soon.  There are two reasons:
 *
 * - When creating a large number of files, the async prereads will be
 *   nicely merged into large reads
 * - When writing out a large number of inodes, we don't need to keep on
 *   stalling the writes while we read the inode block.
 *
 * FIXME: ext2_get_group_desc() needs to be simplified.
 */
static void ext2_preread_inode(struct inode *inode)
{
	unsigned long block_group;
	unsigned long offset;
	unsigned long block;
	struct buffer_head *bh;
	struct ext2_group_desc * gdp;
	struct backing_dev_info *bdi;

	bdi = inode->i_mapping->backing_dev_info;
	if (bdi_read_congested(bdi))
		return;
	if (bdi_write_congested(bdi))
		return;

	block_group = (inode->i_ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
	gdp = ext2_get_group_desc(inode->i_sb, block_group, &bh);
	if (gdp == NULL)
		return;

	/*
	 * Figure out the offset within the block group inode table
	 */
	offset = ((inode->i_ino - 1) % EXT2_INODES_PER_GROUP(inode->i_sb)) *
				EXT2_INODE_SIZE(inode->i_sb);
	block = le32_to_cpu(gdp->bg_inode_table) +
				(offset >> EXT2_BLOCK_SIZE_BITS(inode->i_sb));
	bh = sb_getblk(inode->i_sb, block);
	if (!buffer_uptodate(bh) && !buffer_locked(bh))
		ll_rw_block(READA, 1, &bh);
	__brelse(bh);
}

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory\'s block
 * group to find a free inode.
 */
static int find_group_dir(struct super_block *sb, struct inode *parent)
{
	struct ext2_super_block * es = EXT2_SB(sb)->s_es;
	int ngroups = EXT2_SB(sb)->s_groups_count;
	int avefreei = le32_to_cpu(es->s_free_inodes_count) / ngroups;
	struct ext2_group_desc *desc, *best_desc = NULL;
	struct buffer_head *bh, *best_bh = NULL;
	int group, best_group = -1;

	for (group = 0; group < ngroups; group++) {
		desc = ext2_get_group_desc (sb, group, &bh);
		if (!desc || !desc->bg_free_inodes_count)
			continue;
		if (le16_to_cpu(desc->bg_free_inodes_count) < avefreei)
			continue;
		if (!best_desc || 
		    (le16_to_cpu(desc->bg_free_blocks_count) >
		     le16_to_cpu(best_desc->bg_free_blocks_count))) {
			best_group = group;
			best_desc = desc;
			best_bh = bh;
		}
	}
	if (!best_desc)
		return -1;
	best_desc->bg_free_inodes_count =
		cpu_to_le16(le16_to_cpu(best_desc->bg_free_inodes_count) - 1);
	best_desc->bg_used_dirs_count =
		cpu_to_le16(le16_to_cpu(best_desc->bg_used_dirs_count) + 1);
	mark_buffer_dirty(best_bh);
	return best_group;
}

/* 
 * Orlov's allocator for directories. 
 * 
 * We always try to spread first-level directories.
 *
 * If there are blockgroups with both free inodes and free blocks counts 
 * not worse than average we return one with smallest directory count. 
 * Otherwise we simply return a random group. 
 * 
 * For the rest rules look so: 
 * 
 * It's OK to put directory into a group unless 
 * it has too many directories already (max_dirs) or 
 * it has too few free inodes left (min_inodes) or 
 * it has too few free blocks left (min_blocks) or 
 * it's already running too large debt (max_debt). 
 * Parent's group is prefered, if it doesn't satisfy these 
 * conditions we search cyclically through the rest. If none 
 * of the groups look good we just look for a group with more 
 * free inodes than average (starting at parent's group). 
 * 
 * Debt is incremented each time we allocate a directory and decremented 
 * when we allocate an inode, within 0--255. 
 */ 

#define INODE_COST 64
#define BLOCK_COST 256

static int find_group_orlov(struct super_block *sb, struct inode *parent)
{
	int parent_group = EXT2_I(parent)->i_block_group;
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = sbi->s_es;
	int ngroups = sbi->s_groups_count;
	int inodes_per_group = EXT2_INODES_PER_GROUP(sb);
	int avefreei = le32_to_cpu(es->s_free_inodes_count) / ngroups;
	int avefreeb = le32_to_cpu(es->s_free_blocks_count) / ngroups;
	int blocks_per_dir;
	int ndirs = sbi->s_dir_count;
	int max_debt, max_dirs, min_blocks, min_inodes;
	int group = -1, i;
	struct ext2_group_desc *desc;
	struct buffer_head *bh;

	if ((parent == sb->s_root->d_inode) ||
	    (parent->i_flags & EXT2_TOPDIR_FL)) {
		struct ext2_group_desc *best_desc = NULL;
		struct buffer_head *best_bh = NULL;
		int best_ndir = inodes_per_group;
		int best_group = -1;

		get_random_bytes(&group, sizeof(group));
		parent_group = (unsigned)group % ngroups;
		for (i = 0; i < ngroups; i++) {
			group = (parent_group + i) % ngroups;
			desc = ext2_get_group_desc (sb, group, &bh);
			if (!desc || !desc->bg_free_inodes_count)
				continue;
			if (le16_to_cpu(desc->bg_used_dirs_count) >= best_ndir)
				continue;
			if (le16_to_cpu(desc->bg_free_inodes_count) < avefreei)
				continue;
			if (le16_to_cpu(desc->bg_free_blocks_count) < avefreeb)
				continue;
			best_group = group;
			best_ndir = le16_to_cpu(desc->bg_used_dirs_count);
			best_desc = desc;
			best_bh = bh;
		}
		if (best_group >= 0) {
			desc = best_desc;
			bh = best_bh;
			group = best_group;
			goto found;
		}
		goto fallback;
	}

	blocks_per_dir = (le32_to_cpu(es->s_blocks_count) -
			  le32_to_cpu(es->s_free_blocks_count)) / ndirs;

	max_dirs = ndirs / ngroups + inodes_per_group / 16;
	min_inodes = avefreei - inodes_per_group / 4;
	min_blocks = avefreeb - EXT2_BLOCKS_PER_GROUP(sb) / 4;

	max_debt = EXT2_BLOCKS_PER_GROUP(sb) / max(blocks_per_dir, BLOCK_COST);
	if (max_debt * INODE_COST > inodes_per_group)
		max_debt = inodes_per_group / INODE_COST;
	if (max_debt > 255)
		max_debt = 255;
	if (max_debt == 0)
		max_debt = 1;

	for (i = 0; i < ngroups; i++) {
		group = (parent_group + i) % ngroups;
		desc = ext2_get_group_desc (sb, group, &bh);
		if (!desc || !desc->bg_free_inodes_count)
			continue;
		if (sbi->s_debts[group] >= max_debt)
			continue;
		if (le16_to_cpu(desc->bg_used_dirs_count) >= max_dirs)
			continue;
		if (le16_to_cpu(desc->bg_free_inodes_count) < min_inodes)
			continue;
		if (le16_to_cpu(desc->bg_free_blocks_count) < min_blocks)
			continue;
		goto found;
	}

fallback:
	for (i = 0; i < ngroups; i++) {
		group = (parent_group + i) % ngroups;
		desc = ext2_get_group_desc (sb, group, &bh);
		if (!desc || !desc->bg_free_inodes_count)
			continue;
		if (le16_to_cpu(desc->bg_free_inodes_count) >= avefreei)
			goto found;
	}

	return -1;

found:
	desc->bg_free_inodes_count =
		cpu_to_le16(le16_to_cpu(desc->bg_free_inodes_count) - 1);
	desc->bg_used_dirs_count =
		cpu_to_le16(le16_to_cpu(desc->bg_used_dirs_count) + 1);
	sbi->s_dir_count++;
	mark_buffer_dirty(bh);
	return group;
}

static int find_group_other(struct super_block *sb, struct inode *parent)
{
	int parent_group = EXT2_I(parent)->i_block_group;
	int ngroups = EXT2_SB(sb)->s_groups_count;
	struct ext2_group_desc *desc;
	struct buffer_head *bh;
	int group, i;

	/*
	 * Try to place the inode in its parent directory
	 */
	group = parent_group;
	desc = ext2_get_group_desc (sb, group, &bh);
	if (desc && le16_to_cpu(desc->bg_free_inodes_count) &&
			le16_to_cpu(desc->bg_free_blocks_count))
		goto found;

	/*
	 * We're going to place this inode in a different blockgroup from its
	 * parent.  We want to cause files in a common directory to all land in
	 * the same blockgroup.  But we want files which are in a different
	 * directory which shares a blockgroup with our parent to land in a
	 * different blockgroup.
	 *
	 * So add our directory's i_ino into the starting point for the hash.
	 */
	group = (group + parent->i_ino) % ngroups;

	/*
	 * Use a quadratic hash to find a group with a free inode and some
	 * free blocks.
	 */
	for (i = 1; i < ngroups; i <<= 1) {
		group += i;
		if (group >= ngroups)
			group -= ngroups;
		desc = ext2_get_group_desc (sb, group, &bh);
		if (desc && le16_to_cpu(desc->bg_free_inodes_count) &&
				le16_to_cpu(desc->bg_free_blocks_count))
			goto found;
	}

	/*
	 * That failed: try linear search for a free inode, even if that group
	 * has no free blocks.
	 */
	group = parent_group + 1;
	for (i = 2; i < ngroups; i++) {
		if (++group >= ngroups)
			group = 0;
		desc = ext2_get_group_desc (sb, group, &bh);
		if (desc && le16_to_cpu(desc->bg_free_inodes_count))
			goto found;
	}

	return -1;

found:
	desc->bg_free_inodes_count =
		cpu_to_le16(le16_to_cpu(desc->bg_free_inodes_count) - 1);
	mark_buffer_dirty(bh);
	return group;
}

struct inode * ext2_new_inode(struct inode * dir, int mode)
{
	struct super_block *sb;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	int group, i;
	ino_t ino;
	struct inode * inode;
	struct ext2_group_desc * desc;
	struct ext2_super_block * es;
	struct ext2_inode_info *ei;
	int err;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	ei = EXT2_I(inode);
	lock_super (sb);
	es = EXT2_SB(sb)->s_es;
repeat:
	if (S_ISDIR(mode)) {
		if (test_opt (sb, OLDALLOC))
			group = find_group_dir(sb, dir);
		else
			group = find_group_orlov(sb, dir);
	} else 
		group = find_group_other(sb, dir);

	err = -ENOSPC;
	if (group == -1)
		goto fail;

	err = -EIO;
	bitmap_bh = read_inode_bitmap(sb, group);
	if (!bitmap_bh)
		goto fail2;

	i = ext2_find_first_zero_bit((unsigned long *)bitmap_bh->b_data,
				      EXT2_INODES_PER_GROUP(sb));
	if (i >= EXT2_INODES_PER_GROUP(sb))
		goto bad_count;
	ext2_set_bit(i, bitmap_bh->b_data);

	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ll_rw_block(WRITE, 1, &bitmap_bh);
		wait_on_buffer(bitmap_bh);
	}
	brelse(bitmap_bh);

	ino = group * EXT2_INODES_PER_GROUP(sb) + i + 1;
	if (ino < EXT2_FIRST_INO(sb) || ino > le32_to_cpu(es->s_inodes_count)) {
		ext2_error (sb, "ext2_new_inode",
			    "reserved inode or inode > inodes count - "
			    "block_group = %d,inode=%lu", group,
			    (unsigned long) ino);
		err = -EIO;
		goto fail2;
	}

	es->s_free_inodes_count =
		cpu_to_le32(le32_to_cpu(es->s_free_inodes_count) - 1);

	if (S_ISDIR(mode)) {
		if (EXT2_SB(sb)->s_debts[group] < 255)
			EXT2_SB(sb)->s_debts[group]++;
	} else {
		if (EXT2_SB(sb)->s_debts[group])
			EXT2_SB(sb)->s_debts[group]--;
	}

	mark_buffer_dirty(EXT2_SB(sb)->s_sbh);
	sb->s_dirt = 1;
	inode->i_uid = current->fsuid;
	if (test_opt (sb, GRPID))
		inode->i_gid = dir->i_gid;
	else if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;
	inode->i_mode = mode;

	inode->i_ino = ino;
	inode->i_blksize = PAGE_SIZE;	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_flags = EXT2_I(dir)->i_flags;
	if (S_ISLNK(mode))
		ei->i_flags &= ~(EXT2_IMMUTABLE_FL|EXT2_APPEND_FL);
	/* dirsync is only applied to directories */
	if (!S_ISDIR(mode))
		ei->i_flags &= ~EXT2_DIRSYNC_FL;
	ei->i_faddr = 0;
	ei->i_frag_no = 0;
	ei->i_frag_size = 0;
	ei->i_file_acl = 0;
	ei->i_dir_acl = 0;
	ei->i_dtime = 0;
	ei->i_block_group = group;
	ei->i_next_alloc_block = 0;
	ei->i_next_alloc_goal = 0;
	ei->i_prealloc_block = 0;
	ei->i_prealloc_count = 0;
	ei->i_dir_start_lookup = 0;
	ei->i_state = EXT2_STATE_NEW;
	if (ei->i_flags & EXT2_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (ei->i_flags & EXT2_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
	inode->i_generation = EXT2_SB(sb)->s_next_generation++;
	insert_inode_hash(inode);

	unlock_super(sb);
	if(DQUOT_ALLOC_INODE(inode)) {
		DQUOT_DROP(inode);
		goto fail3;
	}
	err = ext2_init_acl(inode, dir);
	if (err) {
		DQUOT_FREE_INODE(inode);
		goto fail3;
	}
	mark_inode_dirty(inode);
	ext2_debug("allocating inode %lu\n", inode->i_ino);
	ext2_preread_inode(inode);
	return inode;

fail3:
	inode->i_flags |= S_NOQUOTA;
	inode->i_nlink = 0;
	iput(inode);
	return ERR_PTR(err);

fail2:
	desc = ext2_get_group_desc (sb, group, &bh2);
	desc->bg_free_inodes_count =
		cpu_to_le16(le16_to_cpu(desc->bg_free_inodes_count) + 1);
	if (S_ISDIR(mode))
		desc->bg_used_dirs_count =
			cpu_to_le16(le16_to_cpu(desc->bg_used_dirs_count) - 1);
	mark_buffer_dirty(bh2);
fail:
	unlock_super(sb);
	make_bad_inode(inode);
	iput(inode);
	return ERR_PTR(err);

bad_count:
	brelse(bitmap_bh);
	ext2_error (sb, "ext2_new_inode",
		    "Free inodes count corrupted in group %d",
		    group);
	/* Is it really ENOSPC? */
	err = -ENOSPC;
	if (sb->s_flags & MS_RDONLY)
		goto fail;

	desc = ext2_get_group_desc (sb, group, &bh2);
	desc->bg_free_inodes_count = 0;
	mark_buffer_dirty(bh2);
	goto repeat;
}

unsigned long ext2_count_free_inodes (struct super_block * sb)
{
#ifdef EXT2FS_DEBUG
	struct ext2_super_block * es;
	unsigned long desc_count = 0, bitmap_count = 0;
	struct buffer_head *bitmap_bh = NULL;
	int i;

	lock_super (sb);
	es = EXT2_SB(sb)->s_es;
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		struct ext2_group_desc *desc;
		unsigned x;

		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		x = ext2_count_free(bitmap_bh, EXT2_INODES_PER_GROUP(sb) / 8);
		printk ("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(desc->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext2_count_free_inodes: stored = %lu, computed = %lu, %lu\n",
		le32_to_cpu(es->s_free_inodes_count), desc_count, bitmap_count);
	unlock_super(sb);
	return desc_count;
#else
	return le32_to_cpu(EXT2_SB(sb)->s_es->s_free_inodes_count);
#endif
}

/* Called at mount-time, super-block is locked */
unsigned long ext2_count_dirs (struct super_block * sb)
{
	unsigned long count = 0;
	int i;

	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		struct ext2_group_desc *gdp = ext2_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		count += le16_to_cpu(gdp->bg_used_dirs_count);
	}
	return count;
}

#ifdef CONFIG_EXT2_CHECK
/* Called at mount-time, super-block is locked */
void ext2_check_inodes_bitmap (struct super_block * sb)
{
	struct ext2_super_block * es = EXT2_SB(sb)->s_es;
	unsigned long desc_count = 0, bitmap_count = 0;
	struct buffer_head *bitmap_bh = NULL;
	int i;

	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		struct ext2_group_desc *desc;
		unsigned x;

		desc = ext2_get_group_desc(sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, i);
		if (!bitmap_bh)
			continue;
		
		x = ext2_count_free(bitmap_bh, EXT2_INODES_PER_GROUP(sb) / 8);
		if (le16_to_cpu(desc->bg_free_inodes_count) != x)
			ext2_error (sb, "ext2_check_inodes_bitmap",
				    "Wrong free inodes count in group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(desc->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	if (le32_to_cpu(es->s_free_inodes_count) != bitmap_count)
		ext2_error(sb, "ext2_check_inodes_bitmap",
			    "Wrong free inodes count in super block, "
			    "stored = %lu, counted = %lu",
			    (unsigned long)le32_to_cpu(es->s_free_inodes_count),
			    bitmap_count);
}
#endif
