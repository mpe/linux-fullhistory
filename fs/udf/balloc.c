/*
 * balloc.c
 *
 * PURPOSE
 *	Block allocation handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  02/24/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/udf_fs.h>

#include <asm/bitops.h>

#include "udf_i.h"
#include "udf_sb.h"

static int read_block_bitmap(struct super_block * sb, unsigned int block,
	unsigned long bitmap_nr)
{
	struct buffer_head *bh = NULL;
	int retval = 0;
	lb_addr loc;

	loc.logicalBlockNum = UDF_SB_PARTMAPS(sb)[UDF_SB_PARTITION(sb)].s_uspace_bitmap;
	loc.partitionReferenceNum = UDF_SB_PARTITION(sb);

	bh = udf_tread(sb, udf_get_lb_pblock(sb, loc, block), sb->s_blocksize);
	if (!bh)
	{
		retval = -EIO;
	}
	UDF_SB_BLOCK_BITMAP_NUMBER(sb, bitmap_nr) = block;
	UDF_SB_BLOCK_BITMAP(sb, bitmap_nr) = bh;
	return retval;
}

static int load__block_bitmap(struct super_block * sb, unsigned int block_group)
{
	int i, j, retval = 0;
	unsigned long block_bitmap_number;
	struct buffer_head * block_bitmap = NULL;
	int nr_groups = (UDF_SB_PARTLEN(sb, UDF_SB_PARTITION(sb)) +
		(sizeof(struct SpaceBitmapDesc) << 3) + (sb->s_blocksize * 8) - 1) / (sb->s_blocksize * 8);

	if (block_group >= nr_groups)
	{
		udf_debug("block_group (%d) > nr_groups (%d)\n", block_group, nr_groups);
	}

	if (nr_groups <= UDF_MAX_BLOCK_LOADED)
	{
		if (UDF_SB_BLOCK_BITMAP(sb, block_group))
		{
			if (UDF_SB_BLOCK_BITMAP_NUMBER(sb, block_group) == block_group)
				return block_group;
		}
		retval = read_block_bitmap(sb, block_group, block_group);
		if (retval < 0)
			return retval;
		return block_group;
	}

	for (i=0; i<UDF_SB_LOADED_BLOCK_BITMAPS(sb) &&
		UDF_SB_BLOCK_BITMAP_NUMBER(sb, i) != block_group; i++)
	{
		;
	}
	if (i < UDF_SB_LOADED_BLOCK_BITMAPS(sb) &&
		UDF_SB_BLOCK_BITMAP_NUMBER(sb, i) == block_group)
	{
		block_bitmap_number = UDF_SB_BLOCK_BITMAP_NUMBER(sb, i);
		block_bitmap = UDF_SB_BLOCK_BITMAP(sb, i);
		for (j=i; j>0; j--)
		{
			UDF_SB_BLOCK_BITMAP_NUMBER(sb, j) = UDF_SB_BLOCK_BITMAP_NUMBER(sb, j-1);
			UDF_SB_BLOCK_BITMAP(sb, j) = UDF_SB_BLOCK_BITMAP(sb, j-1);
		}
		UDF_SB_BLOCK_BITMAP_NUMBER(sb, 0) = block_bitmap_number;
		UDF_SB_BLOCK_BITMAP(sb, 0) = block_bitmap;

		if (!block_bitmap)
			retval = read_block_bitmap(sb, block_group, 0);
	}
	else
	{
		if (UDF_SB_LOADED_BLOCK_BITMAPS(sb) < UDF_MAX_BLOCK_LOADED)
			UDF_SB_LOADED_BLOCK_BITMAPS(sb) ++;
		else
			brelse(UDF_SB_BLOCK_BITMAP(sb, UDF_MAX_BLOCK_LOADED-1));
		for (j=UDF_SB_LOADED_BLOCK_BITMAPS(sb)-1; j>0; j--)
		{
			UDF_SB_BLOCK_BITMAP_NUMBER(sb, j) = UDF_SB_BLOCK_BITMAP_NUMBER(sb, j-1);
			UDF_SB_BLOCK_BITMAP(sb, j) = UDF_SB_BLOCK_BITMAP(sb, j-1);
		}
		retval = read_block_bitmap(sb, block_group, 0);
	}
	return retval;
}

static inline int load_block_bitmap(struct super_block *sb,
	unsigned int block_group)
{
	int slot;
    int nr_groups = (UDF_SB_PARTLEN(sb, UDF_SB_PARTITION(sb)) +
        (sizeof(struct SpaceBitmapDesc) << 3) + (sb->s_blocksize * 8) - 1) / (sb->s_blocksize * 8);

	if (UDF_SB_LOADED_BLOCK_BITMAPS(sb) > 0 &&
		UDF_SB_BLOCK_BITMAP_NUMBER(sb, 0) == block_group &&
		UDF_SB_BLOCK_BITMAP(sb, block_group))
	{
		return 0;
	}
	else if (nr_groups <= UDF_MAX_BLOCK_LOADED &&
		UDF_SB_BLOCK_BITMAP_NUMBER(sb, block_group) == block_group &&
		UDF_SB_BLOCK_BITMAP(sb, block_group))
	{
		slot = block_group;
	}
	else
	{
		slot = load__block_bitmap(sb, block_group);
	}

	if (slot < 0)
		return slot;

	if (!UDF_SB_BLOCK_BITMAP(sb, slot))
		return -EIO;

	return slot;
}

void udf_free_blocks(const struct inode * inode, lb_addr bloc, Uint32 offset,
	Uint32 count)
{
	struct buffer_head * bh = NULL;
	unsigned long block;
	unsigned long block_group;
	unsigned long bit;
	unsigned long i;
	int bitmap_nr;
	unsigned long overflow;
	struct super_block * sb;

	sb = inode->i_sb;
	if (!sb)
	{
		udf_debug("nonexistent device");
		return;
	}

	if (UDF_SB_PARTMAPS(sb)[bloc.partitionReferenceNum].s_uspace_bitmap == 0xFFFFFFFF)
		return;

	lock_super(sb);
	if (bloc.logicalBlockNum < 0 ||
		(bloc.logicalBlockNum + count) > UDF_SB_PARTLEN(sb, bloc.partitionReferenceNum))
	{
		udf_debug("%d < %d || %d + %d > %d\n",
			bloc.logicalBlockNum, 0, bloc.logicalBlockNum, count,
			UDF_SB_PARTLEN(sb, bloc.partitionReferenceNum));
		goto error_return;
	}

	block = bloc.logicalBlockNum + offset + (sizeof(struct SpaceBitmapDesc) << 3);

do_more:
	overflow = 0;
	block_group = block >> (sb->s_blocksize_bits + 3);
	bit = block % (sb->s_blocksize << 3);

	/*
	 * Check to see if we are freeing blocks across a group boundary.
	 */
	if (bit + count > (sb->s_blocksize << 3))
	{
		overflow = bit + count - (sb->s_blocksize << 3);
		count -= overflow;
	}
	bitmap_nr = load_block_bitmap(sb, block_group);
	if (bitmap_nr < 0)
		goto error_return;

	bh = UDF_SB_BLOCK_BITMAP(sb, bitmap_nr);
	for (i=0; i < count; i++)
	{
		if (udf_set_bit(bit + i, bh->b_data))
		{
			udf_debug("bit %ld already set\n", bit + i);
			udf_debug("byte=%2x\n", ((char *)bh->b_data)[(bit + i) >> 3]);
		}
		else if (UDF_SB_LVIDBH(sb))
		{
			UDF_SB_LVID(sb)->freeSpaceTable[UDF_SB_PARTITION(sb)] =
				cpu_to_le32(le32_to_cpu(UDF_SB_LVID(sb)->freeSpaceTable[UDF_SB_PARTITION(sb)])+1);
		}
	}
	mark_buffer_dirty(bh, 1);
	if (overflow)
	{
		block += count;
		count = overflow;
		goto do_more;
	}
