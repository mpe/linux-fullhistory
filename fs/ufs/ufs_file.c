/*
 *  linux/fs/ufs/ufs_file.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_file.c,v 1.3 1996/04/25 09:12:02 davem Exp $
 *
 */

#include <linux/fs.h>

/*
 * Return values:
 *   0:  bmap failed
 *  nonzero: absolute "block" number
 */
int ufs_bmap (struct inode * inode, int block)
{
	unsigned long int fsblkno, phys_block, lfsblkno;
	struct buffer_head * bh;

	/*
	 * Note that contrary to what the BSD source calls these things,
	 * blkno and lblkno are *frags* (1024), not UFS blocks (8192).
	 * XXX - maybe I'm wrong, and ui_blocks is really 512-blocks...
	 */

	/*
	 * Ok, I think I figured out what is going on.  ui_blocks is the
	 * number of 512-byte blocks that are allocated to the file.  The
	 * elements in ui_db[UFS_NDADDR] are pointers to 1024-byte aligned
	 * 8192 byte objects.  The entire 8192 bytes (16 512-blocks) may
	 * not be allocated to the file in question - use ui_blocks to see
	 * how many of the blocks are allocated.  Also, use ui_size to see
	 * what fraction of the last block is allocated to the file, and
	 * what fraction is unused.  I have not yet seen a file with a
	 * hole in it, but I'd guess that a hole must be at least 8192
	 * bytes of zeros, and it's represented by a zero in ui_db[X].
	 *
	 * Yes, this means that there is more than one way to name a given
	 * 512-byte block on the disk. Because of the 1024-byte alignment
	 * of 8192-byte filesystem blocks, a given 512-byte disk block
	 * could be referred to in eight different ways.
	 */

	/*
	 * block is the logical 1024-block in the file
	 * lfsblkno is the logical 8192-block in the file
	 * fsblkno is the physical 8192-block
	 * phys_block is the 1024-block
	 */
	lfsblkno = block>>3;

	if (block < UFS_NDADDR) {
	        /* It's a direct block */
	        fsblkno = inode->u.ufs_i.ui_db[lfsblkno]; /* XXX */
#if 0
	        phys_block = ufs_cgdmin(inode->i_sb, ufs_ino2cg(inode)) +
	                blkno%(inode->i_sb->u.ufs_sb.s_fpg);
#endif
	        phys_block = fsblkno + ((block & 0x7)<<10); /* XXX */
	        if (inode->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	                printk("ufs_bmap: mapped ino %lu  logical %u to %lu (phys %lu)\n",
	                       inode->i_ino, block, fsblkno, phys_block);
	        }
	        return(phys_block);
	} else {
	        /* Need to use indirect blocks */
	        /* XXX - bmap through indirect blocks not implemented */
	        block -= UFS_NDADDR;
	        if (block < (inode->i_sb->s_blocksize/sizeof(__u32))) {
	                bh = bread(inode->i_dev, inode->u.ufs_i.ui_ib[0],
	                           BLOCK_SIZE);
	                if (bh == NULL) {
	                        printk("ufs_bmap: can't map block %u, ino %lu\n",
	                               block + UFS_NDADDR, inode->i_ino);
	                        return(0);
	                }
	                phys_block = ((__u32 *)bh->b_data)[block];
	                brelse(bh);
	                printk("ufs_bmap: imap ino %lu block %u phys %lu\n",
	                       inode->i_ino, block + UFS_NDADDR, phys_block);
	                return(phys_block);
	        } else {
	                printk("ufs_bmap: ino %lu: indirect blocks not implemented\n",
	                       inode->i_ino);
	                return(0);
	        }
	}

	return(0);
}

static struct file_operations ufs_file_operations = {
	NULL,			/* lseek */
	generic_file_read,	/* read */
	NULL,			/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	generic_file_mmap,	/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
};

struct inode_operations ufs_file_inode_operations = {
	&ufs_file_operations,	/* default directory file operations */
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
	generic_readpage,	/* readpage */
	NULL,			/* writepage */
	ufs_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
};


/*
 * Local Variables: ***
 * c-indent-level: 8 ***
 * c-continued-statement-offset: 8 ***
 * c-brace-offset: -8 ***
 * c-argdecl-indent: 0 ***
 * c-label-offset: -8 ***
 * End: ***
 */
