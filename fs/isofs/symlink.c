/*
 *  linux/fs/isofs/symlink.c
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  isofs symlink handling code.  This is only used with the Rock Ridge
 *  extensions to iso9660
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/iso_fs.h>
#include <linux/stat.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>

static int isofs_readlink(struct dentry *, char *, int);
static struct dentry * isofs_follow_link(struct dentry *, struct dentry *, unsigned int);

/*
 * symlinks can't do much...
 */
struct inode_operations isofs_symlink_inode_operations = {
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
	isofs_readlink,		/* readlink */
	isofs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int isofs_readlink(struct dentry * dentry, char * buffer, int buflen)
{
        char * pnt;
	int i;

	if (buflen > 1023)
		buflen = 1023;
	pnt = get_rock_ridge_symlink(dentry->d_inode);

	if (!pnt)
		return 0;

	i = strlen(pnt);
	if (i > buflen)
		i = buflen; 
	if (copy_to_user(buffer, pnt, i))
		i = -EFAULT; 	
	kfree(pnt);
	return i;
}

static struct dentry * isofs_follow_link(struct dentry * dentry,
					struct dentry *base,
					unsigned int follow)
{
	char * pnt;

	pnt = get_rock_ridge_symlink(dentry->d_inode);
	if(!pnt) {
		dput(base);
		return ERR_PTR(-ELOOP);
	}

	base = lookup_dentry(pnt, base, follow);

	kfree(pnt);
	return base;
}
