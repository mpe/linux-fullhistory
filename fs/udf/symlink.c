/*
 * symlink.c
 *
 * PURPOSE
 *	Symlink handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-1999 Ben Fennema
 *  (C) 1999 Stelias Computing Inc 
 *
 * HISTORY
 *
 *  04/16/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/udf_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include "udf_i.h"

static int udf_readlink(struct dentry *, char *, int);
static struct dentry * udf_follow_link(struct dentry * dentry,
	struct dentry * base, unsigned int follow);

/*
 * symlinks can't do much...
 */
struct inode_operations udf_symlink_inode_operations = {
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
    udf_readlink,	/* readlink */
    udf_follow_link,/* follow_link */
	NULL,			/* get_block */
    NULL,			/* readpage */
    NULL,			/* writepage */
	NULL,			/* flushpage */
    NULL,			/* truncate */
    NULL,			/* permission */
    NULL,			/* smap */
	NULL			/* revalidate */
};

int udf_pc_to_char(char *from, int fromlen, char **to)
{
	struct PathComponent *pc;
	int elen = 0, len = 0;

	*to = (char *)kmalloc(fromlen, GFP_KERNEL);

	if (!(*to))
		return -1;

	while (elen < fromlen)
	{
		pc = (struct PathComponent *)(from + elen);
		if (pc->componentType == 1 && pc->lengthComponentIdent == 0)
		{
			(*to)[0] = '/';
			len = 1;
		}
		else if (pc->componentType == 3)
		{
			memcpy(&(*to)[len], "../", 3);
			len += 3;
		}
        else if (pc->componentType == 4)
		{
			memcpy(&(*to)[len], "./", 2);
			len += 2;
		}
		else if (pc->componentType == 5)
		{
			memcpy(&(*to)[len], pc->componentIdent, pc->lengthComponentIdent);
			len += pc->lengthComponentIdent + 1;
			(*to)[len-1] = '/';
		}
		elen += sizeof(struct PathComponent) + pc->lengthComponentIdent;
	}

	if (len)
	{
		len --;
		(*to)[len] = '\0';
	}
	return len;
}

static struct dentry * udf_follow_link(struct dentry * dentry,
	struct dentry * base, unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh = NULL;
	char *symlink, *tmpbuf;
	int len;
	
	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
	{
		bh = udf_tread(inode->i_sb, inode->i_ino, inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data + udf_file_entry_alloc_offset(inode);
	}
	else
	{
		bh = bread(inode->i_dev, udf_block_map(inode, 0), inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data;
	}

	if ((len = udf_pc_to_char(symlink, inode->i_size, &tmpbuf)) >= 0)
	{
		base = lookup_dentry(tmpbuf, base, follow);
		kfree(tmpbuf);
		return base;
	}
	else
		return ERR_PTR(-ENOMEM);
}

static int udf_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh = NULL;
	char *symlink, *tmpbuf;
	int len;
	
	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
	{
		bh = udf_tread(inode->i_sb, inode->i_ino, inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data + udf_file_entry_alloc_offset(inode);
	}
	else
	{
		bh = bread(inode->i_dev, udf_block_map(inode, 0), inode->i_sb->s_blocksize);

		if (!bh)
			return 0;

		symlink = bh->b_data;
	}

	if ((len = udf_pc_to_char(symlink, inode->i_size, &tmpbuf)) >= 0)
	{
		if (copy_to_user(buffer, tmpbuf, len > buflen ? buflen : len))
			len = -EFAULT;
		kfree(tmpbuf);
	}
	else
		len = -ENOMEM;

	UPDATE_ATIME(inode);
	if (bh)
		udf_release_data(bh);
	return len;
}
