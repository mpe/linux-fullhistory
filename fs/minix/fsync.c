/*
 *  linux/fs/minix/fsync.c
 *
 *  Copyright (C) 1993 Stephen Tweedie (sct@dcs.ed.ac.uk)
 *  from
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  Copyright (C) 1996 Gertjan van Wingerde (gertjan@cs.vu.nl)
 *	Minix V2 fs support
 *
 *  minix fsync primitive
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>

#include <linux/fs.h>
#include <linux/minix_fs.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#define blocksize BLOCK_SIZE

/*
 * The functions for minix V1 fs file synchronization.
 */
static int V1_sync_block (struct inode * inode, unsigned short * block, int wait)
{
	struct buffer_head * bh;
	unsigned short tmp;
	
	if (!*block)
		return 0;
	tmp = *block;
	bh = get_hash_table(inode->i_dev, *block, blocksize);
	if (!bh)
		return 0;
	if (*block != tmp) {
		brelse (bh);
		return 1;
	}
	if (wait && buffer_req(bh) && !buffer_uptodate(bh)) {
		brelse(bh);
		return -1;
	}
	if (wait || !buffer_uptodate(bh) || !buffer_dirty(bh))
	{
		brelse(bh);
		return 0;
	}
	ll_rw_block(WRITE, 1, &bh);
	atomic_dec(&bh->b_count);
	return 0;
}

static int V1_sync_iblock (struct inode * inode, unsigned short * iblock, 
			struct buffer_head **bh, int wait) 
{
	int rc;
	unsigned short tmp;
	
	*bh = NULL;
	tmp = *iblock;
	if (!tmp)
		return 0;
	rc = V1_sync_block (inode, iblock, wait);
	if (rc)
		return rc;
	*bh = bread(inode->i_dev, tmp, blocksize);
	if (tmp != *iblock) {
		brelse(*bh);
		*bh = NULL;
		return 1;
	}
	if (!*bh)
		return -1;
	return 0;
}

