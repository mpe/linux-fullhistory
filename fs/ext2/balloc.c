/*
 *  linux/fs/ext2/balloc.c
 *
 *  Copyright (C) 1992, 1993  Remy Card (card@masi.ibp.fr)
 *
 */

/* balloc.c contains the blocks allocation and deallocation routines */

/*

   The free blocks are managed by bitmaps.  A file system contains several
   blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
   block for inodes, N blocks for the inode table and data blocks.

   The file system contains group descriptors which are located after the
   super block.  Each descriptor contains the number of the bitmap block and
   the free blocks count in the block.  The descriptors are loaded in memory
   when a file system is mounted (see ext2_read_super).

*/

#include <linux/sched.h>
#include <linux/ext2_fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/locks.h>

#define clear_block(addr,size) \
	__asm__("cld\n\t" \
		"rep\n\t" \
		"stosl" \
		: \
		:"a" (0), "c" (size/4), "D" ((long) (addr)) \
		:"cx", "di")

#define set_bit(nr,addr) ( \
{ \
	char res; \
	__asm__ __volatile__("btsl %1,%2\n\tsetb %0" \
			     :"=q" (res) \
			     :"r" (nr),"m" (*(addr))); \
	res; \
} \
)

#define clear_bit(nr,addr) ( \
{ \
	char res; \
	__asm__ __volatile__("btrl %1,%2\n\tsetnb %0" \
			     :"=q" (res) \
			     :"r" (nr),"m" (*(addr))); \
	res; \
} \
)

#define find_first_zero(addr,size) ( \
{ \
	int __res; \
	__asm__("cld\n" \
		"1:\tlodsl\n\t" \
		"notl %%eax\n\t" \
		"bsfl %%eax,%%edx\n\t" \
		"jne 2f\n\t" \
		"addl $32,%%ecx\n\t" \
		"cmpl %%ebx,%%ecx\n\t" \
		"jl 1b\n\t" \
		"xorl %%edx,%%edx\n" \
		"2:\taddl %%edx,%%ecx" \
		:"=c" (__res):"0" (0), "S" (addr), "b" (size) \
		:"ax", "bx", "dx", "si"); \
	__res; \
} \
)

static void read_block_bitmap (struct super_block * sb,
			       unsigned int block_group,
			       unsigned long bitmap_nr)
{
	unsigned long group_desc;
	unsigned long desc;
	struct ext2_group_desc * gdp;
	struct buffer_head * bh;
	
	group_desc = block_group / EXT2_DESC_PER_BLOCK(sb);
	desc = block_group % EXT2_DESC_PER_BLOCK(sb);
	if (!sb->u.ext2_sb.s_group_desc[group_desc]) {
		printk ("block_group = %d,group_desc = %d,desc = %d\n",
			 block_group, group_desc, desc);
		panic ("read_block_bitmap: Group descriptor not loaded");
	}
	gdp = (struct ext2_group_desc *) sb->u.ext2_sb.s_group_desc[group_desc]->b_data;
	bh = bread (sb->s_dev, gdp[desc].bg_block_bitmap, sb->s_blocksize);
	if (!bh) {
		printk ("block_group = %d,group_desc = %d,desc = %d,block_bitmap = %d\n",
			block_group, group_desc, desc, gdp[desc].bg_block_bitmap);
		panic ("read_block_bitmap: Cannot read block bitmap");
	}
	sb->u.ext2_sb.s_block_bitmap_number[bitmap_nr] = block_group;
	sb->u.ext2_sb.s_block_bitmap[bitmap_nr] = bh;
}

/*
 * load_block_bitmap loads the block bitmap for a blocks group
 *
 * It maintains a cache for the last bitmaps loaded.  This cache is managed
 * with a LRU algorithm.
 *
 * Notes:
 * 1/ There is one cache per mounted file system.
 * 2/ If the file system contains less than EXT2_MAX_GROUP_LOADED groups,
 *    this function reads the bitmap without maintaining a LRU cache.
 */
