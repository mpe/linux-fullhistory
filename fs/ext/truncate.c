/*
 *  linux/fs/ext/truncate.c
 *
 *  (C) 1992  Remy Card (card@masi.ibp.fr)
 *
 *  from
 *
 *  linux/fs/minix/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/ext_fs.h>
#include <linux/tty.h>
#include <linux/stat.h>
#include <linux/fcntl.h>

#include <errno.h>

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

static int trunc_direct(struct inode * inode)
{
	int i;
	int result = 0;
#define DIRECT_BLOCK ((inode->i_size + 1023) >> 10)

repeat:
	for (i = DIRECT_BLOCK ; i < 9 ; i++) {
		if (i < DIRECT_BLOCK)
			goto repeat;
		if (!inode->i_data[i])
			continue;
		result = 1;
		if (ext_free_block(inode->i_dev,inode->i_data[i]))
			inode->i_data[i] = 0;
	}
	return result;
}

static int trunc_indirect(struct inode * inode, int offset, unsigned long * p)
{
	int i;
	struct buffer_head * bh = NULL;
	unsigned long * ind;
	int result = 0;
#define INDIRECT_BLOCK (DIRECT_BLOCK-offset)

	if (*p)
		bh = bread(inode->i_dev,*p);
	if (!bh)
		return 0;
repeat:
	for (i = INDIRECT_BLOCK ; i < 256 ; i++) {
		if (i < 0)
			i = 0;
		if (i < INDIRECT_BLOCK)
			goto repeat;
		ind = i+(unsigned long *) bh->b_data;
		if (!*ind)
			continue;
		result = 1;
		if (ext_free_block(inode->i_dev,*ind))
			*ind = 0;
	}
	ind = (unsigned long *) bh->b_data;
	for (i = 0; i < 256; i++)
		if (*(ind++))
			break;
	brelse(bh);
	if (i >= 256) {
		result = 1;
		if (ext_free_block(inode->i_dev,*p))
			*p = 0;
	}
	return result;
}
		
static int trunc_dindirect(struct inode * inode)
{
	int i;
	struct buffer_head * bh = NULL;
	unsigned long * dind;
	int result = 0;
#define DINDIRECT_BLOCK ((DIRECT_BLOCK-(256+9))>>8)

	if (inode->i_data[10])
		bh = bread(inode->i_dev,inode->i_data[10]);
	if (!bh)
		return 0;
repeat:
	for (i = DINDIRECT_BLOCK ; i < 256 ; i ++) {
		if (i < 0)
			i = 0;
		if (i < DINDIRECT_BLOCK)
			goto repeat;
		dind = i+(unsigned long *) bh->b_data;
		if (!*dind)
			continue;
		result |= trunc_indirect(inode,9+256+(i<<8),dind);
	}
	dind = (unsigned long *) bh->b_data;
	for (i = 0; i < 256; i++)
		if (*(dind++))
			break;
	brelse(bh);
	if (i >= 256) {
		result = 1;
		if (ext_free_block(inode->i_dev,inode->i_data[10]))
			inode->i_data[10] = 0;
	}
	return result;
}
		
void ext_truncate(struct inode * inode)
{
	int flag;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		return;
/*	if (inode->i_data[7] & 0xffff0000)
		printk("BAD! ext inode has 16 high bits set\n"); */
	while (1) {
		flag = trunc_direct(inode);
		flag |= trunc_indirect(inode,9,(unsigned long *)&inode->i_data[9]);
		flag |= trunc_dindirect(inode);
		if (!flag)
			break;
		current->counter = 0;
		schedule();
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_dirt = 1;
}

/*
 * Called when a inode is released. Note that this is different
 * from ext_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
void ext_release(struct inode * inode, struct file * filp)
{
	printk("ext_release not implemented\n");
}
