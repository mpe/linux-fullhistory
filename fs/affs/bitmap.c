/*
 *  linux/fs/affs/bitmap.c
 *
 *  (c) 1996 Hans-Joachim Widmaier
 *
 *
 *  bitmap.c contains the code that handles all bitmap related stuff -
 *  block allocation, deallocation, calculation of free space.
 */

#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/amigaffs.h>

#include <asm/bitops.h>

/* This is, of course, shamelessly stolen from fs/minix */

static int nibblemap[] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };

int
affs_count_free_bits(int blocksize, const char *data)
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
		for (i = 0; i < s->u.affs_sb.s_num_az; i++) {
			free += s->u.affs_sb.s_alloc[i].az_free;
		}
	}
	return free;
}

void
affs_free_block(struct super_block *sb, int block)
{
	int			 bmap;
	int			 bit;
	int			 blk;
	int			 zone_no;
	struct affs_bm_info	*bm;

	pr_debug("AFFS: free_block(%d)\n",block);

	blk     = block - sb->u.affs_sb.s_reserved;
	bmap    = blk / (sb->s_blocksize * 8 - 32);
	bit     = blk % (sb->s_blocksize * 8 - 32);
	zone_no = (bmap << (sb->s_blocksize_bits - 7)) + bit / 1024;
	bm      = &sb->u.affs_sb.s_bitmap[bmap];
	if (bmap >= sb->u.affs_sb.s_bm_count) {
		printk("AFFS: free_block(): block %d outside partition.\n",block);
		return;
	}
	blk = 0;
	set_bit(bit & 31,&blk);

	lock_super(sb);
	bm->bm_count++;
	if (!bm->bm_bh) {
		bm->bm_bh = affs_bread(sb->s_dev,bm->bm_key,sb->s_blocksize);
		if (!bm->bm_bh) {
			bm->bm_count--;
			unlock_super(sb);
			printk("AFFS: free_block(): Cannot read bitmap block %d\n",bm->bm_key);
			return;
		}
	}
	if (set_bit(bit ^ BO_EXBITS,bm->bm_bh->b_data + 4))
		printk("AFFS: free_block(): block %d is already free.\n",block);
	else {
		sb->u.affs_sb.s_alloc[zone_no].az_free++;
		((__u32 *)bm->bm_bh->b_data)[0] = ntohl(htonl(((__u32 *)bm->bm_bh->b_data)[0]) - blk);
		mark_buffer_dirty(bm->bm_bh,1);
		sb->s_dirt = 1;
	}
	if (--bm->bm_count == 0) {
		affs_brelse(bm->bm_bh);
		bm->bm_bh = NULL;
	}
	unlock_super(sb);
}

static int
affs_balloc(struct inode *inode, int zone_no)
{
	__u32			 w;
	__u32			*bm;
	int			 fb;
	int			 i;
	int			 fwb;
	int			 block;
	struct affs_zone	*zone;
	struct affs_alloc_zone	*az;
	struct super_block	*sb;

	sb   = inode->i_sb;
	zone = &sb->u.affs_sb.s_zones[zone_no];

	if (!zone->z_bm || !zone->z_bm->bm_bh)
		return 0;	

	pr_debug("AFFS: balloc(inode=%lu,zone=%d)\n",inode->i_ino,zone_no);

	az = &sb->u.affs_sb.s_alloc[zone->z_az_no];
	bm = (__u32 *)zone->z_bm->bm_bh->b_data;
repeat:
	for (i = zone->z_start; i < zone->z_end; i++) {
		if (bm[i])
			goto found;
	}
	return 0;	

found:
	fwb = zone->z_bm->bm_firstblk + (i - 1) * 32;
	lock_super(sb);
	zone->z_start = i;
	w   = ~htonl(bm[i]);
	fb  = find_first_zero_bit(&w,32);
	if (fb > 31 || !clear_bit(fb ^ BO_EXBITS,&bm[i])) {
		unlock_super(sb);
		printk("AFFS: balloc(): empty block disappeared somehow\n");
		goto repeat;
	}
	block = fwb + fb;
	az->az_free--;

	/* prealloc as much as possible within this word, but not in header zone */

	if (zone_no) {
		while (inode->u.affs_i.i_pa_cnt < MAX_PREALLOC && ++fb < 32) {
			fb = find_next_zero_bit(&w,32,fb);
			if (fb > 31)
				break;
			if (!clear_bit(fb ^ BO_EXBITS,&bm[i])) {
				printk("AFFS: balloc(): empty block disappeared\n");
				break;
			}
			inode->u.affs_i.i_data[inode->u.affs_i.i_pa_last++] = fwb + fb;
			inode->u.affs_i.i_pa_last &= MAX_PREALLOC - 1;
			inode->u.affs_i.i_pa_cnt++;
			az->az_free--;
		}
	}
	w     = ~w - htonl(bm[i]);
	bm[0] = ntohl(htonl(bm[0]) + w);
	unlock_super(sb);
	mark_buffer_dirty(zone->z_bm->bm_bh,1);
	zone->z_lru_time = jiffies;

	return block;
}

