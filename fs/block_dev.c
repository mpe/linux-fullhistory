/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <asm/system.h>

extern int *blk_size[];
extern int *blksize_size[];

#define NBUF 64

int block_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	int blocksize, blocksize_bits, i, j;
	int block, blocks;
	loff_t offset;
	int chars;
	int written = 0;
	int cluster_list[4];
	struct buffer_head * bhlist[NBUF];
	int blocks_per_cluster;
	unsigned int size;
	unsigned int dev;
	struct buffer_head * bh;
	register char * p;

	dev = inode->i_rdev;
	if ( is_read_only( inode->i_rdev ))
		return -EPERM;
	blocksize = BLOCK_SIZE;
	if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
		blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];

	i = blocksize;
	blocksize_bits = 0;
	while(i != 1) {
		blocksize_bits++;
		i >>= 1;
	}

	blocks_per_cluster = PAGE_SIZE / blocksize;

	block = filp->f_pos >> blocksize_bits;
	offset = filp->f_pos & (blocksize-1);

	if (blk_size[MAJOR(dev)])
		size = ((loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS) >> blocksize_bits;
	else
		size = INT_MAX;
	while (count>0) {
		if (block >= size)
			return written;
		chars = blocksize - offset;
		if (chars > count)
			chars=count;

#if 0
		if (chars == blocksize)
			bh = getblk(dev, block, blocksize);
		else
			bh = breada(dev,block,block+1,block+2,-1);

#else
		for(i=0; i<blocks_per_cluster; i++) cluster_list[i] = block+i;
		if((block % blocks_per_cluster) == 0)
		  generate_cluster(dev, cluster_list, blocksize);
		bh = getblk(dev, block, blocksize);

		if (chars != blocksize && !bh->b_uptodate) {
		  if(!filp->f_reada ||
		     !read_ahead[MAJOR(dev)]) {
		    /* We do this to force the read of a single buffer */
		    brelse(bh);
		    bh = bread(dev,block,blocksize);
		  } else {
		    /* Read-ahead before write */
		    blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9) / 2;
		    if (block + blocks > size) blocks = size - block;
		    if (blocks > NBUF) blocks=NBUF;
		    blocks -= (block % blocks_per_cluster);
		    if(!blocks) blocks = 1;
		    bhlist[0] = bh;
		    for(i=1; i<blocks; i++){
		      if(((i+block) % blocks_per_cluster) == 0) {
			for(j=0; j<blocks_per_cluster; j++) cluster_list[j] = block+i+j;
			generate_cluster(dev, cluster_list, blocksize);
		      };
		      bhlist[i] = getblk (dev, block+i, blocksize);
		      if(!bhlist[i]){
			while(i >= 0) brelse(bhlist[i--]);
			return written? written: -EIO;
		      };
		    };
		    ll_rw_block(READ, blocks, bhlist);
		    for(i=1; i<blocks; i++) brelse(bhlist[i]);
		      
		  };
		};
#endif
		block++;
		if (!bh)
			return written?written:-EIO;
		p = offset + bh->b_data;
		offset = 0;
		filp->f_pos += chars;
		written += chars;
		count -= chars;
		memcpy_fromfs(p,buf,chars);
		p += chars;
		buf += chars;
		bh->b_uptodate = 1;
		mark_buffer_dirty(bh, 0);
		if (filp->f_flags & O_SYNC) {
			ll_rw_block(WRITE, 1, &bh);
			wait_on_buffer(bh);
		}
		brelse(bh);
	}
	filp->f_reada = 1;
	return written;
}

