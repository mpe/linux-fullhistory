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

static int		 affs_readlink(struct dentry *, char *, int);
static struct dentry	*affs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int);

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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static int
affs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head	*bh;
	struct slink_front	*lf;
	int			 i, j;
	char			 c;
	char			 lc;
	char			*pf;

	pr_debug("AFFS: readlink(ino=%lu,buflen=%d)\n",inode->i_ino,buflen);

	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	if (!bh) {
		affs_warning(inode->i_sb,"follow_link","Unable to read i-node block %lu\n",
				inode->i_ino);
		return -EIO;
	}
	lf = (struct slink_front *)bh->b_data;
	lc = 0;
	i  = 0;
	j  = 0;
	pf = inode->i_sb->u.affs_sb.s_prefix ? inode->i_sb->u.affs_sb.s_prefix : "/";
	
	if (strchr(lf->symname,':')) {		/* Handle assign or volume name */
		while (i < buflen && (c = pf[i])) {
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
	affs_brelse(bh);
	return i;
}

static struct dentry *
affs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head	*bh;
	struct slink_front	*lf;
	char			*buffer;
	int			 i, j;
	char			 c;
	char			 lc;
	char			*pf;

	pr_debug("AFFS: follow_link(ino=%lu)\n",inode->i_ino);

	if (!(buffer = kmalloc(1024,GFP_KERNEL))) {
		dput(base);
		return ERR_PTR(-ENOSPC);
	}
	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	if (!bh) {
		affs_warning(inode->i_sb,"follow_link","Unable to read i-node block %lu\n",
				inode->i_ino);
		kfree(buffer);
		dput(base);
		return ERR_PTR(-EIO);
	}
	i  = 0;
	j  = 0;
	lf = (struct slink_front *)bh->b_data;
	lc = 0;
	pf = inode->i_sb->u.affs_sb.s_prefix ? inode->i_sb->u.affs_sb.s_prefix : "/";

	if (strchr(lf->symname,':')) {		/* Handle assign or volume name */
		while (i < 1023 && (c = pf[i]))
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
	base = lookup_dentry(buffer,base,follow);
	kfree(buffer);
	return base;
}

