/*
 *  linux/fs/ext/symlink.c
 *
 *  Copyright (C) 1992 Remy Card (card@masi.ibp.fr)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext symlink handling code
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/ext_fs.h>
#include <linux/stat.h>

static int ext_readlink(struct inode *, char *, int);
static struct inode * ext_follow_link(struct inode *, struct inode *);

/*
 * symlinks can't do much...
 */
struct inode_operations ext_symlink_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	ext_readlink,		/* readlink */
	ext_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL			/* truncate */
};

static struct inode * ext_follow_link(struct inode * dir, struct inode * inode)
{
	unsigned short fs;
	struct buffer_head * bh;

	if (!dir) {
		dir = current->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return NULL;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		return inode;
	}
	__asm__("mov %%fs,%0":"=r" (fs));
	if ((current->link_count > 5) || !inode->i_data[0] ||
	   !(bh = bread(inode->i_dev, inode->i_data[0], BLOCK_SIZE))) {
		iput(dir);
		iput(inode);
		return NULL;
	}
	iput(inode);
	__asm__("mov %0,%%fs"::"r" ((unsigned short) 0x10));
	current->link_count++;
	inode = _namei(bh->b_data,dir,1);
	current->link_count--;
	__asm__("mov %0,%%fs"::"r" (fs));
	brelse(bh);
	return inode;
}

static int ext_readlink(struct inode * inode, char * buffer, int buflen)
{
	struct buffer_head * bh;
	int i;
	char c;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	if (buflen > 1023)
		buflen = 1023;
	if (inode->i_data[0])
		bh = bread(inode->i_dev, inode->i_data[0], BLOCK_SIZE);
	else
		bh = NULL;
	iput(inode);
	if (!bh)
		return 0;
	i = 0;
	while (i<buflen && (c = bh->b_data[i])) {
		i++;
		put_fs_byte(c,buffer++);
	}
	brelse(bh);
	return i;
}
