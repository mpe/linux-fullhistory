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
	iput(inode);
	if (! error) {
		copy_to_user(buffer, res, len);
		put_user('\0', buffer + len);
		error = len;
	}
	kfree(mem);
	return error;
}