static int V1_sync_direct(struct inode *inode, int wait)
{
	int i;
	int rc, err = 0;

	for (i = 0; i < 7; i++) {
		rc = V1_sync_block (inode, 
		     (unsigned short *) inode->u.minix_i.u.i1_data + i, wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	return err;
}

static int V1_sync_indirect(struct inode *inode, unsigned short *iblock, int wait)
{
	int i;
	struct buffer_head * ind_bh;
	int rc, err = 0;

	rc = V1_sync_iblock (inode, iblock, &ind_bh, wait);
	if (rc || !ind_bh)
		return rc;

	for (i = 0; i < 512; i++) {
		rc = V1_sync_block (inode, 
				    ((unsigned short *) ind_bh->b_data) + i, 
				    wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(ind_bh);
	return err;
}

static int V1_sync_dindirect(struct inode *inode, unsigned short *diblock,
			  int wait)
{
	int i;
	struct buffer_head * dind_bh;
	int rc, err = 0;

	rc = V1_sync_iblock (inode, diblock, &dind_bh, wait);
	if (rc || !dind_bh)
		return rc;

	for (i = 0; i < 512; i++) {
		rc = V1_sync_indirect (inode,
				       ((unsigned short *) dind_bh->b_data) + i, 
				       wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(dind_bh);
	return err;
}

static int V1_minix_sync_file(struct inode * inode, struct file * file)
{
	int wait, err = 0;
	
	lock_kernel();
	for (wait=0; wait<=1; wait++)
	{
		err |= V1_sync_direct(inode, wait);
		err |= V1_sync_indirect(inode, inode->u.minix_i.u.i1_data + 7, wait);
		err |= V1_sync_dindirect(inode, inode->u.minix_i.u.i1_data + 8, wait);
	}
	err |= minix_sync_inode (inode);
	unlock_kernel();
	return (err < 0) ? -EIO : 0;
}

/* 
 * The functions for minix V2 fs file synchronization.
 */
static int V2_sync_block (struct inode * inode, unsigned long * block, int wait)
{
	struct buffer_head * bh;
	unsigned long tmp;
	
	if (!*block)
		return 0;
	tmp = *block;
	bh = get_hash_table(inode->i_dev, *block, blocksize);
	if (!bh)
		return 0;
	if (*block != tmp) {
		brelse (bh);
		return 1;
	}
	if (wait && buffer_req(bh) && !buffer_uptodate(bh)) {
		brelse(bh);
		return -1;
	}
	if (wait || !buffer_uptodate(bh) || !buffer_dirty(bh))
	{
		brelse(bh);
		return 0;
	}
	ll_rw_block(WRITE, 1, &bh);
	atomic_dec(&bh->b_count);
	return 0;
}

static int V2_sync_iblock (struct inode * inode, unsigned long * iblock, 
			struct buffer_head **bh, int wait) 
{
	int rc;
	unsigned long tmp;
	
	*bh = NULL;
	tmp = *iblock;
	if (!tmp)
		return 0;
	rc = V2_sync_block (inode, iblock, wait);
	if (rc)
		return rc;
	*bh = bread(inode->i_dev, tmp, blocksize);
	if (tmp != *iblock) {
		brelse(*bh);
		*bh = NULL;
		return 1;
	}
	if (!*bh)
		return -1;
	return 0;
}

static int V2_sync_direct(struct inode *inode, int wait)
{
	int i;
	int rc, err = 0;

	for (i = 0; i < 7; i++) {
		rc = V2_sync_block (inode, 
			(unsigned long *)inode->u.minix_i.u.i2_data + i, wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	return err;
}

static int V2_sync_indirect(struct inode *inode, unsigned long *iblock, int wait)
{
	int i;
	struct buffer_head * ind_bh;
	int rc, err = 0;

	rc = V2_sync_iblock (inode, iblock, &ind_bh, wait);
	if (rc || !ind_bh)
		return rc;

	for (i = 0; i < 256; i++) {
		rc = V2_sync_block (inode, 
				    ((unsigned long *) ind_bh->b_data) + i, 
				    wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(ind_bh);
	return err;
}

static int V2_sync_dindirect(struct inode *inode, unsigned long *diblock,
			  int wait)
{
	int i;
	struct buffer_head * dind_bh;
	int rc, err = 0;

	rc = V2_sync_iblock (inode, diblock, &dind_bh, wait);
	if (rc || !dind_bh)
		return rc;

	for (i = 0; i < 256; i++) {
		rc = V2_sync_indirect (inode,
				       ((unsigned long *) dind_bh->b_data) + i, 
				       wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(dind_bh);
	return err;
}

static int V2_sync_tindirect(struct inode *inode, unsigned long *tiblock,
			  int wait)
{
	int i;
	struct buffer_head * tind_bh;
	int rc, err = 0;

	rc = V2_sync_iblock (inode, tiblock, &tind_bh, wait);
	if (rc || !tind_bh)
		return rc;

	for (i = 0; i < 256; i++) {
		rc = V2_sync_dindirect (inode,
					((unsigned long *) tind_bh->b_data) + i, 
					wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(tind_bh);
	return err;
}

static int V2_minix_sync_file(struct inode * inode, struct file * file)
{
	int wait, err = 0;
	
	lock_kernel();
	for (wait=0; wait<=1; wait++)
	{
		err |= V2_sync_direct(inode, wait);
		err |= V2_sync_indirect(inode, 
		      (unsigned long *) inode->u.minix_i.u.i2_data + 7, wait);
		err |= V2_sync_dindirect(inode, 
		      (unsigned long *) inode->u.minix_i.u.i2_data + 8, wait);
		err |= V2_sync_tindirect(inode, 
		      (unsigned long *) inode->u.minix_i.u.i2_data + 9, wait);
	}
	err |= minix_sync_inode (inode);
	unlock_kernel();
	return (err < 0) ? -EIO : 0;
}

/*
 *	The function which is called for file synchronization. File may be
 *	NULL
 */
 
int minix_sync_file(struct file * file, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return -EINVAL;

	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_sync_file(inode, file);
	else
		return V2_minix_sync_file(inode, file);
}
