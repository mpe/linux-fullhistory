/*
 *  linux/fs/truncate.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Copyright (C) 1996  Gertjan van Wingerde (gertjan@cs.vu.nl)
 *	Minix V2 fs support.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>

#define DIRECT_BLOCK		((inode->i_size + 1023) >> 10)
#define INDIRECT_BLOCK(offset)	(DIRECT_BLOCK-offset)
#define V1_DINDIRECT_BLOCK(offset) ((DIRECT_BLOCK-offset)>>9)
#define V2_DINDIRECT_BLOCK(offset) ((DIRECT_BLOCK-offset)>>8)
#define TINDIRECT_BLOCK(offset) ((DIRECT_BLOCK-(offset))>>8)

/*
 * Truncate has the most races in the whole filesystem: coding it is
 * a pain in the a**. Especially as I don't do any locking...
 *
 * The code may look a bit weird, but that's just because I've tried to
 * handle things like file-size changes in a somewhat graceful manner.
 * Anyway, truncating a file at the same time somebody else writes to it
 * is likely to result in pretty weird behaviour...
 *
 * The new code handles normal truncates (size = 0) as well as the more
 * general case (size = XXX). I hope.
 */

/*
 * The functions for minix V1 fs truncation.
 */
static int V1_trunc_direct(struct inode * inode)
{
	unsigned short * p;
	struct buffer_head * bh;
	int i, tmp;
	int retry = 0;

repeat:
	for (i = DIRECT_BLOCK ; i < 7 ; i++) {
		p = i + inode->u.minix_i.u.i1_data;
		if (!(tmp = *p))
			continue;
		bh = get_hash_table(inode->i_dev,tmp,BLOCK_SIZE);
		if (i < DIRECT_BLOCK) {
			brelse(bh);
			goto repeat;
		}
		if ((bh && bh->b_count != 1) || tmp != *p) {
			retry = 1;
			brelse(bh);
			continue;
		}
		*p = 0;
		inode->i_dirt = 1;
		if (bh) {
			mark_buffer_clean(bh);
			brelse(bh);
		}
		minix_free_block(inode->i_sb,tmp);
	}
	return retry;
}

static int V1_trunc_indirect(struct inode * inode, int offset, unsigned short * p)
{
	struct buffer_head * bh;
	int i, tmp;
	struct buffer_head * ind_bh;
	unsigned short * ind;
	int retry = 0;

	tmp = *p;
	if (!tmp)
		return 0;
	ind_bh = bread(inode->i_dev, tmp, BLOCK_SIZE);
	if (tmp != *p) {
		brelse(ind_bh);
		return 1;
	}
	if (!ind_bh) {
		*p = 0;
		return 0;
	}
repeat:
	for (i = INDIRECT_BLOCK(offset) ; i < 512 ; i++) {
		if (i < 0)
			i = 0;
		if (i < INDIRECT_BLOCK(offset))
			goto repeat;
		ind = i+(unsigned short *) ind_bh->b_data;
		tmp = *ind;
		if (!tmp)
			continue;
		bh = get_hash_table(inode->i_dev,tmp,BLOCK_SIZE);
		if (i < INDIRECT_BLOCK(offset)) {
			brelse(bh);
			goto repeat;
		}
		if ((bh && bh->b_count != 1) || tmp != *ind) {
			retry = 1;
			brelse(bh);
			continue;
		}
		*ind = 0;
		mark_buffer_dirty(ind_bh, 1);
		brelse(bh);
		minix_free_block(inode->i_sb,tmp);
	}
	ind = (unsigned short *) ind_bh->b_data;
	for (i = 0; i < 512; i++)
		if (*(ind++))
			break;
	if (i >= 512)
		if (ind_bh->b_count != 1)
			retry = 1;
		else {
			tmp = *p;
			*p = 0;
			minix_free_block(inode->i_sb,tmp);
		}
	brelse(ind_bh);
	return retry;
}