static int load_block_bitmap (struct super_block * sb,
			      unsigned int block_group)
{
	int i, j;
	unsigned long block_bitmap_number;
	struct buffer_head * block_bitmap;

	if (block_group >= sb->u.ext2_sb.s_groups_count) {
		printk ("block_group = %d, groups_count = %d\n",
			block_group, sb->u.ext2_sb.s_groups_count);
		panic ("load_block_bitmap: block_group >= groups_count");
	}
	if (sb->u.ext2_sb.s_loaded_block_bitmaps > 0 &&
	    sb->u.ext2_sb.s_block_bitmap_number[0] == block_group)
		return 0;

	if (sb->u.ext2_sb.s_groups_count <= EXT2_MAX_GROUP_LOADED) {
		if (sb->u.ext2_sb.s_block_bitmap[block_group]) {
			if (sb->u.ext2_sb.s_block_bitmap_number[block_group] != block_group)
				panic ("load_block_bitmap: block_group != block_bitmap_number");
			else
				return block_group;
		} else {
			read_block_bitmap (sb, block_group, block_group);
			return block_group;
		}
	}

	for (i = 0; i < sb->u.ext2_sb.s_loaded_block_bitmaps &&
		    sb->u.ext2_sb.s_block_bitmap_number[i] != block_group; i++)
		;
	if (i < sb->u.ext2_sb.s_loaded_block_bitmaps &&
  	    sb->u.ext2_sb.s_block_bitmap_number[i] == block_group) {
		block_bitmap_number = sb->u.ext2_sb.s_block_bitmap_number[i];
		block_bitmap = sb->u.ext2_sb.s_block_bitmap[i];
		for (j = i; j > 0; j--) {
			sb->u.ext2_sb.s_block_bitmap_number[j] =
				sb->u.ext2_sb.s_block_bitmap_number[j - 1];
			sb->u.ext2_sb.s_block_bitmap[j] =
				sb->u.ext2_sb.s_block_bitmap[j - 1];
		}
		sb->u.ext2_sb.s_block_bitmap_number[0] = block_bitmap_number;
		sb->u.ext2_sb.s_block_bitmap[0] = block_bitmap;
	} else {
		if (sb->u.ext2_sb.s_loaded_block_bitmaps < EXT2_MAX_GROUP_LOADED)
			sb->u.ext2_sb.s_loaded_block_bitmaps++;
		else
			brelse (sb->u.ext2_sb.s_block_bitmap[EXT2_MAX_GROUP_LOADED - 1]);
		for (j = sb->u.ext2_sb.s_loaded_block_bitmaps - 1; j > 0;  j--) {
			sb->u.ext2_sb.s_block_bitmap_number[j] =
				sb->u.ext2_sb.s_block_bitmap_number[j - 1];
			sb->u.ext2_sb.s_block_bitmap[j] =
				sb->u.ext2_sb.s_block_bitmap[j - 1];
		}
		read_block_bitmap (sb, block_group, 0);
	}
	return 0;
}

void ext2_free_block (struct super_block * sb, unsigned long block)
{
	struct buffer_head * bh;
	struct buffer_head * bh2;
	unsigned long block_group;
	unsigned long bit;
	unsigned long group_desc;
	unsigned long desc;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	struct ext2_super_block * es;

	if (!sb) {
		printk ("ext2_free_block: nonexistant device");
		return;
	}
	lock_super (sb);
	if (block < sb->u.ext2_sb.s_first_data_block ||
	    block >= sb->u.ext2_sb.s_blocks_count) {
		printk ("ext2_free_block: block not in datazone\n");
		unlock_super (sb);
		return;
	}
	es = (struct ext2_super_block *) sb->u.ext2_sb.s_sbh->b_data;
#ifdef EXT2FS_DEBUG
	printk ("ext2_free_block: freeing block %d\n", block);
#endif
	bh = get_hash_table (sb->s_dev, block, sb->s_blocksize);
	if (bh)
		bh->b_dirt = 0;
	brelse (bh);
	block_group = (block - sb->u.ext2_sb.s_first_data_block) /
		      EXT2_BLOCKS_PER_GROUP(sb);
	bit = (block - sb->u.ext2_sb.s_first_data_block) %
		EXT2_BLOCKS_PER_GROUP(sb);
	bitmap_nr = load_block_bitmap (sb, block_group);
	bh = sb->u.ext2_sb.s_block_bitmap[bitmap_nr];
	if (!bh) {
		printk ("block_group = %d\n", block_group);
		panic ("ext2_free_block: Unable to load group bitmap");
	}
	if (clear_bit (bit, bh->b_data))
		printk ("ext2_free_block (%04x:%d): bit already cleared\n",
			sb->s_dev, block);
	else {
		group_desc = block_group / EXT2_DESC_PER_BLOCK(sb);
		desc = block_group % EXT2_DESC_PER_BLOCK(sb);
		bh2 = sb->u.ext2_sb.s_group_desc[group_desc];
		if (!bh2) {
			printk ("group_desc = %d\n", group_desc);
			panic ("ext2_free_block: Group descriptor not loaded");
		}
		gdp = (struct ext2_group_desc *) bh2->b_data;
		gdp[desc].bg_free_blocks_count ++;
		bh2->b_dirt = 1;
	}
	bh->b_dirt = 1;
	es->s_free_blocks_count ++;
	sb->u.ext2_sb.s_sbh->b_dirt = 1;
	sb->s_dirt = 1;
	unlock_super (sb);
	return;
}

/*
 * ext2_new_block does not use a very clever allocation algorithm yet
 *
 * Currently, the group descriptors are scanned until a free block is found
 */
