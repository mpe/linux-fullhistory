/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/system.h>

extern int *blk_size[];

int block_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	int block = filp->f_pos >> BLOCK_SIZE_BITS;
	int offset = filp->f_pos & (BLOCK_SIZE-1);
	int chars;
	int written = 0;
	int size;
	unsigned int dev;
	struct buffer_head * bh;
	register char * p;

	dev = inode->i_rdev;
	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		size = 0x7fffffff;
	while (count>0) {
		if (block >= size)
			return written;
		chars = BLOCK_SIZE - offset;
		if (chars > count)
			chars=count;
		if (chars == BLOCK_SIZE)
			bh = getblk(dev, block, BLOCK_SIZE);
		else
			bh = breada(dev,block,block+1,block+2,-1);
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
		bh->b_dirt = 1;
		brelse(bh);
	}
	return written;
}

#define NBUF 16


int block_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	unsigned int block = filp->f_pos >> BLOCK_SIZE_BITS;
	unsigned int offset = filp->f_pos & (BLOCK_SIZE-1);
	int blocks, left;
	int bhrequest;
	int ra_blocks, max_block, nextblock;
	struct buffer_head ** bhb, ** bhe;
	struct buffer_head * buflist[NBUF];
	struct buffer_head * bhreq[NBUF];
	unsigned int chars;
	unsigned int size;
	unsigned int dev;
	int read = 0;

	dev = inode->i_rdev;
	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS;
	else
		size = 0x7fffffff;

	if (filp->f_pos > size)
		left = 0;
	else
		left = size - filp->f_pos;
	if (left > count)
		left = count;
	if (left <= 0)
		return 0;

	blocks = (left + offset + BLOCK_SIZE - 1) / BLOCK_SIZE;
	bhb = bhe = buflist;
	ra_blocks = read_ahead[MAJOR(dev)] / (BLOCK_SIZE >> 9);
	max_block = size;
	nextblock = -1;

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
		while (blocks) {
		        int uptodate;
			--blocks;
			*bhb = getblk(dev, block++, BLOCK_SIZE);
			uptodate = 1;
			if (*bhb && !(*bhb)->b_uptodate) {
			        uptodate = 0;
			        bhreq[bhrequest++] = *bhb;
				nextblock = (*bhb)->b_blocknr + 1;
			      };

			if (++bhb == &buflist[NBUF])
				bhb = buflist;

			/* If the block we have on hand is uptodate, go ahead
			   and complete processing. */
			if(bhrequest == 0 && uptodate) break;

			if (bhb == bhe)
				break;
		      }

		if(blocks == 0 && bhrequest && filp->f_reada && bhb != bhe) { 
		  /* If we are going to read something anyways, add in the
		     read-ahead blocks */
		  while(ra_blocks){
		    if (block >= max_block) break;
		    if(bhrequest == NBUF) break;  /* Block full */
		    --ra_blocks;
		    *bhb = getblk(dev, block++, BLOCK_SIZE);

		    if (*bhb && !(*bhb)->b_uptodate) {
		      if((*bhb)->b_blocknr != nextblock) {
			brelse(*bhb);
			break;
		      };
		      nextblock = (*bhb)->b_blocknr + 1;
		      bhreq[bhrequest++] = *bhb;
		    };
		    
		    if (++bhb == &buflist[NBUF])
		      bhb = buflist;
		    
		    if (bhb == bhe)
		      break;
		  };
		};
		/* Now request them all */
		if (bhrequest)
		  ll_rw_block(READ, bhrequest, bhreq);

		do{ /* Finish off all I/O that has actually completed */
		  if (*bhe) {/* test for valid buffer */
		    wait_on_buffer(*bhe);
		    if (!(*bhe)->b_uptodate) {
		      do {
			brelse(*bhe);
			if (++bhe == &buflist[NBUF])
			  bhe = buflist;
		      } while (bhe != bhb);
		      break;
		    }
		  }
		  
		  if (left < BLOCK_SIZE - offset)
		    chars = left;
		  else
		    chars = BLOCK_SIZE - offset;
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
		} while( bhe != bhb && (*bhe == 0 || !(*bhe)->b_lock) && 
			(left > 0));
	} while (left > 0);

/* Release the read-ahead blocks */
	while (bhe != bhb) {
	  if (*bhe) brelse(*bhe);
	  if (++bhe == &buflist[NBUF])
	    bhe = buflist;
	};

	filp->f_reada = 1;

	if (!read)
		return -EIO;

	return read;
}
