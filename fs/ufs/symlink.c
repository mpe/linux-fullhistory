/*
 *  linux/fs/ufs/symlink.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@emai.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/symlink.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 symlink handling code
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/stat.h>


#undef UFS_SYMLINK_DEBUG

#ifdef UFS_SYMLINK_DEBUG
#define UFSD(x) printk("(%s, %d), %s:", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif


static struct dentry * ufs_follow_link(struct dentry * dentry,
	struct dentry * base, unsigned int follow)
{
	struct inode * inode;
	struct buffer_head * bh;
	int error;
	char * link;

	UFSD(("ENTER\n"))

	inode = dentry->d_inode;
	bh = NULL;
	/* slow symlink */	    
	if (inode->i_blocks) {
		if (!(bh = ufs_bread (inode, 0, 0, &error))) {
			dput(base);
			return ERR_PTR(-EIO);
		}
		link = bh->b_data;
	}
	/* fast symlink */
	else {
		link = (char *) inode->u.ufs_i.i_u1.i_symlink;
	}
	UPDATE_ATIME(inode);
	base = lookup_dentry(link, base, follow);
	if (bh)
		brelse(bh);
	UFSD(("EXIT\n"))
	return base;
}

static int ufs_readlink (struct dentry * dentry, char * buffer, int buflen)
{
	struct super_block * sb;
	struct inode * inode;
	struct buffer_head * bh;
	char * link;
	int i;

	UFSD(("ENTER\n"))

	inode = dentry->d_inode;
	sb = inode->i_sb;
	bh = NULL;
	if (buflen > sb->s_blocksize - 1)
		buflen = sb->s_blocksize - 1;
	/* slow symlink */
	if (inode->i_blocks) {
		int err;
		bh = ufs_bread (inode, 0, 0, &err);
		if (!bh) {
			if(err < 0) /* indicate type of error */
				return err;
			return 0;
		}
		link = bh->b_data;
	}
	/* fast symlink */
	else {
		link = (char *) inode->u.ufs_i.i_u1.i_symlink;
	}
	i = 0;
	while (i < buflen && link[i])
		i++;
	if (copy_to_user(buffer, link, i))
		i = -EFAULT;
	UPDATE_ATIME(inode);
	if (bh)
		brelse (bh);
	UFSD(("ENTER\n"))
	return i;
}

struct inode_operations ufs_symlink_inode_operations = {
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
	ufs_readlink,		/* readlink */
	ufs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};
