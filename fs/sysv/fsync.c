/*
 *  linux/fs/sysv/fsync.c
 *
 *  minix/fsync.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1993  Stephen Tweedie (sct@dcs.ed.ac.uk)
 *
 *  coh/fsync.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/fsync.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent fsync primitive
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/errno.h>
#include <linux/stat.h>

#include <linux/fs.h>
#include <linux/sysv_fs.h>


/* return values: 0 means OK/done, 1 means redo, -1 means I/O error. */

/* Sync one block. The block number is
 * from_coh_ulong(*blockp) if convert=1, *blockp if convert=0.
 */
static int sync_block (struct inode * inode, unsigned long * blockp, int convert, int wait)
{
	struct buffer_head * bh;
	unsigned long tmp, block;
	struct super_block * sb;

	block = tmp = *blockp;
	if (convert)
		block = from_coh_ulong(block);
	if (!block)
		return 0;
	sb = inode->i_sb;
	bh = sv_get_hash_table(sb, inode->i_dev, block);
	if (!bh)
		return 0;
	if (*blockp != tmp) {
		brelse (bh);
		return 1;
	}
	if (wait && bh->b_req && !bh->b_uptodate) {
		brelse(bh);
		return -1;
	}
	if (wait || !bh->b_uptodate || !bh->b_dirt) {
		brelse(bh);
		return 0;
	}
	ll_rw_block(WRITE, 1, &bh);
	bh->b_count--;
	return 0;
}

/* Sync one block full of indirect pointers and read it because we'll need it. */
static int sync_iblock (struct inode * inode, unsigned long * iblockp, int convert,
			struct buffer_head * *bh, int wait)
{
	int rc;
	unsigned long tmp, block;

	*bh = NULL;
	block = tmp = *iblockp;
	if (convert)
		block = from_coh_ulong(block);
	if (!block)
		return 0;
	rc = sync_block (inode, iblockp, convert, wait);
	if (rc)
		return rc;
	*bh = sv_bread(inode->i_sb, inode->i_dev, block);
	if (tmp != *iblockp) {
		brelse(*bh);
		*bh = NULL;
		return 1;
	}
	if (!*bh)
		return -1;
	return 0;
}


static int sync_direct(struct inode *inode, int wait)
{
	int i;
	int rc, err = 0;

	for (i = 0; i < 10; i++) {
		rc = sync_block (inode, inode->u.sysv_i.i_data + i, 0, wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	return err;
}

static int sync_indirect(struct inode *inode, unsigned long *iblockp, int convert, int wait)
{
	int i;
	struct buffer_head * ind_bh;
	int rc, err = 0;
	struct super_block * sb;

	rc = sync_iblock (inode, iblockp, convert, &ind_bh, wait);
	if (rc || !ind_bh)
		return rc;

	sb = inode->i_sb;
	for (i = 0; i < sb->sv_ind_per_block; i++) {
		rc = sync_block (inode,
				 ((unsigned long *) ind_bh->b_data) + i, sb->sv_convert,
				 wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(ind_bh);
	return err;
}

static int sync_dindirect(struct inode *inode, unsigned long *diblockp, int convert,
			  int wait)
{
	int i;
	struct buffer_head * dind_bh;
	int rc, err = 0;
	struct super_block * sb;

	rc = sync_iblock (inode, diblockp, convert, &dind_bh, wait);
	if (rc || !dind_bh)
		return rc;

	sb = inode->i_sb;
	for (i = 0; i < sb->sv_ind_per_block; i++) {
		rc = sync_indirect (inode,
				    ((unsigned long *) dind_bh->b_data) + i, sb->sv_convert,
				    wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(dind_bh);
	return err;
}

static int sync_tindirect(struct inode *inode, unsigned long *tiblockp, int convert,
			  int wait)
{
	int i;
	struct buffer_head * tind_bh;
	int rc, err = 0;
	struct super_block * sb;

	rc = sync_iblock (inode, tiblockp, convert, &tind_bh, wait);
	if (rc || !tind_bh)
		return rc;

	sb = inode->i_sb;
	for (i = 0; i < sb->sv_ind_per_block; i++) {
		rc = sync_dindirect (inode,
				     ((unsigned long *) tind_bh->b_data) + i, sb->sv_convert,
				     wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(tind_bh);
	return err;
}

int sysv_sync_file(struct inode * inode, struct file * file)
{
	int wait, err = 0;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return -EINVAL;

	for (wait=0; wait<=1; wait++) {
		err |= sync_direct(inode, wait);
		err |= sync_indirect(inode, inode->u.sysv_i.i_data+10, 0, wait);
		err |= sync_dindirect(inode, inode->u.sysv_i.i_data+11, 0, wait);
		err |= sync_tindirect(inode, inode->u.sysv_i.i_data+12, 0, wait);
	}
	err |= sysv_sync_inode (inode);
	return (err < 0) ? -EIO : 0;
}