static int
affs_find_new_zone(struct super_block *sb, int zone_no)
{
	struct affs_bm_info	*bm;
	struct affs_zone	*zone;
	struct affs_alloc_zone	*az;
	int			 bestfree;
	int			 bestno;
	int			 bestused;
	int			 lusers;
	int			 i;
	int			 min;

	pr_debug("AFFS: find_new_zone(zone_no=%d)\n",zone_no);

	bestfree = 0;
	bestused = -1;
	bestno   = -1;
	lusers   = MAX_ZONES;
	min      = zone_no ? AFFS_DATA_MIN_FREE : AFFS_HDR_MIN_FREE;
	lock_super(sb);
	zone = &sb->u.affs_sb.s_zones[zone_no];
	i    = zone->z_az_no;
	az   = &sb->u.affs_sb.s_alloc[i];
	if (zone->z_bm && zone->z_bm->bm_count) {
		if (--zone->z_bm->bm_count == 0) {
			affs_brelse(zone->z_bm->bm_bh);
			zone->z_bm->bm_bh = NULL;
		}
		if (az->az_count)
			az->az_count--;
		else
			printk("AFFS: find_new_zone(): az_count=0, but bm used\n");

	}
	while (1) {
		if (i >= sb->u.affs_sb.s_num_az)
			i = 0;
		az = &sb->u.affs_sb.s_alloc[i];
		if (!az->az_count) {
			if (az->az_free > min) {
				break;
			}
			if (az->az_free > bestfree) {
				bestfree = az->az_free;
				bestno   = i;
			}
		} else if (az->az_free && az->az_count < lusers) {
			lusers   = az->az_count;
			bestused = i;
		}
		if (++i == zone->z_az_no) {		/* Seen all */
			if (bestno >= 0) {
				i = bestno;
			} else {
				i = bestused;
			}
			break;
		}
	}
	if (i < 0) {
		/* Didn't find a single free block anywhere. */
		unlock_super(sb);
		return 0;
	}
	az = &sb->u.affs_sb.s_alloc[i];
	az->az_count++;
	bm = &sb->u.affs_sb.s_bitmap[i >> (sb->s_blocksize_bits - 7)];
	bm->bm_count++;
	if (!bm->bm_bh)
		bm->bm_bh = affs_bread(sb->s_dev,bm->bm_key,sb->s_blocksize);
	if (!bm->bm_bh) {
		bm->bm_count--;
		az->az_count--;
		unlock_super(sb);
		printk("AFFS: find_new_zone(): Cannot read bitmap\n");
		return 0;
	}
	zone->z_bm    = bm;
	zone->z_start = (i & ((sb->s_blocksize / 128) - 1)) * 32 + 1;
	zone->z_end   = zone->z_start + az->az_size;
	zone->z_az_no = i;
	zone->z_lru_time = jiffies;
	pr_debug("  ++ found zone (%d) in bm %d at lw offset %d with %d free blocks\n",
		 i,(i >> (sb->s_blocksize_bits - 7)),zone->z_start,az->az_free);
	unlock_super(sb);
	return az->az_free;
}

int
affs_new_header(struct inode *inode)
{
	int			 block;
	struct buffer_head	*bh;

	pr_debug("AFFS: new_header(ino=%lu)\n",inode->i_ino);

	if (!(block = affs_balloc(inode,0))) {
		while(affs_find_new_zone(inode->i_sb,0)) {
			if ((block = affs_balloc(inode,0)))
				goto init_block;
			schedule();
		}
		return 0;
	}
init_block:
	if (!(bh = getblk(inode->i_dev,block,AFFS_I2BSIZE(inode)))) {
		printk("AFFS: balloc(): cannot read block %d\n",block);
		return 0;
	}
	memset(bh->b_data,0,AFFS_I2BSIZE(inode));
	mark_buffer_uptodate(bh,1);
	mark_buffer_dirty(bh,1);
	affs_brelse(bh);

	return block;
}

int
affs_new_data(struct inode *inode)
{
	int			 empty, old;
	unsigned long		 oldest;
	struct affs_zone	*zone;
	struct super_block	*sb;
	struct buffer_head	*bh;
	int			 i = 0;
	int			 block;

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
	else {
		inode->u.affs_i.i_zone = 0;
		return affs_new_header(inode);
	}

	inode->u.affs_i.i_zone = i;
	zone->z_ino            = inode->i_ino;

found:
	zone = &sb->u.affs_sb.s_zones[i];
	if (!(block = affs_balloc(inode,i))) {		/* No data zones left */
		while(affs_find_new_zone(sb,i)) {
			if ((block = affs_balloc(inode,i)))
				goto init_block;
			schedule();
		}
		inode->u.affs_i.i_zone = 0;
		zone->z_ino            = -1;
		return 0;
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
	int	 i, mid;

	pr_debug("AFFS: make_zones(): num_zones=%d\n",sb->u.affs_sb.s_num_az);

	mid = (sb->u.affs_sb.s_num_az + 1) / 2;
	sb->u.affs_sb.s_zones[0].z_az_no = mid;
	affs_find_new_zone(sb,0);
	for (i = 1; i < MAX_ZONES; i++) {
		sb->u.affs_sb.s_zones[i].z_az_no = mid;
		affs_find_new_zone(sb,i);
	}
}