error_return:
	sb->s_dirt = 1;
	if (UDF_SB_LVIDBH(sb))
		mark_buffer_dirty(UDF_SB_LVIDBH(sb), 1);
	unlock_super(sb);
	return;
}

int udf_alloc_blocks(const struct inode * inode, Uint16 partition,
	Uint32 first_block, Uint32 block_count)
{
	int alloc_count = 0;
	int bit, block, block_group, group_start;
	int nr_groups, bitmap_nr;
	struct buffer_head *bh;
	struct super_block *sb;

	sb = inode->i_sb;
	if (!sb)
	{
		udf_debug("nonexistent device\n");
		return 0;
	}
	lock_super(sb);

	if (first_block < 0 || first_block >= UDF_SB_PARTLEN(sb, partition))
		goto out;

repeat:
	nr_groups = (UDF_SB_PARTLEN(sb, partition) +
		(sizeof(struct SpaceBitmapDesc) << 3) + (sb->s_blocksize * 8) - 1) / (sb->s_blocksize * 8);
	block = first_block + (sizeof(struct SpaceBitmapDesc) << 3);
	block_group = block >> (sb->s_blocksize_bits + 3);
	group_start = block_group ? 0 : sizeof(struct SpaceBitmapDesc);

	bitmap_nr = load_block_bitmap(sb, block_group);
	if (bitmap_nr < 0)
		goto out;
	bh = UDF_SB_BLOCK_BITMAP(sb, bitmap_nr);

	bit = block % (sb->s_blocksize << 3);

	while (bit < (sb->s_blocksize << 3) && block_count > 0)
	{
		if (!udf_test_bit(bit, bh->b_data))
			goto out;
		if (!udf_clear_bit(bit, bh->b_data))
		{
			udf_debug("bit already cleared for block %d\n", bit);
			goto out;
		}
		block_count --;
		alloc_count ++;
		bit ++;
		block ++;

	}
	mark_buffer_dirty(bh, 1);
	if (block_count > 0)
		goto repeat;
out:
	if (UDF_SB_LVIDBH(sb))
	{
		UDF_SB_LVID(sb)->freeSpaceTable[partition] =
			cpu_to_le32(le32_to_cpu(UDF_SB_LVID(sb)->freeSpaceTable[partition])-alloc_count);
		mark_buffer_dirty(UDF_SB_LVIDBH(sb), 1);
	}
	sb->s_dirt = 1;
	unlock_super(sb);
	return alloc_count;
}

