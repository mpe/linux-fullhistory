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

static int nfs_readlink(struct dentry *, char *, int);
static struct dentry *nfs_follow_link(struct dentry *, struct dentry *, unsigned int);

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

static int nfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	int error;
	unsigned int len;
	char *res;
	void *mem;

	dfprintk(VFS, "nfs: readlink(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	if (buflen > NFS_MAXPATHLEN)
		buflen = NFS_MAXPATHLEN;
	error = nfs_proc_readlink(NFS_DSERVER(dentry), NFS_FH(dentry),
					&mem, &res, &len, buflen);
	if (! error) {
		copy_to_user(buffer, res, len);
		put_user('\0', buffer + len);
		error = len;
		kfree(mem);
	}
	return error;
}

static struct dentry *
nfs_follow_link(struct dentry * dentry, struct dentry *base, unsigned int follow)
{
	int error;
	unsigned int len;
	char *res;
	void *mem;
	char *path;
	struct dentry *result;

	dfprintk(VFS, "nfs: follow_link(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = nfs_proc_readlink(NFS_DSERVER(dentry), NFS_FH(dentry),
				 &mem, &res, &len, NFS_MAXPATHLEN);
	result = ERR_PTR(error);
	if (error)
		goto out_dput;

	result = ERR_PTR(-ENOMEM);
	path = kmalloc(len + 1, GFP_KERNEL);
	if (!path)
		goto out_mem;
	memcpy(path, res, len);
	path[len] = 0;
	kfree(mem);

	result = lookup_dentry(path, base, follow);
	kfree(path);
out:
	return result;

out_mem:
	kfree(mem);
out_dput:
	dput(base);
	goto out;
}
