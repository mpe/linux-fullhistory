/*
 *  linux/fs/nfs/symlink.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  nfs symlink handling code
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/nfs_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>

#include <asm/uaccess.h>

static int nfs_readlink(struct inode *, char *, int);
static struct dentry *nfs_follow_link(struct inode *, struct dentry *);

/*
 * symlinks can't do much...
 */
struct inode_operations nfs_symlink_inode_operations = {
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
	nfs_readlink,		/* readlink */
	nfs_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int nfs_readlink(struct inode *inode, char *buffer, int buflen)
{
	int error;
	unsigned int len;
	char *res;
	void *mem;

	dfprintk(VFS, "nfs: readlink(%x/%ld)\n", inode->i_dev, inode->i_ino);

	if (buflen > NFS_MAXPATHLEN)
		buflen = NFS_MAXPATHLEN;
	error = nfs_proc_readlink(NFS_SERVER(inode), NFS_FH(inode), &mem,
		&res, &len, buflen);
	if (! error) {
		copy_to_user(buffer, res, len);
		put_user('\0', buffer + len);
		error = len;
	}
	kfree(mem);
	return error;
}

static struct dentry * nfs_follow_link(struct inode * inode, struct dentry *base)
{
	int error;
	unsigned int len;
	char *res;
	void *mem;
	char *path;

	dfprintk(VFS, "nfs: follow_link(%x/%ld)\n", inode->i_dev, inode->i_ino);

	error = nfs_proc_readlink(NFS_SERVER(inode), NFS_FH(inode), &mem,
		&res, &len, NFS_MAXPATHLEN);

	if (error) {
		dput(base);
		kfree(mem);
		return ERR_PTR(error);
	}
	path = kmalloc(len + 1, GFP_KERNEL);
	if (!path) {
		dput(base);
		kfree(mem);
		return ERR_PTR(-ENOMEM);
	}
	memcpy(path, res, len);
	path[len] = 0;
	kfree(mem);

	base = lookup_dentry(path, base, 1);
	kfree(path);
	return base;
}
