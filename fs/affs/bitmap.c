/*
 *  linux/fs/affs/bitmap.c
 *
 *  (c) 1996 Hans-Joachim Widmaier
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */

#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/amigaffs.h>
#include <linux/locks.h>

#include <asm/bitops.h>

static int nibblemap[] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };

int
affs_count_free_bits(int blocksize, const UBYTE *data)
{
  int	 free;
  int	 i;

  free = 0;
  for (i = 0; i < blocksize; i++) {
    free  += nibblemap[data[i] & 0xF] + nibblemap[(data[i]>>4) & 0xF];
  }

  return free;
}

int
affs_count_free_blocks(struct super_block *s)
{
	int	 free;
	int	 i;

	pr_debug("AFFS: count_free_blocks()\n");

	free = 0;
	if (s->u.affs_sb.s_flags & SF_BM_VALID) {
		for (i = 0; i < s->u.affs_sb.s_bm_count; i++) {
			free += s->u.affs_sb.s_bitmap[i].bm_free;
		}
	}
	return free;
}

void
affs_free_block(struct super_block *sb, LONG block)
{
	int			 bmap;
	int			 bit;
	ULONG			 blk;
	struct affs_bm_info	*bm;

	pr_debug("AFFS: free_block(%d)\n",block);

	blk    = block - sb->u.affs_sb.s_reserved;
	bmap   = blk / (sb->s_blocksize * 8 - 32);
	bit    = blk % (sb->s_blocksize * 8 - 32);
	bm     = &sb->u.affs_sb.s_bitmap[bmap];
	if (bmap >= sb->u.affs_sb.s_bm_count || bit >= bm->bm_size) {
		printk("AFFS: free_block(): block %d outside partition.\n",block);
		return;
	}
	blk  = 0;
	set_bit(bit & 31,&blk);

	lock_super(sb);
	if (set_bit(bit ^ BO_EXBITS,bm->bm_bh->b_data + 4))
		printk("AFFS: free_block(): block %d is already free.\n",block);
	else {
		bm->bm_free++;
		((ULONG *)bm->bm_bh->b_data)[0] = ntohl(htonl(((ULONG *)bm->bm_bh->b_data)[0]) - blk);
		mark_buffer_dirty(bm->bm_bh,1);
		sb->s_dirt = 1;
	}
	unlock_super(sb);
}

static ULONG
affs_balloc(struct inode *inode, int zone_no)
{
	ULONG			 w;
	ULONG			*bm;
	int			 fb;
	int			 i;
	int			 fwb;
	ULONG			 block;
	struct affs_zone	*zone;
	struct super_block	*sb;

	sb   = inode->i_sb;
	zone = &sb->u.affs_sb.s_zones[zone_no];

	if (!zone || !zone->z_bm || !zone->z_bm->bm_bh)
		return 0;
	
	pr_debug("AFFS: balloc(inode=%lu,zone=%d)\n",inode->i_ino,zone_no);

	bm = (ULONG *)zone->z_bm->bm_bh->b_data;
repeat:
	fb = (zone->z_bm->bm_size + 31) >> 5;
	for (i = zone->z_start; i <= fb; i++) {
		if (bm[i])
			goto found;
	}
	return 0;

found:
	fwb = zone->z_bm->bm_firstblk + (i - 1) * 32;
	lock_super(sb);
	zone->z_start = i;
	w   = htonl(bm[i]);
	fb  = find_first_one_bit(&w,32);
	if (fb > 31 || !clear_bit(fb ^ BO_EXBITS,&bm[i])) {
		unlock_super(sb);
		printk("AFFS: balloc(): empty block disappeared somehow\n");
		goto repeat;
	}
	block = fwb + fb;
	zone->z_bm->bm_free--;

	/* prealloc as much as possible within this word, but not for headers */

	if (zone_no) {
		while (inode->u.affs_i.i_pa_cnt < MAX_PREALLOC && ++fb < 32) {
			fb = find_next_one_bit(&w,32,fb);
			if (fb > 31)
				break;
			if (!clear_bit(fb ^ BO_EXBITS,&bm[i])) {
				printk("AFFS: balloc(): empty block disappeared\n");
				break;
			}
			inode->u.affs_i.i_data[inode->u.affs_i.i_pa_last++] = fwb + fb;
			inode->u.affs_i.i_pa_last &= MAX_PREALLOC - 1;
			inode->u.affs_i.i_pa_cnt++;
			zone->z_bm->bm_free--;
		}
	}
	w    -= htonl(bm[i]);
	bm[0] = ntohl(htonl(bm[0]) + w);
	unlock_super(sb);
	mark_buffer_dirty(zone->z_bm->bm_bh,1);

	return block;
}

static void
affs_find_new_zone(struct super_block *sb,struct affs_zone *z, int minfree, int start)
{
	struct affs_bm_info	*bm;
	int			 offs;
	int			 zone;
	int			 free;
	int			 len;

	pr_debug("AFFS: find_new_zone()\n");

	lock_super(sb);

	zone    = start;
	z->z_bm = NULL;
	while (1) {
		if (zone >= sb->u.affs_sb.s_num_zones) {
			zone = 0;
			continue;
		}

		if (!set_bit(zone,sb->u.affs_sb.s_zonemap)) {
			bm   = &sb->u.affs_sb.s_bitmap[zone >> (sb->s_blocksize_bits - 8)];
			offs = zone * 256 & (sb->s_blocksize - 1);
			len  = bm->bm_size / 8 - offs;
			if (len > 256)
				len = 256;
			offs += 4;
			free  = affs_count_free_bits(len,(char *)bm->bm_bh->b_data + offs);
			if (free && (100 * free) / (len * 8) > minfree) {
				z->z_bm       = bm;
				z->z_start    = offs / 4;
				z->z_ino      = 0;
				z->z_zone_no  = zone;
				pr_debug("  ++ found zone (%d) in bh %d at offset %d with %d free blocks\n",
					 zone,(zone >> (sb->s_blocksize_bits - 8)),offs,free);
				break;
			}
			clear_bit(zone,sb->u.affs_sb.s_zonemap);
		}

		/* Skip to next possible zone */

		pr_debug("  ++ Skipping to next zone\n");
		if (++zone == start)
			break;
	}
	unlock_super(sb);
	return;
}

