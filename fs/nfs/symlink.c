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

/* Symlink caching in the page cache is even more simplistic
 * and straight-forward than readdir caching.
 */
static int nfs_symlink_filler(struct dentry *dentry, struct page *page)
{
	struct nfs_readlinkargs rl_args;
	kmap(page);
	/* We place the length at the beginning of the page,
	 * in host byte order, followed by the string.  The
	 * XDR response verification will NULL terminate it.
	 */
	rl_args.fh = NFS_FH(dentry);
	rl_args.buffer = (const void *)page_address(page);
	if (rpc_call(NFS_CLIENT(dentry->d_inode), NFSPROC_READLINK,
		     &rl_args, NULL, 0) < 0)
		goto error;
	SetPageUptodate(page);
	kunmap(page);
	UnlockPage(page);
	return 0;

error:
	SetPageError(page);
	kunmap(page);
	UnlockPage(page);
	return -EIO;
}

static char *nfs_getlink(struct dentry *dentry, struct page **ppage)
{
	struct inode *inode = dentry->d_inode;
	struct page *page;
	u32 *p;

	/* Caller revalidated the directory inode already. */
	page = read_cache_page(&inode->i_data, 0,
				(filler_t *)nfs_symlink_filler, dentry);
	if (IS_ERR(page))
		goto read_failed;
	if (!Page_Uptodate(page))
		goto getlink_read_error;
	*ppage = page;
	p = (u32 *) kmap(page);
	return (char*)(p+1);
		
getlink_read_error:
	page_cache_release(page);
	return ERR_PTR(-EIO);
read_failed:
	return (char*)page;
}

static int nfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct page *page = NULL;
	int res = vfs_readlink(dentry,buffer,buflen,nfs_getlink(dentry,&page));
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	return res;
}

static struct dentry *
nfs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow)
{
	struct page *page = NULL;
	struct dentry *res = vfs_follow_link(dentry, base, follow,
					     nfs_getlink(dentry, &page));
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	return res;
}

/*
 * symlinks can't do much...
 */
struct inode_operations nfs_symlink_inode_operations = {
	readlink:	nfs_readlink,
	follow_link:	nfs_follow_link,
	revalidate:	nfs_revalidate,
	setattr:	nfs_notify_change,
};