int udf_new_block(const struct inode * inode, Uint16 partition, Uint32 goal, int *err)
{
	int tmp, newbit, bit=0, block, block_group, group_start;
	int end_goal, nr_groups, bitmap_nr, i;
	struct buffer_head *bh = NULL;
	struct super_block *sb;
	char *ptr;
	int newblock = 0;

	*err = -ENOSPC;
	sb = inode->i_sb;
	if (!sb)
	{
		udf_debug("nonexistent device\n");
		return newblock;
	}
	lock_super(sb);

repeat:
	if (goal < 0 || goal >= UDF_SB_PARTLEN(sb, partition))
		goal = 0;

	nr_groups = (UDF_SB_PARTLEN(sb, partition) +
		(sizeof(struct SpaceBitmapDesc) << 3) + (sb->s_blocksize * 8) - 1) / (sb->s_blocksize * 8);
	block = goal + (sizeof(struct SpaceBitmapDesc) << 3);
	block_group = block >> (sb->s_blocksize_bits + 3);
	group_start = block_group ? 0 : sizeof(struct SpaceBitmapDesc);

	bitmap_nr = load_block_bitmap(sb, block_group);
	if (bitmap_nr < 0)
		goto error_return;
	bh = UDF_SB_BLOCK_BITMAP(sb, bitmap_nr);
	ptr = memscan((char *)bh->b_data + group_start, 0xFF, sb->s_blocksize - group_start);

	if ((ptr - ((char *)bh->b_data)) < sb->s_blocksize)
	{
		bit = block % (sb->s_blocksize << 3);

		if (udf_test_bit(bit, bh->b_data))
		{
			goto got_block;
		}
		end_goal = (bit + 63) & ~63;
		bit = udf_find_next_one_bit(bh->b_data, end_goal, bit);
		if (bit < end_goal)
			goto got_block;
		ptr = memscan((char *)bh->b_data + (bit >> 3), 0xFF, sb->s_blocksize - ((bit + 7) >> 3));
		newbit = (ptr - ((char *)bh->b_data)) << 3;
		if (newbit < sb->s_blocksize << 3)
		{
			bit = newbit;
			goto search_back;
		}
		newbit = udf_find_next_one_bit(bh->b_data, sb->s_blocksize << 3, bit);
		if (newbit < sb->s_blocksize << 3)
		{
			bit = newbit;
			goto got_block;
		}
	}

	for (i=0; i<(nr_groups*2); i++)
	{
		block_group ++;
		if (block_group >= nr_groups)
			block_group = 0;
		group_start = block_group ? 0 : sizeof(struct SpaceBitmapDesc);

		bitmap_nr = load_block_bitmap(sb, block_group);
		if (bitmap_nr < 0)
			goto error_return;
		bh = UDF_SB_BLOCK_BITMAP(sb, bitmap_nr);
		if (i < nr_groups)
		{
			ptr = memscan((char *)bh->b_data + group_start, 0xFF, sb->s_blocksize - group_start);
			if ((ptr - ((char *)bh->b_data)) < sb->s_blocksize)
			{
				bit = (ptr - ((char *)bh->b_data)) << 3;
				break;
			}
		}
		else
		{
			bit = udf_find_next_one_bit((char *)bh->b_data, sb->s_blocksize << 3, group_start << 3);
			if (bit < sb->s_blocksize << 3)
				break;
		}
	}
	if (i >= (nr_groups*2))
	{
		unlock_super(sb);
		return newblock;
	}
	if (bit < sb->s_blocksize << 3)
		goto search_back;
	else
		bit = udf_find_next_one_bit(bh->b_data, sb->s_blocksize << 3, group_start << 3);
	if (bit >= sb->s_blocksize << 3)
	{
		unlock_super(sb);
		return 0;
	}

search_back:
	for (i=0; i<7 && bit > (group_start << 3) && udf_test_bit(bit - 1, bh->b_data); i++, bit--);

got_block:
	newblock = bit + (block_group << (sb->s_blocksize_bits + 3)) -
		(group_start << 3);

	tmp = udf_get_pblock(sb, newblock, partition, 0);
	if (!udf_clear_bit(bit, bh->b_data))
	{
		udf_debug("bit already cleared for block %d\n", bit);
		goto repeat;
	}

	mark_buffer_dirty(bh, 1);
	if (!(bh = getblk(sb->s_dev, tmp, sb->s_blocksize)))
	{
		udf_debug("cannot get block %d\n", tmp);
		unlock_super(sb);
		return 0;
	}
	memset(bh->b_data, 0, sb->s_blocksize);
	mark_buffer_uptodate(bh, 1);
	mark_buffer_dirty(bh, 1);
	udf_release_data(bh);

	if (UDF_SB_LVIDBH(sb))
	{
		UDF_SB_LVID(sb)->freeSpaceTable[partition] =
			cpu_to_le32(le32_to_cpu(UDF_SB_LVID(sb)->freeSpaceTable[partition])-1);
		mark_buffer_dirty(UDF_SB_LVIDBH(sb), 1);
	}
	sb->s_dirt = 1;
	unlock_super(sb);
	*err = 0;
	return newblock;

error_return:
	*err = -EIO;
	unlock_super(sb);
	return 0;
}