LONG
affs_new_header(struct inode *inode)
{
	struct affs_zone	*zone;
	LONG			 block;
	struct super_block	*sb;
	struct buffer_head	*bh;

	sb   = inode->i_sb;
	zone = &sb->u.affs_sb.s_zones[0];

	/* We try up to 3 times to find a free block:
	 * If there is no more room in the current header zone,
	 * we try to get a new one and allocate the block there.
	 * If there is no zone with at least AFFS_HDR_MIN_FREE
	 * percent of free blocks, we try to find a zone with
	 * at least one free block.
	 */

	if (!(block = affs_balloc(inode,0))) {
		clear_bit(zone->z_zone_no,sb->u.affs_sb.s_zonemap);
		affs_find_new_zone(sb,zone,AFFS_HDR_MIN_FREE,(sb->u.affs_sb.s_num_zones + 1) / 2);
		if (!(block = affs_balloc(inode,0))) {
			clear_bit(zone->z_zone_no,sb->u.affs_sb.s_zonemap);
			affs_find_new_zone(sb,zone,0,(sb->u.affs_sb.s_num_zones + 1) / 2);
			if (!(block = affs_balloc(inode,0)))
				return 0;
		}
	}
	if (!(bh = getblk(inode->i_dev,block,sb->s_blocksize))) {
		printk("AFFS: balloc(): cannot read block %d\n",block);
		return 0;
	}
	memset(bh->b_data,0,sb->s_blocksize);
	mark_buffer_uptodate(bh,1);
	mark_buffer_dirty(bh,1);
	affs_brelse(bh);

	return block;
}

LONG
affs_new_data(struct inode *inode)
{
	int			 empty, old;
	unsigned long		 oldest;
	struct affs_zone	*zone;
	struct super_block	*sb;
	struct buffer_head	*bh;
	int			 i = 0;
	LONG			 block;

	pr_debug("AFFS: new_data(ino=%lu)\n",inode->i_ino);

	sb = inode->i_sb;
	lock_super(sb);
	if (inode->u.affs_i.i_pa_cnt) {
		inode->u.affs_i.i_pa_cnt--;
		unlock_super(sb);
		block = inode->u.affs_i.i_data[inode->u.affs_i.i_pa_next++];
		inode->u.affs_i.i_pa_next &= MAX_PREALLOC - 1;
		goto init_block;
	}
	unlock_super(sb);
repeat:
	oldest = jiffies;
	old    = 0;
	empty  = 0;
	zone   = &sb->u.affs_sb.s_zones[inode->u.affs_i.i_zone];
	if (zone->z_ino == inode->i_ino) {
		i = inode->u.affs_i.i_zone;
		goto found;
	}
	for (i = 1; i < MAX_ZONES; i++) {
		zone = &sb->u.affs_sb.s_zones[i];
		if (!empty && zone->z_bm && !zone->z_ino)
			empty = i;
		if (zone->z_bm && zone->z_lru_time < oldest) {
			old    = i;
			oldest = zone->z_lru_time;
		}
	}
	if (empty)
		i = empty;
	else if (old)
		i = old;
	else
		return affs_new_header(inode);

	inode->u.affs_i.i_zone = i;
	zone->z_ino            = inode->i_ino;

found:
	zone = &sb->u.affs_sb.s_zones[i];
	if (!(block = affs_balloc(inode,i))) {				/* Zone is full */
		clear_bit(zone->z_zone_no,sb->u.affs_sb.s_zonemap);
		affs_find_new_zone(sb,zone,AFFS_DATA_MIN_FREE,sb->u.affs_sb.s_nextzone);
		sb->u.affs_sb.s_nextzone = zone->z_zone_no + 1;
		goto repeat;
	}

init_block:
	if (!(bh = getblk(inode->i_dev,block,sb->s_blocksize))) {
		printk("AFFS: balloc(): cannot read block %u\n",block);
		return 0;
	}
	memset(bh->b_data,0,sb->s_blocksize);
	mark_buffer_uptodate(bh,1);
	mark_buffer_dirty(bh,1);
	affs_brelse(bh);

	return block;
}

void
affs_make_zones(struct super_block *sb)
{
	int	 i, j;

	pr_debug("AFFS: make_zones(): num_zones=%d\n",sb->u.affs_sb.s_num_zones);

	j = (sb->u.affs_sb.s_num_zones + 1) / 2;

	affs_find_new_zone(sb,&sb->u.affs_sb.s_zones[0],AFFS_HDR_MIN_FREE,j);
	for (i = 1; i < MAX_ZONES; i++) {
		affs_find_new_zone(sb,&sb->u.affs_sb.s_zones[i],AFFS_DATA_MIN_FREE,j);
		j = sb->u.affs_sb.s_zones[i].z_zone_no + 1;
	}
}
