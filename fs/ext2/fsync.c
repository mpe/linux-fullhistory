/*
 *  linux/fs/ext2/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@dcs.ed.ac.uk)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 * 
 *  ext2fs fsync primitive
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 * 
 *  Removed unnecessary code duplication for little endian machines
 *  and excessive __inline__s. 
 *        Andi Kleen, 1997
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>


#define blocksize (EXT2_BLOCK_SIZE(inode->i_sb))
#define addr_per_block (EXT2_ADDR_PER_BLOCK(inode->i_sb))

static int sync_block (struct inode * inode, u32 * block, int wait)
{
	struct buffer_head * bh;
	
	if (!*block)
		return 0;
	bh = get_hash_table (inode->i_dev, *block, blocksize);
	if (!bh)
		return 0;
	if (wait && buffer_req(bh) && !buffer_uptodate(bh)) {
		brelse (bh);
		return -1;
	}
	if (wait || !buffer_uptodate(bh) || !buffer_dirty(bh)) {
		brelse (bh);
		return 0;
	}
	ll_rw_block (WRITE, 1, &bh);
	bh->b_count--;
	return 0;
}


static int sync_direct (struct inode * inode, int wait)
{
	int i;
	int rc, err = 0;

	for (i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		rc = sync_block (inode, inode->u.ext2_i.i_data + i, wait);
		if (rc)
			err = rc;
	}
	return err;
}

/*
 *	File may be NULL when we are called. Perhaps we shouldn't
 *	even pass file to fsync ?
 *
 *	This currently falls back to synching the whole device when
 *	the file is larger than can fit directly in the inode. This
 *	is because dirty-buffer handling is indexed by the device
 *	of the buffer, which makes it much faster to sync the whole
 *	device than to sync just one large file.
 */

int ext2_sync_file(struct file * file, struct dentry *dentry)
{
	int wait, err = 0;
	struct inode *inode = dentry->d_inode;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (S_ISLNK(inode->i_mode) && !(inode->i_blocks))
		/*
		 * Don't sync fast links!
		 */
		goto skip;

	if (inode->i_size > EXT2_NDIR_BLOCKS*blocksize) {
		err = fsync_dev(inode->i_dev);
		goto skip;
	}

	for (wait=0; wait<=1; wait++)
	{
		err |= sync_direct (inode, wait);
	}
skip:
	err |= ext2_sync_inode (inode);
	return err ? -EIO : 0;
}