int block_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	unsigned int block;
	loff_t offset;
	int blocksize;
	int blocksize_bits, i;
	unsigned int blocks, rblocks, left;
	int bhrequest, uptodate;
	int cluster_list[4];
	int blocks_per_cluster;
	struct buffer_head ** bhb, ** bhe;
	struct buffer_head * buflist[NBUF];
	struct buffer_head * bhreq[NBUF];
	unsigned int chars;
	loff_t size;
	unsigned int dev;
	int read;

	dev = inode->i_rdev;
	blocksize = BLOCK_SIZE;
	if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
		blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];
	i = blocksize;
	blocksize_bits = 0;
	while (i != 1) {
		blocksize_bits++;
		i >>= 1;
	}

	offset = filp->f_pos;
	if (blk_size[MAJOR(dev)])
		size = (loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS;
	else
		size = INT_MAX;

	blocks_per_cluster = PAGE_SIZE / blocksize;

	if (offset > size)
		left = 0;
	else
		left = size - offset;
	if (left > count)
		left = count;
	if (left <= 0)
		return 0;
	read = 0;
	block = offset >> blocksize_bits;
	offset &= blocksize-1;
	size >>= blocksize_bits;
	rblocks = blocks = (left + offset + blocksize - 1) >> blocksize_bits;
	bhb = bhe = buflist;
	if (filp->f_reada) {
	        if (blocks < read_ahead[MAJOR(dev)] / (blocksize >> 9))
			blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9);
		blocks -= (block % blocks_per_cluster);
		if (rblocks > blocks)
			blocks = rblocks;
		
	}
	if (block + blocks > size)
		blocks = size - block;

	/* We do this in a two stage process.  We first try and request
	   as many blocks as we can, then we wait for the first one to
	   complete, and then we try and wrap up as many as are actually
	   done.  This routine is rather generic, in that it can be used
	   in a filesystem by substituting the appropriate function in
	   for getblk.

	   This routine is optimized to make maximum use of the various
	   buffers and caches. */

	do {
		bhrequest = 0;
		uptodate = 1;
		while (blocks) {
			--blocks;
#if 1
			if((block % blocks_per_cluster) == 0) {
			  for(i=0; i<blocks_per_cluster; i++) cluster_list[i] = block+i;
			  generate_cluster(dev, cluster_list, blocksize);
			}
#endif
			*bhb = getblk(dev, block++, blocksize);
			if (*bhb && !(*bhb)->b_uptodate) {
				uptodate = 0;
				bhreq[bhrequest++] = *bhb;
			}

			if (++bhb == &buflist[NBUF])
				bhb = buflist;

			/* If the block we have on hand is uptodate, go ahead
			   and complete processing. */
			if (uptodate)
				break;
			if (bhb == bhe)
				break;
		}

		/* Now request them all */
		if (bhrequest) {
			ll_rw_block(READ, bhrequest, bhreq);
			refill_freelist(blocksize);
		}

		do { /* Finish off all I/O that has actually completed */
			if (*bhe) {
				wait_on_buffer(*bhe);
				if (!(*bhe)->b_uptodate) {	/* read error? */
				        brelse(*bhe);
					if (++bhe == &buflist[NBUF])
					  bhe = buflist;
					left = 0;
					break;
				}
			}			
			if (left < blocksize - offset)
				chars = left;
			else
				chars = blocksize - offset;
			filp->f_pos += chars;
			left -= chars;
			read += chars;
			if (*bhe) {
				memcpy_tofs(buf,offset+(*bhe)->b_data,chars);
				brelse(*bhe);
				buf += chars;
			} else {
				while (chars-->0)
					put_fs_byte(0,buf++);
			}
			offset = 0;
			if (++bhe == &buflist[NBUF])
				bhe = buflist;
		} while (left > 0 && bhe != bhb && (!*bhe || !(*bhe)->b_lock));
	} while (left > 0);

/* Release the read-ahead blocks */
	while (bhe != bhb) {
		brelse(*bhe);
		if (++bhe == &buflist[NBUF])
			bhe = buflist;
	};
	if (!read)
		return -EIO;
	filp->f_reada = 1;
	return read;
}

int block_fsync(struct inode *inode, struct file *filp)
{
	return fsync_dev (inode->i_rdev);
}