static int V1_trunc_dindirect(struct inode * inode, int offset, unsigned short *p)
{
	int i, tmp;
	struct buffer_head * dind_bh;
	unsigned short * dind;
	int retry = 0;

	if (!(tmp = *p))
		return 0;
	dind_bh = bread(inode->i_dev, tmp, BLOCK_SIZE);
	if (tmp != *p) {
		brelse(dind_bh);
		return 1;
	}
	if (!dind_bh) {
		*p = 0;
		return 0;
	}
repeat:
	for (i = V1_DINDIRECT_BLOCK(offset) ; i < 512 ; i ++) {
		if (i < 0)
			i = 0;
		if (i < V1_DINDIRECT_BLOCK(offset))
			goto repeat;
		dind = i+(unsigned short *) dind_bh->b_data;
		retry |= V1_trunc_indirect(inode,offset+(i<<9),dind);
		mark_buffer_dirty(dind_bh, 1);
	}
	dind = (unsigned short *) dind_bh->b_data;
	for (i = 0; i < 512; i++)
		if (*(dind++))
			break;
	if (i >= 512)
		if (dind_bh->b_count != 1)
			retry = 1;
		else {
			tmp = *p;
			*p = 0;
			inode->i_dirt = 1;
			minix_free_block(inode->i_sb,tmp);
		}
	brelse(dind_bh);
	return retry;
}

