/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/devpts/root.c
 *
 *  Copyright 1998 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/string.h>
#include "devpts_i.h"

static int devpts_root_readdir(struct file *,void *,filldir_t);
static int devpts_root_lookup(struct inode *,struct dentry *);
static int devpts_revalidate(struct dentry *);

static struct file_operations devpts_root_operations = {
	NULL,                   /* llseek */
	NULL,                   /* read */
	NULL,                   /* write */
	devpts_root_readdir,    /* readdir */
	NULL,                   /* poll */
	NULL,			/* ioctl */
	NULL,                   /* mmap */
	NULL,                   /* open */
	NULL,			/* flush */
	NULL,                   /* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
	NULL			/* lock */
};

struct inode_operations devpts_root_inode_operations = {
	&devpts_root_operations, /* file operations */
	NULL,                   /* create */
	devpts_root_lookup,     /* lookup */
	NULL,                   /* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,                   /* mknod */
	NULL,                   /* rename */
	NULL,                   /* readlink */
	NULL,                   /* follow_link */
	NULL,                   /* readpage */
	NULL,                   /* writepage */
	NULL,                   /* bmap */
	NULL,                   /* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL			/* revalidate */
};

static struct dentry_operations devpts_dentry_operations = {
	devpts_revalidate,	/* d_revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
};

/*
 * The normal naming convention is simply /dev/pts/<number>; this conforms
 * to the System V naming convention
 */

#define genptsname(buf,num) sprintf(buf, "%d", num)

static int devpts_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode * inode = filp->f_dentry->d_inode;
	struct devpts_sb_info * sbi = SBI(filp->f_dentry->d_inode->i_sb);
	off_t nr;
	char numbuf[16];

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		while ( nr < sbi->max_ptys ) {
			int ptynr = nr - 2;
			if ( sbi->inodes[ptynr] ) {
				genptsname(numbuf, ptynr);
				if ( filldir(dirent, numbuf, strlen(numbuf), nr, nr) < 0 )
					return 0;
			}
			filp->f_pos = ++nr;
		}
		break;
	}

	return 0;
}

/*
 * Revalidate is called on every cache lookup.  We use it to check that
 * the pty really does still exist.  Never revalidate negative dentries;
 * for simplicity (fix later?)
 */
static int devpts_revalidate(struct dentry * dentry)
{
	struct devpts_sb_info *sbi;

	if ( !dentry->d_inode )
		return 0;

	sbi = SBI(dentry->d_inode->i_sb);

	return ( sbi->inodes[dentry->d_inode->i_ino - 2] == dentry->d_inode );
}

static int devpts_root_lookup(struct inode * dir, struct dentry * dentry)
{
	struct devpts_sb_info *sbi = SBI(dir->i_sb);
	unsigned int entry;
	int i;
	const char *p;

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	dentry->d_inode = NULL;	/* Assume failure */
	dentry->d_op    = &devpts_dentry_operations;

	if ( dentry->d_name.len == 1 && dentry->d_name.name[0] == '0' ) {
		entry = 0;
	} else if ( dentry->d_name.len < 1 ) {
		return 0;
	} else {
		p = dentry->d_name.name;
		if ( *p < '1' || *p > '9' )
			return 0;
		entry = *p++ - '0';

		for ( i = dentry->d_name.len-1 ; i ; i-- ) {
			unsigned int nentry = *p++ - '0';
			if ( nentry > 9 )
				return 0;
			nentry += entry * 10;
			if (nentry < entry)
				return 0;
			entry = nentry;
		}
	}

	if ( entry >= sbi->max_ptys )
		return 0;

	dentry->d_inode = sbi->inodes[entry];
	if ( dentry->d_inode )
		dentry->d_inode->i_count++;
	
	d_add(dentry, dentry->d_inode);

	return 0;
}
