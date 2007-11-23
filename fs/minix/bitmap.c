/*
 *  linux/fs/minix/bitmap.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Modified for 680x0 by Hamish Macdonald
 * Fixed for 680x0 by Andreas Schwab
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/quotaops.h>

#include <asm/bitops.h>

static int nibblemap[] = { 4,3,3,2,3,2,2,1,3,2,2,1,2,1,1,0 };

static unsigned long count_free(struct buffer_head *map[], unsigned numblocks, __u32 numbits)
{
	unsigned i, j, sum = 0;
	struct buffer_head *bh;
  
	for (i=0; i<numblocks-1; i++) {
		if (!(bh=map[i])) 
			return(0);
		for (j=0; j<BLOCK_SIZE; j++)
			sum += nibblemap[bh->b_data[j] & 0xf]
				+ nibblemap[(bh->b_data[j]>>4) & 0xf];
	}

	if (numblocks==0 || !(bh=map[numblocks-1]))
		return(0);
	i = ((numbits-(numblocks-1)*BLOCK_SIZE*8)/16)*2;
	for (j=0; j<i; j++) {
		sum += nibblemap[bh->b_data[j] & 0xf]
			+ nibblemap[(bh->b_data[j]>>4) & 0xf];
	}

	i = numbits%16;
	if (i!=0) {
		i = *(__u16 *)(&bh->b_data[j]) | ~((1<<i) - 1);
		sum += nibblemap[i & 0xf] + nibblemap[(i>>4) & 0xf];
		sum += nibblemap[(i>>8) & 0xf] + nibblemap[(i>>12) & 0xf];
	}
	return(sum);
}

void minix_free_block(struct inode * inode, int block)
{
	struct super_block * sb = inode->i_sb;
	struct buffer_head * bh;
	unsigned int bit,zone;

	if (!sb) {
		printk("trying to free block on nonexistent device\n");
		return;
	}
	if (block < sb->u.minix_sb.s_firstdatazone ||
	    block >= sb->u.minix_sb.s_nzones) {
		printk("trying to free block not in datazone\n");
		return;
	}
	bh = get_hash_table(sb->s_dev,block,BLOCK_SIZE);
	if (bh)
		clear_bit(BH_Dirty, &bh->b_state);
	brelse(bh);
	zone = block - sb->u.minix_sb.s_firstdatazone + 1;
	bit = zone & 8191;
	zone >>= 13;
	if (zone >= sb->u.minix_sb.s_zmap_blocks) {
		printk("minix_free_block: nonexistent bitmap buffer\n");
		return;
	}
	bh = sb->u.minix_sb.s_zmap[zone];
	if (!minix_clear_bit(bit,bh->b_data))
		printk("free_block (%s:%d): bit already cleared\n",
		       kdevname(sb->s_dev), block);
	else
		DQUOT_FREE_BLOCK(sb, inode, 1);
	mark_buffer_dirty(bh, 1);
	return;
}

int minix_new_block(struct inode * inode)
{
	struct super_block * sb = inode->i_sb;
	struct buffer_head * bh;
	int i,j;

	if (!sb) {
		printk("trying to get new block from nonexistent device\n");
		return 0;
	}
repeat:
	if(DQUOT_ALLOC_BLOCK(sb, inode, 1))
		return -EDQUOT;

	j = 8192;
	bh = NULL;
	for (i = 0; i < sb->u.minix_sb.s_zmap_blocks; i++) {
		bh = sb->u.minix_sb.s_zmap[i];
		if ((j = minix_find_first_zero_bit(bh->b_data, 8192)) < 8192)
			break;
	}
	if (!bh || j >= 8192)
		return 0;
	if (minix_set_bit(j,bh->b_data)) {
		printk("new_block: bit already set");
		DQUOT_FREE_BLOCK(sb, inode, 1);
		goto repeat;
	}
	mark_buffer_dirty(bh, 1);
	j += i*8192 + sb->u.minix_sb.s_firstdatazone-1;
	if (j < sb->u.minix_sb.s_firstdatazone ||
	    j >= sb->u.minix_sb.s_nzones)
		return 0;
	return j;
}

unsigned long minix_count_free_blocks(struct super_block *sb)
{
	return (count_free(sb->u.minix_sb.s_zmap, sb->u.minix_sb.s_zmap_blocks,
		sb->u.minix_sb.s_nzones - sb->u.minix_sb.s_firstdatazone + 1)
		<< sb->u.minix_sb.s_log_zone_size);
}

static struct buffer_head *V1_minix_clear_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct minix_inode *raw_inode;
	int ino, block;

	ino = inode->i_ino;
	if (!ino || ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s: %d is out of range\n",
		       kdevname(inode->i_dev), ino);
		return NULL;
	}
	block = (2 + inode->i_sb->u.minix_sb.s_imap_blocks +
		 inode->i_sb->u.minix_sb.s_zmap_blocks +
		 (ino - 1) / MINIX_INODES_PER_BLOCK);
	bh = bread(inode->i_dev, block, BLOCK_SIZE);
	if (!bh) {
		printk("unable to read i-node block\n");
		return NULL;
	}
	raw_inode = ((struct minix_inode *)bh->b_data +
		     (ino - 1) % MINIX_INODES_PER_BLOCK);
	raw_inode->i_nlinks = 0;
	raw_inode->i_mode = 0;
	mark_buffer_dirty(bh, 1);
	return bh;
}

static struct buffer_head *V2_minix_clear_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct minix2_inode *raw_inode;
	int ino, block;

	ino = inode->i_ino;
	if (!ino || ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s: %d is out of range\n",
		       kdevname(inode->i_dev), ino);
		return NULL;
	}
	block = (2 + inode->i_sb->u.minix_sb.s_imap_blocks +
		 inode->i_sb->u.minix_sb.s_zmap_blocks +
		 (ino - 1) / MINIX2_INODES_PER_BLOCK);
	bh = bread(inode->i_dev, block, BLOCK_SIZE);
	if (!bh) {
		printk("unable to read i-node block\n");
		return NULL;
	}
	raw_inode = ((struct minix2_inode *) bh->b_data +
		     (ino - 1) % MINIX2_INODES_PER_BLOCK);
	raw_inode->i_nlinks = 0;
	raw_inode->i_mode = 0;
	mark_buffer_dirty(bh, 1);
	return bh;
}

/* Clear the link count and mode of a deleted inode on disk. */