void V1_minix_truncate(struct inode * inode)
{
	int retry;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return;
	while (1) {
		retry = V1_trunc_direct(inode);
		retry |= V1_trunc_indirect(inode, 7, inode->u.minix_i.u.i1_data + 7);
		retry |= V1_trunc_dindirect(inode, 7+512, inode->u.minix_i.u.i1_data + 8);
		if (!retry)
			break;
		current->counter = 0;
		schedule();
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;
}

/*
 * The functions for minix V2 fs truncation.
 */
static int V2_trunc_direct(struct inode * inode)
{
	unsigned long * p;
	struct buffer_head * bh;
	int i, tmp;
	int retry = 0;

repeat:
	for (i = DIRECT_BLOCK ; i < 7 ; i++) {
		p = (unsigned long *) inode->u.minix_i.u.i2_data + i;
		if (!(tmp = *p))
			continue;
		bh = get_hash_table(inode->i_dev,tmp,BLOCK_SIZE);
		if (i < DIRECT_BLOCK) {
			brelse(bh);
			goto repeat;
		}
		if ((bh && bh->b_count != 1) || tmp != *p) {
			retry = 1;
			brelse(bh);
			continue;
		}
		*p = 0;
		inode->i_dirt = 1;
		if (bh) {
			mark_buffer_clean(bh);
			brelse(bh);
		}
		minix_free_block(inode->i_sb,tmp);
	}
	return retry;
}

static int V2_trunc_indirect(struct inode * inode, int offset, unsigned long * p)
{
	struct buffer_head * bh;
	int i, tmp;
	struct buffer_head * ind_bh;
	unsigned long * ind;
	int retry = 0;

	tmp = *p;
	if (!tmp)
		return 0;
	ind_bh = bread(inode->i_dev, tmp, BLOCK_SIZE);
	if (tmp != *p) {
		brelse(ind_bh);
		return 1;
	}
	if (!ind_bh) {
		*p = 0;
		return 0;
	}
repeat:
	for (i = INDIRECT_BLOCK(offset) ; i < 256 ; i++) {
		if (i < 0)
			i = 0;
		if (i < INDIRECT_BLOCK(offset))
			goto repeat;
		ind = i+(unsigned long *) ind_bh->b_data;
		tmp = *ind;
		if (!tmp)
			continue;
		bh = get_hash_table(inode->i_dev,tmp,BLOCK_SIZE);
		if (i < INDIRECT_BLOCK(offset)) {
			brelse(bh);
			goto repeat;
		}
		if ((bh && bh->b_count != 1) || tmp != *ind) {
			retry = 1;
			brelse(bh);
			continue;
		}
		*ind = 0;
		mark_buffer_dirty(ind_bh, 1);
		brelse(bh);
		minix_free_block(inode->i_sb,tmp);
	}
	ind = (unsigned long *) ind_bh->b_data;
	for (i = 0; i < 256; i++)
		if (*(ind++))
			break;
	if (i >= 256)
		if (ind_bh->b_count != 1)
			retry = 1;
		else {
			tmp = *p;
			*p = 0;
			minix_free_block(inode->i_sb,tmp);
		}
	brelse(ind_bh);
	return retry;
}

static int V2_trunc_dindirect(struct inode * inode, int offset, unsigned long *p)
{
	int i, tmp;
	struct buffer_head * dind_bh;
	unsigned long * dind;
	int retry = 0;

	if (!(tmp = *p))
		return 0;
	dind_bh = bread(inode->i_dev, tmp, BLOCK_SIZE);
	if (tmp != *p) {
		brelse(dind_bh);
		return 1;
	}
	if (!dind_bh) {
		*p = 0;
		return 0;
	}
repeat:
	for (i = V2_DINDIRECT_BLOCK(offset) ; i < 256 ; i ++) {
		if (i < 0)
			i = 0;
		if (i < V2_DINDIRECT_BLOCK(offset))
			goto repeat;
		dind = i+(unsigned long *) dind_bh->b_data;
		retry |= V2_trunc_indirect(inode,offset+(i<<8),dind);
		mark_buffer_dirty(dind_bh, 1);
	}
	dind = (unsigned long *) dind_bh->b_data;
	for (i = 0; i < 256; i++)
		if (*(dind++))
			break;
	if (i >= 256)
		if (dind_bh->b_count != 1)
			retry = 1;
		else {
			tmp = *p;
			*p = 0;
			inode->i_dirt = 1;
			minix_free_block(inode->i_sb,tmp);
		}
	brelse(dind_bh);
	return retry;
}

static int V2_trunc_tindirect(struct inode * inode, int offset, unsigned long * p)
{
        int i, tmp;
        struct buffer_head * tind_bh;
        unsigned long * tind;
        int retry = 0;

        if (!(tmp = *p))
                return 0;
        tind_bh = bread(inode->i_dev, tmp, BLOCK_SIZE);
        if (tmp != *p) {
                brelse(tind_bh);
                return 1;
	}
        if (!tind_bh) {
                *p = 0;
                return 0;
	}
repeat:
        for (i = TINDIRECT_BLOCK(offset) ; i < 256 ; i ++) {
                if (i < 0)
                        i = 0;
                if (i < TINDIRECT_BLOCK(offset))
                        goto repeat;
                tind = i+(unsigned long *) tind_bh->b_data;
                retry |= V2_trunc_dindirect(inode,offset+(i<<8),tind);
                mark_buffer_dirty(tind_bh, 1);
	}
        tind = (unsigned long *) tind_bh->b_data;
        for (i = 0; i < 256; i++)
                if (*(tind++))
                        break;
        if (i >= 256)
                if (tind_bh->b_count != 1)
                        retry = 1;
                else {
                        tmp = *p;
                        *p = 0;
                        inode->i_dirt = 1;
                        minix_free_block(inode->i_sb,tmp);
		}
        brelse(tind_bh);
        return retry;
}

static void V2_minix_truncate(struct inode * inode)
{
	int retry;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return;
	while (1) {
		retry = V2_trunc_direct(inode);
		retry |= V2_trunc_indirect(inode,7,
			(unsigned long *) inode->u.minix_i.u.i2_data + 7);
		retry |= V2_trunc_dindirect(inode, 7+256, 
			(unsigned long *) inode->u.minix_i.u.i2_data + 8);
		retry |= V2_trunc_tindirect(inode, 7+256+256*256, 
			(unsigned long *) inode->u.minix_i.u.i2_data + 9);
		if (!retry)
			break;
		current->counter = 0;
		schedule();
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;
}

/*
 * The function that is called for file truncation.
 */
void minix_truncate(struct inode * inode)
{
	if (INODE_VERSION(inode) == MINIX_V1)
		V1_minix_truncate(inode);
	else
		V2_minix_truncate(inode);
}
