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

#ifndef __LITTLE_ENDIAN
static int sync_block_swab32 (struct inode * inode, u32 * block, int wait)
{
	struct buffer_head * bh;
	
	if (!le32_to_cpu(*block))
		return 0;
	bh = get_hash_table (inode->i_dev, le32_to_cpu(*block), blocksize);
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
#else
#define sync_block_swab32 sync_block
#endif


static int sync_iblock (struct inode * inode, u32 * iblock, 
			struct buffer_head ** bh, int wait) 
{
	int rc, tmp;
	
	*bh = NULL;
	tmp = *iblock;
	if (!tmp)
		return 0;
	rc = sync_block (inode, iblock, wait);
	if (rc)
		return rc;
	*bh = bread (inode->i_dev, tmp, blocksize);
	if (!*bh)
		return -1;
	return 0;
}

#ifndef __LITTLE_ENDIAN
static int sync_iblock_swab32 (struct inode * inode, u32 * iblock, 
			       struct buffer_head ** bh, int wait) 
{
	int rc, tmp;
	
	*bh = NULL;
	tmp = le32_to_cpu(*iblock);
	if (!tmp)
		return 0;
	rc = sync_block_swab32 (inode, iblock, wait);
	if (rc)
		return rc;
	*bh = bread (inode->i_dev, tmp, blocksize);
	if (!*bh)
		return -1;
	return 0;
}
#else
#define sync_iblock_swab32 sync_iblock
#endif

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

static int sync_indirect (struct inode * inode, u32 * iblock, int wait)
{
	int i;
	struct buffer_head * ind_bh;
	int rc, err = 0;

	rc = sync_iblock (inode, iblock, &ind_bh, wait);
	if (rc || !ind_bh)
		return rc;
	
	for (i = 0; i < addr_per_block; i++) {
		rc = sync_block_swab32 (inode, 
					((u32 *) ind_bh->b_data) + i,
					wait);
		if (rc)
			err = rc;
	}
	brelse (ind_bh);
	return err;
}

#ifndef __LITTLE_ENDIAN
static __inline__ int sync_indirect_swab32 (struct inode * inode, u32 * iblock, int wait)
{
	int i;
	struct buffer_head * ind_bh;
	int rc, err = 0;

	rc = sync_iblock_swab32 (inode, iblock, &ind_bh, wait);
	if (rc || !ind_bh)
		return rc;
	
	for (i = 0; i < addr_per_block; i++) {
		rc = sync_block_swab32 (inode, 
					((u32 *) ind_bh->b_data) + i,
					wait);
		if (rc)
			err = rc;
	}
	brelse (ind_bh);
	return err;
}
#else
#define sync_indirect_swab32 sync_indirect
#endif

static int sync_dindirect (struct inode * inode, u32 * diblock, int wait)
{
	int i;
	struct buffer_head * dind_bh;
	int rc, err = 0;

	rc = sync_iblock (inode, diblock, &dind_bh, wait);
	if (rc || !dind_bh)
		return rc;
	
	for (i = 0; i < addr_per_block; i++) {
		rc = sync_indirect_swab32 (inode,
					   ((u32 *) dind_bh->b_data) + i,
					   wait);
		if (rc)
			err = rc;
	}
	brelse (dind_bh);
	return err;
}

#ifndef __LITTLE_ENDIAN
static __inline__ int sync_dindirect_swab32 (struct inode * inode, u32 * diblock, int wait)
{
	int i;
	struct buffer_head * dind_bh;
	int rc, err = 0;

	rc = sync_iblock_swab32 (inode, diblock, &dind_bh, wait);
	if (rc || !dind_bh)
		return rc;
	
	for (i = 0; i < addr_per_block; i++) {
		rc = sync_indirect_swab32 (inode,
					   ((u32 *) dind_bh->b_data) + i,
					   wait);
		if (rc)
			err = rc;
	}
	brelse (dind_bh);
	return err;
}
#else
#define sync_dindirect_swab32 sync_dindirect
#endif

static int sync_tindirect (struct inode * inode, u32 * tiblock, int wait)
{
	int i;
	struct buffer_head * tind_bh;
	int rc, err = 0;

	rc = sync_iblock (inode, tiblock, &tind_bh, wait);
	if (rc || !tind_bh)
		return rc;
	
	for (i = 0; i < addr_per_block; i++) {
		rc = sync_dindirect_swab32 (inode,
					    ((u32 *) tind_bh->b_data) + i,
					    wait);
		if (rc)
			err = rc;
	}
	brelse (tind_bh);
	return err;
}

/*
 *	File may be NULL when we are called. Perhaps we shouldn't
 *	even pass file to fsync ?
 */

int ext2_sync_file(struct file * file, struct dentry *dentry)
{
	int wait, err = 0;
	struct inode *inode = dentry->d_inode;

	if (S_ISLNK(inode->i_mode) && !(inode->i_blocks))
		/*
		 * Don't sync fast links!
		 */
		goto skip;

	for (wait=0; wait<=1; wait++)
	{
		err |= sync_direct (inode, wait);
		err |= sync_indirect (inode,
				      inode->u.ext2_i.i_data+EXT2_IND_BLOCK,
				      wait);
		err |= sync_dindirect (inode,
				       inode->u.ext2_i.i_data+EXT2_DIND_BLOCK, 
				       wait);
		err |= sync_tindirect (inode, 
				       inode->u.ext2_i.i_data+EXT2_TIND_BLOCK, 
				       wait);
	}
skip:
	err |= ext2_sync_inode (inode);
	return err ? -EIO : 0;
}
