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
#include <asm/uaccess.h>

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

static int affs_readlink(struct inode *, char *, int);

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
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int
affs_readlink(struct inode *inode, char *buffer, int buflen)
{
	struct buffer_head	*bh;
	struct slink_front	*lf;
	int			 i, j;
	char			 c;
	char			 lc;

	pr_debug("AFFS: readlink(ino=%lu,buflen=%d)\n",inode->i_ino,buflen);

	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	i  = 0;
	j  = 0;
	if (!bh) {
		affs_error(inode->i_sb,"readlink","Cannot read block %lu\n",inode->i_ino);
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
