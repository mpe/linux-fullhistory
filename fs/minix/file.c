/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include <asm/segment.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/locks.h>

#define	NBUF	16

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <linux/fs.h>
#include <linux/minix_fs.h>

static int minix_file_read(struct inode *, struct file *, char *, int);
static int minix_file_write(struct inode *, struct file *, char *, int);

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the minix filesystem.
 */
static struct file_operations minix_file_operations = {
	NULL,			/* lseek - default */
	minix_file_read,	/* read */
	minix_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open is needed */
	NULL			/* release */
};

struct inode_operations minix_file_inode_operations = {
	&minix_file_operations,	/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	minix_bmap,		/* bmap */
	minix_truncate		/* truncate */
};

static int minix_file_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int read,left,chars;
	int block, blocks, offset;
	int bhrequest;
	int ra_blocks, max_block, nextblock;
	struct buffer_head ** bhb, ** bhe;
	struct buffer_head * bhreq[NBUF];
	struct buffer_head * buflist[NBUF];

	if (!inode) {
		printk("minix_file_read: inode = NULL\n");
		return -EINVAL;
	}
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))) {
		printk("minix_file_read: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
	if (filp->f_pos > inode->i_size)
		left = 0;
	else
		left = inode->i_size - filp->f_pos;
	if (left > count)
		left = count;
	if (left <= 0)
		return 0;
	read = 0;
	block = filp->f_pos >> BLOCK_SIZE_BITS;
	offset = filp->f_pos & (BLOCK_SIZE-1);
	blocks = (left + offset + BLOCK_SIZE - 1) / BLOCK_SIZE;
	bhb = bhe = buflist;

	ra_blocks = read_ahead[MAJOR(inode->i_dev)] / (BLOCK_SIZE >> 9);
	max_block = (inode->i_size + BLOCK_SIZE - 1)/BLOCK_SIZE;
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
			*bhb = minix_getblk(inode, block++, 0);
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
		    *bhb = minix_getblk(inode, block++, 0);

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
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	return read;
}

static int minix_file_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int written,c;
	struct buffer_head * bh;
	char * p;

	if (!inode) {
		printk("minix_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (!S_ISREG(inode->i_mode)) {
		printk("minix_file_write: mode = %07o\n",inode->i_mode);
		return -EINVAL;
	}
/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;
	written = 0;
	while (written<count) {
		bh = minix_getblk(inode,pos/BLOCK_SIZE,1);
		if (!bh) {
			if (!written)
				written = -ENOSPC;
			break;
		}
		c = BLOCK_SIZE - (pos % BLOCK_SIZE);
		if (c > count-written)
			c = count-written;
		if (c != BLOCK_SIZE && !bh->b_uptodate) {
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			if (!bh->b_uptodate) {
				brelse(bh);
				if (!written)
					written = -EIO;
				break;
			}
		}
		p = (pos % BLOCK_SIZE) + bh->b_data;
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		written += c;
		memcpy_fromfs(p,buf,c);
		buf += c;
		bh->b_uptodate = 1;
		bh->b_dirt = 1;
		brelse(bh);
	}
	inode->i_mtime = CURRENT_TIME;
	inode->i_ctime = CURRENT_TIME;
	filp->f_pos = pos;
	inode->i_dirt = 1;
	return written;
}
