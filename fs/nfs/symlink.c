/*
 *  linux/fs/nfs/symlink.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  Jun 7 1999, cache symlink lookups in the page cache.  -DaveM
 *
 *  nfs symlink handling code
 */

#define NFS_NEED_XDR_TYPES
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs.h>
#include <linux/pagemap.h>
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
	NULL,			/* bmap */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

/* Symlink caching in the page cache is even more simplistic
 * and straight-forward than readdir caching.
 */
static struct page *try_to_get_symlink_page(struct dentry *dentry, struct inode *inode)
{
	struct nfs_readlinkargs rl_args;
	struct page *page, **hash;
	unsigned long page_cache;

	page = NULL;
	page_cache = page_cache_alloc();
	if (!page_cache)
		goto out;

	hash = page_hash(inode, 0);
repeat:
	page = __find_lock_page(inode, 0, hash);
	if (page) {
		page_cache_free(page_cache);
		goto unlock_out;
	}

	page = page_cache_entry(page_cache);
	if (add_to_page_cache_unique(page, inode, 0, hash)) {
		page_cache_release(page);
		goto repeat;
	}

	/* We place the length at the beginning of the page,
	 * in host byte order, followed by the string.  The
	 * XDR response verification will NULL terminate it.
	 */
	rl_args.fh = NFS_FH(dentry);
	rl_args.buffer = (const void *)page_cache;
	if (rpc_call(NFS_CLIENT(inode), NFSPROC_READLINK,
		     &rl_args, NULL, 0) < 0)
		goto error;
	SetPageUptodate(page);
unlock_out:
	UnlockPage(page);
out:
	return page;

error:
	SetPageError(page);
	goto unlock_out;
}

static int nfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct page *page;
	u32 *p, len;

	/* Caller revalidated the directory inode already. */
	page = find_get_page(inode, 0);
	if (!page)
		goto no_readlink_page;
	if (!Page_Uptodate(page))
		goto readlink_read_error;
success:
	p = (u32 *) page_address(page);
	len = *p++;
	if (len > buflen)
		len = buflen;
	copy_to_user(buffer, p, len);
	page_cache_release(page);
	return len;

no_readlink_page:
	page = try_to_get_symlink_page(dentry, inode);
	if (!page)
		goto no_page;
	if (Page_Uptodate(page))
		goto success;
readlink_read_error:
	page_cache_release(page);
no_page:
	return -EIO;
}

static struct dentry *
nfs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow)
{
	struct dentry *result;
	struct inode *inode = dentry->d_inode;
	struct page *page;
	u32 *p;

	/* Caller revalidated the directory inode already. */
	page = find_get_page(inode, 0);
	if (!page)
		goto no_followlink_page;
	if (!Page_Uptodate(page))
		goto followlink_read_error;
success:
	p = (u32 *) page_address(page);
	result = lookup_dentry((char *) (p + 1), base, follow);
	page_cache_release(page);
	return result;

no_followlink_page:
	page = try_to_get_symlink_page(dentry, inode);
	if (!page)
		goto no_page;
	if (Page_Uptodate(page))
		goto success;
followlink_read_error:
	page_cache_release(page);
no_page:
	return ERR_PTR(-EIO);
}