static void minix_clear_inode(struct inode *inode)
{
	struct buffer_head *bh;
	if (INODE_VERSION(inode) == MINIX_V1)
		bh = V1_minix_clear_inode(inode);
	else
		bh = V2_minix_clear_inode(inode);
	brelse (bh);
}

void minix_free_inode(struct inode * inode)
{
	struct buffer_head * bh;
	unsigned long ino;

	if (!inode)
		return;
	if (!inode->i_dev) {
		printk("free_inode: inode has no device\n");
		return;
	}
	if (inode->i_count > 1) {
		printk("free_inode: inode has count=%d\n",inode->i_count);
		return;
	}
	if (inode->i_nlink) {
		printk("free_inode: inode has nlink=%d\n",inode->i_nlink);
		return;
	}
	if (!inode->i_sb) {
		printk("free_inode: inode on nonexistent device\n");
		return;
	}
	if (inode->i_ino < 1 || inode->i_ino > inode->i_sb->u.minix_sb.s_ninodes) {
		printk("free_inode: inode 0 or nonexistent inode\n");
		return;
	}
	ino = inode->i_ino;
	if ((ino >> 13) >= inode->i_sb->u.minix_sb.s_imap_blocks) {
		printk("free_inode: nonexistent imap in superblock\n");
		return;
	}

	DQUOT_FREE_INODE(inode->i_sb, inode);
	DQUOT_DROP(inode);

	bh = inode->i_sb->u.minix_sb.s_imap[ino >> 13];
	minix_clear_inode(inode);
	clear_inode(inode);
	if (!minix_clear_bit(ino & 8191, bh->b_data))
		printk("free_inode: bit %lu already cleared.\n",ino);
	mark_buffer_dirty(bh, 1);
}

struct inode * minix_new_inode(const struct inode * dir, int * error)
{
	struct super_block * sb;
	struct inode * inode;
	struct buffer_head * bh;
	int i,j;

	inode = get_empty_inode();
	if (!inode)
		return NULL;
	sb = dir->i_sb;
	inode->i_sb = sb;
	inode->i_flags = 0;
	j = 8192;
	bh = NULL;
	lock_super(sb);
	for (i = 0; i < sb->u.minix_sb.s_imap_blocks; i++) {
		bh = inode->i_sb->u.minix_sb.s_imap[i];
		if ((j = minix_find_first_zero_bit(bh->b_data, 8192)) < 8192)
			break;
	}
	if (!bh || j >= 8192) {
		iput(inode);
		unlock_super(sb);
		return NULL;
	}
	if (minix_set_bit(j,bh->b_data)) {	/* shouldn't happen */
		printk("new_inode: bit already set");
		iput(inode);
		unlock_super(sb);
		return NULL;
	}
	mark_buffer_dirty(bh, 1);
	j += i*8192;
	if (!j || j > inode->i_sb->u.minix_sb.s_ninodes) {
		iput(inode);
		unlock_super(sb);
		return NULL;
	}
	inode->i_nlink = 1;
	inode->i_dev = sb->s_dev;
	inode->i_uid = current->fsuid;
	inode->i_gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
	inode->i_ino = j;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = inode->i_blksize = 0;
	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	unlock_super(sb);
printk("m_n_i: allocated inode ");
	if(DQUOT_ALLOC_INODE(sb, inode)) {
printk("fails quota test\n");
		sb->dq_op->drop(inode);
		inode->i_nlink = 0;
		iput(inode);
		*error = -EDQUOT;
		return NULL;
	}
printk("is within quota\n");

	*error = 0;
	return inode;
}

unsigned long minix_count_free_inodes(struct super_block *sb)
{
	return count_free(sb->u.minix_sb.s_imap, sb->u.minix_sb.s_imap_blocks,
		sb->u.minix_sb.s_ninodes + 1);
}
