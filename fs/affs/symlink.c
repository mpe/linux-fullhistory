/*
 *  linux/fs/affs/symlink.c
 *
 *  1995  Hans-Joachim Widmaier - Modified for affs.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  affs symlink handling code
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/affs_fs.h>
#include <linux/amigaffs.h>
#include <asm/segment.h>

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

static int affs_readlink(struct inode *, char *, int);
static int affs_follow_link(struct inode *, struct inode *, int, int, struct inode **);

struct inode_operations affs_symlink_inode_operations = {
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
	affs_readlink,		/* readlink */
	affs_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int
affs_follow_link(struct inode *dir, struct inode *inode, int flag, int mode,
		 struct inode **res_inode)
{
	struct buffer_head	*bh;
	struct slink_front	*lf;
	char			*buffer;
	int			 error;
	int			 i, j;
	char			 c;
	char			 lc;

	pr_debug("AFFS: follow_link(ino=%lu)\n",inode->i_ino);

	*res_inode = NULL;
	if (!dir) {
		dir = current->fs->root;
		dir->i_count++;
	}
	if (!inode) {
		iput(dir);
		return -ENOENT;
	}
	if (!S_ISLNK(inode->i_mode)) {
		iput(dir);
		*res_inode = inode;
		return 0;
	}
	if (current->link_count > 5) {
		iput(inode);
		iput(dir);
		return -ELOOP;
	}
	if (!(buffer = kmalloc(1024,GFP_KERNEL))) {
		iput(inode);
		iput(dir);
		return -ENOSPC;
	}
	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	i  = 0;
	j  = 0;
	if (!bh) {
		printk("AFFS: unable to read i-node block %lu\n",inode->i_ino);
		kfree(buffer);
		iput(inode);
		iput(dir);
		return -EIO;
	}
	lf = (struct slink_front *)bh->b_data;
	lc = 0;
	if (strchr(lf->symname,':')) {		/* Handle assign or volume name */
		while (i < 1023 && (c = inode->i_sb->u.affs_sb.s_prefix[i]))
			buffer[i++] = c;
		while (i < 1023 && lf->symname[j] != ':')
			buffer[i++] = lf->symname[j++];
		if (i < 1023)
			 buffer[i++] = '/';
		j++;
		lc = '/';
	}
	while (i < 1023 && (c = lf->symname[j])) {
		if (c == '/' && lc == '/' && i < 1020) {	/* parent dir */
			buffer[i++] = '.';
			buffer[i++] = '.';
		}
		buffer[i++] = c;
		lc = c;
		j++;
	}
	buffer[i] = '\0';
	affs_brelse(bh);
	iput(inode);
	current->link_count++;
	error = open_namei(buffer,flag,mode,res_inode,dir);
	current->link_count--;
	kfree(buffer);
	return error;
}

static int
affs_readlink(struct inode *inode, char *buffer, int buflen)
{
	struct buffer_head	*bh;
	struct slink_front	*lf;
	int			 i, j;
	char			 c;
	char			 lc;

	pr_debug("AFFS: readlink(ino=%lu,buflen=%d)\n",inode->i_ino,buflen);

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	i  = 0;
	j  = 0;
	if (!bh) {
		printk("AFFS: unable to read i-node block %lu\n",inode->i_ino);
		goto symlink_end;
	}
	lf = (struct slink_front *)bh->b_data;
	lc = 0;
	
	if (strchr(lf->symname,':')) {		/* Handle assign or volume name */
		while (i < buflen && (c = inode->i_sb->u.affs_sb.s_prefix[i])) {
			put_user(c,buffer++);
			i++;
		}
		while (i < buflen && (c = lf->symname[j]) != ':') {
			put_user(c,buffer++);
			i++, j++;
		}
		if (i < buflen) {
			put_user('/',buffer++);
			i++, j++;
		}
		lc = '/';
	}
	while (i < buflen && (c = lf->symname[j])) {
		if (c == '/' && lc == '/' && (i + 3 < buflen)) {	/* parent dir */
			put_user('.',buffer++);
			put_user('.',buffer++);
			i += 2;
		}
		put_user(c,buffer++);
		lc = c;
		i++, j++;
	}
symlink_end:
	iput(inode);
	affs_brelse(bh);
	return i;
}
