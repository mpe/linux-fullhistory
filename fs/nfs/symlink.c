/*
 *  linux/fs/nfs/symlink.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  nfs symlink handling code
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/nfs_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>

static int nfs_readlink(struct inode *, char *, int);
static int nfs_follow_link(struct inode *, struct inode *, int, int,
			   struct inode **);

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
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int nfs_follow_link(struct inode *dir, struct inode *inode,
			   int flag, int mode, struct inode **res_inode)
{
	int error, *mem;
	unsigned int len;
	char *res, *res2;

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
	error = nfs_proc_readlink(NFS_SERVER(inode), NFS_FH(inode), &mem,
		&res, &len, NFS_MAXPATHLEN);
	if (error) {
		iput(inode);
		iput(dir);
		kfree(mem);
		return error;
	}
	while ((res2 = (char *) kmalloc(NFS_MAXPATHLEN + 1, GFP_NFS)) == NULL) {
		schedule();
	}
	memcpy(res2, res, len);
	res2[len] = '\0';
	kfree(mem);
	iput(inode);
	current->link_count++;
	error = open_namei(res2, flag, mode, res_inode, dir);
	current->link_count--;
	kfree_s(res2, NFS_MAXPATHLEN + 1);
	return error;
}

static int nfs_readlink(struct inode *inode, char *buffer, int buflen)
{
	int error, *mem;
	unsigned int len;
	char *res;

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	if (buflen > NFS_MAXPATHLEN)
		buflen = NFS_MAXPATHLEN;
	error = nfs_proc_readlink(NFS_SERVER(inode), NFS_FH(inode), &mem,
		&res, &len, buflen);
	iput(inode);
	if (! error) {
		memcpy_tofs(buffer, res, len);
		put_fs_byte('\0', buffer + len);
		error = len;
	}
	kfree(mem);
	return error;
}