int ext2_new_block (struct super_block * sb, unsigned long block_group)
{
	struct buffer_head * bh;
	int i, j;
	unsigned long group_desc;
	unsigned long desc;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	struct ext2_super_block * es;

	if (!sb) {
		printk ("ext2_new_block: nonexistant device");
		return 0;
	}
	lock_super (sb);
	es = (struct ext2_super_block *) sb->u.ext2_sb.s_sbh->b_data;
	if (es->s_free_blocks_count <= es->s_r_blocks_count && !suser()) {
		unlock_super (sb);
		return 0;
	}
	
repeat:
	group_desc = 0;
	desc = 0;
	gdp = NULL;
	for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++) {
		if (!gdp) {
			if (!sb->u.ext2_sb.s_group_desc[group_desc])
				panic ("ext2_new_block: Descriptor not loaded");
			gdp = (struct ext2_group_desc *) sb->u.ext2_sb.s_group_desc[group_desc]->b_data;
		}
		if (gdp[desc].bg_free_blocks_count > 0)
			break;
		desc ++;
		if (desc == EXT2_DESC_PER_BLOCK(sb)) {
			group_desc ++;
			desc = 0;
			gdp = NULL;
		}
	}
	if (i >= sb->u.ext2_sb.s_groups_count) {
		unlock_super (sb);
		return 0;
	}
#ifdef EXT2FS_DEBUG
	printk ("ext2_new_block: using block group %d(%d,%d,%d)\n", 
		i, group_desc, desc, gdp[desc].bg_free_blocks_count);
#endif
	bitmap_nr = load_block_bitmap (sb, i);
	bh = sb->u.ext2_sb.s_block_bitmap[bitmap_nr];
	if (!bh) {
		printk ("block_group = %d\n", i);
		panic ("ext2_new_block: Unable to load group bitmap");
	}
	if ((j = find_first_zero (bh->b_data, EXT2_BLOCKS_PER_GROUP(sb))) <
	    EXT2_BLOCKS_PER_GROUP(sb)) {
		if (set_bit (j, bh->b_data)) {
			printk ("ext2_new_block: bit already set\n");
			goto repeat;
		}
		bh->b_dirt = 1;
	} else
		goto repeat;
#ifdef EXT2FS_DEBUG
	printk ("ext2_new_block: found bit %d\n", j);
#endif
	j += i * EXT2_BLOCKS_PER_GROUP(sb) +
		sb->u.ext2_sb.s_first_data_block;
	if (j >= sb->u.ext2_sb.s_blocks_count) {
		printk ("block_group = %d,block=%d\n", i, j);
		printk ("ext2_new_block: block >= blocks count");
		return 0;
	}
	if (!(bh = getblk (sb->s_dev, j, sb->s_blocksize))) {
		printk ("ext2_new_block: cannot get block");
		unlock_super (sb);
		return 0;
	}
	clear_block (bh->b_data, sb->s_blocksize);
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	brelse (bh);
#ifdef EXT2FS_DEBUG
	printk("ext2_new_block: allocating block %d\n", j);
#endif
	gdp[desc].bg_free_blocks_count --;
	sb->u.ext2_sb.s_group_desc[group_desc]->b_dirt = 1;
	es->s_free_blocks_count --;
	sb->u.ext2_sb.s_sbh->b_dirt = 1;
	sb->s_dirt = 1;
	unlock_super (sb);
	return j;
}

unsigned long ext2_count_free_blocks (struct super_block *sb)
{
	struct ext2_super_block * es;
#ifdef EXT2FS_DEBUG
	unsigned long desc_count, bitmap_count, x;
	unsigned long group_desc;
	unsigned long desc;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	int i;

	lock_super (sb);
	es = (struct ext2_super_block *) sb->u.ext2_sb.s_sbh->b_data;
	desc_count = 0;
	bitmap_count = 0;
	group_desc = 0;
	desc = 0;
	gdp = NULL;
	for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++) {
		if (!gdp) {
			if (!sb->u.ext2_sb.s_group_desc[group_desc]) {
				printk ("ext2_count_free_block: Descriptor not loaded\n");
				break;
			}
			gdp = (struct ext2_group_desc *) sb->u.ext2_sb.s_group_desc[group_desc]->b_data;
		}
		desc_count += gdp[desc].bg_free_blocks_count;
		bitmap_nr = load_block_bitmap (sb, i);
		if (sb->u.ext2_sb.s_block_bitmap[bitmap_nr])
			x = ext2_count_free (sb->u.ext2_sb.s_block_bitmap[bitmap_nr],
					       sb->s_blocksize);
		else {
			x = 0;
			printk ("Cannot load bitmap for group %d\n", i);
		}
		printk ("group %d: stored = %d, counted = %d\n",
			i, gdp[desc].bg_free_blocks_count, x);
		bitmap_count += x;
		desc ++;
		if (desc == EXT2_DESC_PER_BLOCK(sb)) {
			group_desc ++;
			desc = 0;
			gdp = NULL;
		}
	}
	printk("ext2_count_free_blocks: stored = %d, computed = %d, %d\n",
		es->s_free_blocks_count, desc_count, bitmap_count);
	unlock_super (sb);
	return bitmap_count;
#else
	es = (struct ext2_super_block *) sb->u.ext2_sb.s_sbh->b_data;
	return es->s_free_blocks_count;
#endif
}
