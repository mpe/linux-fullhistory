/*
 * symlink.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/efs_fs.h>

static int efs_readlink(struct dentry *, char *, int);
static struct dentry * efs_follow_link(struct dentry *, struct dentry *, unsigned int);

struct inode_operations efs_symlink_inode_operations = {
	NULL,			/* no symlink file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	efs_readlink,		/* readlink */
	efs_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

static char *efs_linktarget(struct inode *in, int *len) {
	char *name;
	struct buffer_head * bh;
	efs_block_t size = in->i_size;
  
	if (size > 2 * EFS_BLOCKSIZE) {
		printk(KERN_ERR "EFS: linktarget(): name too long: %lu\n", in->i_size);
		return NULL;
	}
  
	if (!(name = kmalloc(size + 1, GFP_KERNEL)))
		return NULL;
  
	/* read first 512 bytes of link target */
	bh = bread(in->i_dev, efs_bmap(in, 0), EFS_BLOCKSIZE);
	if (!bh) {
		kfree(name);
		printk(KERN_ERR "EFS: linktarget(): couldn't read block %d\n", efs_bmap(in, 0));
		return NULL;
	}

	memcpy(name, bh->b_data, (size > EFS_BLOCKSIZE) ? EFS_BLOCKSIZE : size);
	brelse(bh);

	if (size > EFS_BLOCKSIZE) {
		bh = bread(in->i_dev, efs_bmap(in, 1), EFS_BLOCKSIZE);
		if (!bh) {
			kfree(name);
			printk(KERN_ERR "EFS: linktarget(): couldn't read block %d\n", efs_bmap(in, 1));
			return NULL;
		}
		memcpy(name + EFS_BLOCKSIZE, bh->b_data, size - EFS_BLOCKSIZE);
		brelse(bh);
	}
  
	name[size] = (char) 0;
	if (len) *len = size;

	return name;
}

static struct dentry *efs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow) {
	char *name;
	struct inode *inode = dentry->d_inode;

	if (!(name = efs_linktarget(inode, NULL))) {
		dput(base);
		return ERR_PTR(-ELOOP);
	}
	base = lookup_dentry(name, base, follow);
	kfree(name);
  
	return base;
}

static int efs_readlink(struct dentry * dir, char * buf, int bufsiz) {
	int rc;
	char *name;
	struct inode *inode = dir->d_inode;
  
	if (!(name = efs_linktarget(inode, &bufsiz))) return 0;
	rc = copy_to_user(buf, name, bufsiz) ? -EFAULT : bufsiz;
	kfree(name);

	return rc;
}

