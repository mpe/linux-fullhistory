/*
 *  linux/fs/nfs/dir.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs directory handling functions
 *
 * 10 Apr 1996	Added silly rename for unlink	--okir
 * 28 Sep 1996	Improved directory cache --okir
 * 23 Aug 1997  Claus Heine claus@momo.math.rwth-aachen.de 
 *              Re-implemented silly rename for unlink, newly implemented
 *              silly rename for nfs_rename() following the suggestions
 *              of Olaf Kirch (okir) found in this file.
 *              Following Linus comments on my original hack, this version
 *              depends only on the dcache stuff and doesn't touch the inode
 *              layer (iput() and friends).
 *  6 Jun 1999	Cache readdir lookups in the page cache. -DaveM
 */

#define NFS_NEED_XDR_TYPES
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs.h>
#include <linux/pagemap.h>

#include <asm/segment.h>	/* for fs functions */

#define NFS_PARANOIA 1
/* #define NFS_DEBUG_VERBOSE 1 */

static int nfs_safe_remove(struct dentry *);

static ssize_t nfs_dir_read(struct file *, char *, size_t, loff_t *);
static int nfs_readdir(struct file *, void *, filldir_t);
static struct dentry *nfs_lookup(struct inode *, struct dentry *);
static int nfs_create(struct inode *, struct dentry *, int);
static int nfs_mkdir(struct inode *, struct dentry *, int);
static int nfs_rmdir(struct inode *, struct dentry *);
static int nfs_unlink(struct inode *, struct dentry *);
static int nfs_symlink(struct inode *, struct dentry *, const char *);
static int nfs_link(struct dentry *, struct inode *, struct dentry *);
static int nfs_mknod(struct inode *, struct dentry *, int, int);
static int nfs_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *);

static struct file_operations nfs_dir_operations = {
	NULL,			/* lseek - default */
	nfs_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	nfs_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	nfs_open,		/* open */
	NULL,			/* flush */
	nfs_release,		/* release */
	NULL			/* fsync */
};

struct inode_operations nfs_dir_inode_operations = {
	&nfs_dir_operations,	/* default directory file-ops */
	nfs_create,		/* create */
	nfs_lookup,		/* lookup */
	nfs_link,		/* link */
	nfs_unlink,		/* unlink */
	nfs_symlink,		/* symlink */
	nfs_mkdir,		/* mkdir */
	nfs_rmdir,		/* rmdir */
	nfs_mknod,		/* mknod */
	nfs_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	nfs_revalidate,		/* revalidate */
};

static ssize_t
nfs_dir_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EISDIR;
}

/* Each readdir response is composed of entries which look
 * like the following, as per the NFSv2 RFC:
 *
 *	__u32	not_end			zero if end of response
 *	__u32	file ID			opaque ino_t
 *	__u32	namelen			size of name string
 *	VAR	name string		the string, padded to modulo 4 bytes
 *	__u32	cookie			opaque ID of next entry
 *
 * When you hit not_end being zero, the next __u32 is non-zero if
 * this is the end of the complete set of readdir entires for this
 * directory.  This can be used, for example, to initiate pre-fetch.
 *
 * In order to know what to ask the server for, we only need to know
 * the final cookie of the previous page, and offset zero has cookie
 * zero, so we cache cookie to page offset translations in chunks.
 */
#define COOKIES_PER_CHUNK (8 - ((sizeof(void *) / sizeof(__u32))))
struct nfs_cookie_table {
	struct nfs_cookie_table *next;
	__u32	cookies[COOKIES_PER_CHUNK];
};
static kmem_cache_t *nfs_cookie_cachep;

/* This whole scheme relies on the fact that dirent cookies
 * are monotonically increasing.
 *
 * Another invariant is that once we have a valid non-zero
 * EOF marker cached, we also have the complete set of cookie
 * table entries.
 *
 * We return the page offset assosciated with the page where
 * cookie must be if it exists at all, however if we can not
 * figure that out conclusively, we return < 0.
 */
static long __nfs_readdir_offset(struct inode *inode, __u32 cookie)
{
	struct nfs_cookie_table *p;
	unsigned long ret = 0;

	for(p = NFS_COOKIES(inode); p != NULL; p = p->next) {
		int i;

		for (i = 0; i < COOKIES_PER_CHUNK; i++) {
			__u32 this_cookie = p->cookies[i];

			/* End of known cookies, EOF is our only hope. */
			if (!this_cookie)
				goto check_eof;

			/* Next cookie is larger, must be in previous page. */
			if (this_cookie > cookie)
				return ret;

			ret += 1;

			/* Exact cookie match, it must be in this page :-) */
			if (this_cookie == cookie)
				return ret;
		}
	}
check_eof:
	if (NFS_DIREOF(inode) != 0)
		return ret;

	return -1L;
}

static __inline__ long nfs_readdir_offset(struct inode *inode, __u32 cookie)
{
	/* Cookie zero is always at page offset zero.   Optimize the
	 * other common case since most directories fit entirely
	 * in one page.
	 */
	if (!cookie || (!NFS_COOKIES(inode) && NFS_DIREOF(inode)))
		return 0;
	return __nfs_readdir_offset(inode, cookie);
}

/* Since a cookie of zero is declared special by the NFS
 * protocol, we easily can tell if a cookie in an existing
 * table chunk is valid or not.
 *
 * NOTE: The cookies are indexed off-by-one because zero
 *       need not an entry.
 */
static __inline__ __u32 *find_cookie(struct inode *inode, unsigned long off)
{
	static __u32 cookie_zero = 0;
	struct nfs_cookie_table *p;
	__u32 *ret;

	if (!off)
		return &cookie_zero;
	off -= 1;
	p = NFS_COOKIES(inode);
	while(off >= COOKIES_PER_CHUNK && p) {
		off -= COOKIES_PER_CHUNK;
		p = p->next;
	}
	ret = NULL;
	if (p) {
		ret = &p->cookies[off];
		if (!*ret)
			ret = NULL;
	}
	return ret;
}

#define NFS_NAMELEN_ALIGN(__len) ((((__len)+3)>>2)<<2)
static int create_cookie(__u32 cookie, unsigned long off, struct inode *inode)
{
	struct nfs_cookie_table **cpp;

	cpp = (struct nfs_cookie_table **) &NFS_COOKIES(inode);
	while (off >= COOKIES_PER_CHUNK && *cpp) {
		off -= COOKIES_PER_CHUNK;
		cpp = &(*cpp)->next;
	}
	if (*cpp) {
		(*cpp)->cookies[off] = cookie;
	} else {
		struct nfs_cookie_table *new;
		int i;

		new = kmem_cache_alloc(nfs_cookie_cachep, SLAB_ATOMIC);
		if(!new)
			return -1;
		*cpp = new;
		new->next = NULL;
		for(i = 0; i < COOKIES_PER_CHUNK; i++) {
			if (i == off) {
				new->cookies[i] = cookie;
			} else {
				new->cookies[i] = 0;
			}
		}
	}
	return 0;
}

static struct page *try_to_get_dirent_page(struct file *, __u32, int);

/* Recover from a revalidation flush.  The case here is that
 * the inode for the directory got invalidated somehow, and
 * all of our cached information is lost.  In order to get
 * a correct cookie for the current readdir request from the
 * user, we must (re-)fetch older readdir page cache entries.
 *
 * Returns < 0 if some error occurrs, else it is the page offset
 * to fetch.
 */
static long refetch_to_readdir_cookie(struct file *file, struct inode *inode)
{
	struct page *page;
	u32 goal_cookie = file->f_pos;
	long cur_off, ret = -1L;

again:
	cur_off = 0;
	for (;;) {
		page = find_get_page(inode, cur_off);
		if (page) {
			if (!Page_Uptodate(page))
				goto out_error;
		} else {
			__u32 *cp = find_cookie(inode, cur_off);

			if (!cp)
				goto out_error;

			page = try_to_get_dirent_page(file, *cp, 0);
			if (!page) {
				if (!cur_off)
					goto out_error;

				/* Someone touched the dir on us. */
				goto again;
			}
		}
		page_cache_release(page);

		if ((ret = nfs_readdir_offset(inode, goal_cookie)) >= 0)
			goto out;

		cur_off += 1;
	}
out:
	return ret;

out_error:
	if (page)
		page_cache_release(page);
	goto out;
}

/* Now we cache directories properly, by stuffing the dirent
 * data directly in the page cache.
 *
 * Inode invalidation due to refresh etc. takes care of
 * _everything_, no sloppy entry flushing logic, no extraneous
 * copying, network direct to page cache, the way it was meant
 * to be.
 *
 * NOTE: Dirent information verification is done always by the
 *	 page-in of the RPC reply, nowhere else, this simplies
 *	 things substantially.
 */
static struct page *try_to_get_dirent_page(struct file *file, __u32 cookie, int refetch_ok)
{
	struct nfs_readdirargs rd_args;
	struct nfs_readdirres rd_res;
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct page *page, **hash;
	unsigned long page_cache;
	long offset;
	__u32 *cookiep;

	page = NULL;
	page_cache = page_cache_alloc();
	if (!page_cache)
		goto out;

	if ((offset = nfs_readdir_offset(inode, cookie)) < 0) {
		if (!refetch_ok ||
		    (offset = refetch_to_readdir_cookie(file, inode)) < 0) {
			page_cache_free(page_cache);
			goto out;
		}
	}

	cookiep = find_cookie(inode, offset);
	if (!cookiep) {
		/* Gross fatal error. */
		page_cache_free(page_cache);
		goto out;
	}

	hash = page_hash(inode, offset);
repeat:
	page = __find_lock_page(inode, offset, hash);
	if (page) {
		page_cache_free(page_cache);
		goto unlock_out;
	}

	page = page_cache_entry(page_cache);
	if (add_to_page_cache_unique(page, inode, offset, hash)) {
		page_cache_release(page);
		goto repeat;
	}

	rd_args.fh = NFS_FH(dentry);
	rd_res.buffer = (char *)page_cache;
	rd_res.bufsiz = PAGE_CACHE_SIZE;
	rd_res.cookie = *cookiep;
	do {
		rd_args.buffer = rd_res.buffer;
		rd_args.bufsiz = rd_res.bufsiz;
		rd_args.cookie = rd_res.cookie;
		if (rpc_call(NFS_CLIENT(inode),
			     NFSPROC_READDIR, &rd_args, &rd_res, 0) < 0)
			goto error;
	} while(rd_res.bufsiz > 0);

	if (rd_res.bufsiz < 0)
		NFS_DIREOF(inode) = rd_res.cookie;
	else if (create_cookie(rd_res.cookie, offset, inode))
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

/* Seek up to dirent assosciated with the passed in cookie,
 * then fill in dirents found.  Return the last cookie
 * actually given to the user, to update the file position.
 */
static __inline__ u32 nfs_do_filldir(__u32 *p, u32 cookie,
				     void *dirent, filldir_t filldir)
{
	u32 end;

	while((end = *p++) != 0) {
		__u32 fileid, len, skip, this_cookie;
		char *name;

		fileid = *p++;
		len = *p++;
		name = (char *) p;
		skip = NFS_NAMELEN_ALIGN(len);
		p += (skip >> 2);
		this_cookie = *p++;

		if (this_cookie < cookie)
			continue;

		cookie = this_cookie;
		if (filldir(dirent, name, len, cookie, fileid) < 0)
			break;
	}

	return cookie;
}

/* The file offset position is represented in pure bytes, to
 * make the page cache interface straight forward.
 *
 * However, some way is needed to make the connection between the
 * opaque NFS directory entry cookies and our offsets, so a per-inode
 * cookie cache table is used.
 */
static int nfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct page *page, **hash;
	long offset;
	int res;

	res = nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
	if (res < 0)
		return res;

	if (NFS_DIREOF(inode) && filp->f_pos >= NFS_DIREOF(inode))
		return 0;

	if ((offset = nfs_readdir_offset(inode, filp->f_pos)) < 0)
		goto no_dirent_page;

	hash = page_hash(inode, offset);
	page = __find_get_page(inode, offset, hash);
	if (!page)
		goto no_dirent_page;
	if (!Page_Uptodate(page))
		goto dirent_read_error;
success:
	filp->f_pos = nfs_do_filldir((__u32 *) page_address(page),
				     filp->f_pos, dirent, filldir);
	page_cache_release(page);
	return 0;

no_dirent_page:
	page = try_to_get_dirent_page(filp, filp->f_pos, 1);
	if (!page)
		goto no_page;

	if (Page_Uptodate(page))
		goto success;
dirent_read_error:
	page_cache_release(page);
no_page:
	return -EIO;
}

/* Flush directory cookie and EOF caches for an inode.
 * So we don't thrash allocating/freeing cookie tables,
 * we keep the cookies around until the inode is
 * deleted/reused.
 */
__inline__ void nfs_flush_dircache(struct inode *inode)
{
	struct nfs_cookie_table *p = NFS_COOKIES(inode);

	while (p != NULL) {
		int i;

		for(i = 0; i < COOKIES_PER_CHUNK; i++)
			p->cookies[i] = 0;

		p = p->next;
	}
	NFS_DIREOF(inode) = 0;
}

/* Free up directory cache state, this happens when
 * nfs_delete_inode is called on an NFS directory.
 */
void nfs_free_dircache(struct inode *inode)
{
	struct nfs_cookie_table *p = NFS_COOKIES(inode);

	while (p != NULL) {
		struct nfs_cookie_table *next = p->next;
		kmem_cache_free(nfs_cookie_cachep, p);
		p = next;
	}
	NFS_COOKIES(inode) = NULL;
	NFS_DIREOF(inode) = 0;
}

/*
 * Whenever an NFS operation succeeds, we know that the dentry
 * is valid, so we update the revalidation timestamp.
 */
static inline void nfs_renew_times(struct dentry * dentry)
{
	dentry->d_time = jiffies;
}

static inline int nfs_dentry_force_reval(struct dentry *dentry, int flags)
{
	struct inode *inode = dentry->d_inode;
	unsigned long timeout = NFS_ATTRTIMEO(inode);

	/*
	 * If it's the last lookup in a series, we use a stricter
	 * cache consistency check by looking at the parent mtime.
	 *
	 * If it's been modified in the last hour, be really strict.
	 * (This still means that we can avoid doing unnecessary
	 * work on directories like /usr/share/bin etc which basically
	 * never change).
	 */
	if (!(flags & LOOKUP_CONTINUE)) {
		long diff = CURRENT_TIME - dentry->d_parent->d_inode->i_mtime;

		if (diff < 15*60)
			timeout = 0;
	}
	
	return time_after(jiffies,dentry->d_time + timeout);
}

/*
 * We judge how long we want to trust negative
 * dentries by looking at the parent inode mtime.
 *
 * If mtime is close to present time, we revalidate
 * more often.
 */
static inline int nfs_neg_need_reval(struct dentry *dentry)
{
	unsigned long timeout = 30 * HZ;
	long diff = CURRENT_TIME - dentry->d_parent->d_inode->i_mtime;

	if (diff < 5*60)
		timeout = 1 * HZ;

	return time_after(jiffies, dentry->d_time + timeout);
}

/*
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that
 * lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 *
 * If the dentry is older than the revalidation interval, 
 * we do a new lookup and verify that the dentry is still
 * correct.
 */
static int nfs_lookup_revalidate(struct dentry * dentry, int flags)
{
	struct dentry * parent = dentry->d_parent;
	struct inode * inode = dentry->d_inode;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	/*
	 * If we don't have an inode, let's look at the parent
	 * directory mtime to get a hint about how often we
	 * should validate things..
	 */
	if (!inode) {
		if (nfs_neg_need_reval(dentry))
			goto out_bad;
		goto out_valid;
	}

	if (is_bad_inode(inode)) {
		dfprintk(VFS, "nfs_lookup_validate: %s/%s has dud inode\n",
			parent->d_name.name, dentry->d_name.name);
		goto out_bad;
	}

	if (IS_ROOT(dentry))
		goto out_valid;

	if (!nfs_dentry_force_reval(dentry, flags))
		goto out_valid;

	/*
	 * Do a new lookup and check the dentry attributes.
	 */
	error = nfs_proc_lookup(NFS_DSERVER(parent), NFS_FH(parent),
				dentry->d_name.name, &fhandle, &fattr);
	if (error)
		goto out_bad;

	/* Inode number matches? */
	if (fattr.fileid != inode->i_ino)
		goto out_bad;

	/* Filehandle matches? */
	if (memcmp(dentry->d_fsdata, &fhandle, sizeof(struct nfs_fh))) {
		if (dentry->d_count < 2)
			goto out_bad;
	}

	/* Ok, remeber that we successfully checked it.. */
	nfs_renew_times(dentry);
	nfs_refresh_inode(inode, &fattr);

out_valid:
	return 1;
out_bad:
	/* Purge readdir caches. */
	if (dentry->d_parent->d_inode) {
		invalidate_inode_pages(dentry->d_parent->d_inode);
		nfs_flush_dircache(dentry->d_parent->d_inode);
	}
	if (inode && S_ISDIR(inode->i_mode)) {
		invalidate_inode_pages(inode);
		nfs_flush_dircache(inode);
	}
	return 0;
}

/*
 * This is called from dput() when d_count is going to 0.
 * We use it to clean up silly-renamed files.
 */
static void nfs_dentry_delete(struct dentry *dentry)
{
	dfprintk(VFS, "NFS: dentry_delete(%s/%s, %x)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		dentry->d_flags);

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		int error;
		
		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
		/* Unhash it first */
		d_drop(dentry);
		error = nfs_safe_remove(dentry);
		if (error)
			printk("NFS: can't silly-delete %s/%s, error=%d\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name, error);
	}

#ifdef NFS_PARANOIA
	/*
	 * Sanity check: if the dentry has been unhashed and the
	 * inode still has users, we could have problems ...
	 */
	if (list_empty(&dentry->d_hash) && dentry->d_inode) {
		struct inode *inode = dentry->d_inode;
		int max_count = (S_ISDIR(inode->i_mode) ? 1 : inode->i_nlink);
		if (inode->i_count > max_count) {
printk("nfs_dentry_delete: %s/%s: ino=%ld, count=%d, nlink=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->i_ino, inode->i_count, inode->i_nlink);
		}
	}
#endif
}

static kmem_cache_t *nfs_fh_cachep;

__inline__ struct nfs_fh *nfs_fh_alloc(void)
{
	return kmem_cache_alloc(nfs_fh_cachep, SLAB_KERNEL);
}

__inline__ void nfs_fh_free(struct nfs_fh *p)
{
	kmem_cache_free(nfs_fh_cachep, p);
}

/*
 * Called when the dentry is being freed to release private memory.
 */
static void nfs_dentry_release(struct dentry *dentry)
{
	if (dentry->d_fsdata)
		nfs_fh_free(dentry->d_fsdata);
}

struct dentry_operations nfs_dentry_operations = {
	nfs_lookup_revalidate,	/* d_revalidate(struct dentry *, int) */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	nfs_dentry_delete,	/* d_delete(struct dentry *) */
	nfs_dentry_release,	/* d_release(struct dentry *) */
	NULL			/* d_iput */
};

#ifdef NFS_PARANOIA
/*
 * Display all dentries holding the specified inode.
 */
static void show_dentry(struct list_head * dlist)
{
	struct list_head *tmp = dlist;

	while ((tmp = tmp->next) != dlist) {
		struct dentry * dentry = list_entry(tmp, struct dentry, d_alias);
		const char * unhashed = "";

		if (list_empty(&dentry->d_hash))
			unhashed = "(unhashed)";

		printk("show_dentry: %s/%s, d_count=%d%s\n",
			dentry->d_parent->d_name.name,
			dentry->d_name.name, dentry->d_count,
			unhashed);
	}
}
#endif

static struct dentry *nfs_lookup(struct inode *dir, struct dentry * dentry)
{
	struct inode *inode;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	dfprintk(VFS, "NFS: lookup(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	error = -ENOMEM;
	if (!dentry->d_fsdata) {
		dentry->d_fsdata = nfs_fh_alloc();
		if (!dentry->d_fsdata)
			goto out;
	}
	dentry->d_op = &nfs_dentry_operations;

	error = nfs_proc_lookup(NFS_SERVER(dir), NFS_FH(dentry->d_parent), 
				dentry->d_name.name, &fhandle, &fattr);
	inode = NULL;
	if (error == -ENOENT)
		goto no_entry;
	if (!error) {
		error = -EACCES;
		inode = nfs_fhget(dentry, &fhandle, &fattr);
		if (inode) {
#ifdef NFS_PARANOIA
if (inode->i_count > (S_ISDIR(inode->i_mode) ? 1 : inode->i_nlink)) {
printk("nfs_lookup: %s/%s ino=%ld in use, count=%d, nlink=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->i_ino, inode->i_count, inode->i_nlink);
show_dentry(&inode->i_dentry);
}
#endif
	    no_entry:
			d_add(dentry, inode);
			nfs_renew_times(dentry);
			error = 0;
		}
	}
out:
	return ERR_PTR(error);
}

/*
 * Code common to create, mkdir, and mknod.
 */
static int nfs_instantiate(struct dentry *dentry, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr)
{
	struct inode *inode;
	int error = -EACCES;

	inode = nfs_fhget(dentry, fhandle, fattr);
	if (inode) {
#ifdef NFS_PARANOIA
if (inode->i_count > (S_ISDIR(inode->i_mode) ? 1 : inode->i_nlink)) {
printk("nfs_instantiate: %s/%s ino=%ld in use, count=%d, nlink=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->i_ino, inode->i_count, inode->i_nlink);
show_dentry(&inode->i_dentry);
}
#endif
		d_instantiate(dentry, inode);
		nfs_renew_times(dentry);
		error = 0;
	}
	return error;
}

/*
 * Following a failed create operation, we drop the dentry rather
 * than retain a negative dentry. This avoids a problem in the event
 * that the operation succeeded on the server, but an error in the
 * reply path made it appear to have failed.
 */
static int nfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;

	dfprintk(VFS, "NFS: create(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	/*
	 * Invalidate the dir cache before the operation to avoid a race.
	 */
	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
			dentry->d_name.name, &sattr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dentry, &fhandle, &fattr);
	if (error)
		d_drop(dentry);
	return error;
}

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
static int nfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int rdev)
{
	int error;
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;

	dfprintk(VFS, "NFS: mknod(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	if (S_ISCHR(mode) || S_ISBLK(mode))
		sattr.size = rdev; /* get out your barf bag */
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name, &sattr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dentry, &fhandle, &fattr);
	if (error)
		d_drop(dentry);
	return error;
}

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
static int nfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;

	dfprintk(VFS, "NFS: mkdir(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	sattr.mode = mode | S_IFDIR;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	/*
	 * Always drop the dentry, we can't always depend on
	 * the fattr returned by the server (AIX seems to be
	 * broken). We're better off doing another lookup than
	 * depending on potentially bogus information.
	 */
	d_drop(dentry);
	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_mkdir(NFS_DSERVER(dentry), NFS_FH(dentry->d_parent),
				dentry->d_name.name, &sattr, &fhandle, &fattr);
	return error;
}

static int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

#ifdef NFS_PARANOIA
if (dentry->d_inode->i_count > 1)
printk("nfs_rmdir: %s/%s inode busy?? i_count=%d, i_nlink=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
dentry->d_inode->i_count, dentry->d_inode->i_nlink);
#endif

	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_rmdir(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name);

	/* Update i_nlink and invalidate dentry. */
	if (!error) {
		d_drop(dentry);
		if (dentry->d_inode->i_nlink)
			dentry->d_inode->i_nlink --;
	}

	return error;
}


/*  Note: we copy the code from lookup_dentry() here, only: we have to
 *  omit the directory lock. We are already the owner of the lock when
 *  we reach here. And "down(&dir->i_sem)" would make us sleep forever
 *  ('cause WE have the lock)
 * 
 *  VERY IMPORTANT: calculate the hash for this dentry!!!!!!!!
 *  Otherwise the cached lookup DEFINITELY WILL fail. And a new dentry
 *  is created. Without the DCACHE_NFSFS_RENAMED flag. And with d_count
 *  == 1. And trouble.
 *
 *  Concerning my choice of the temp name: it is just nice to have
 *  i_ino part of the temp name, as this offers another check whether
 *  somebody attempts to remove the "silly renamed" dentry itself.
 *  Which is something that I consider evil. Your opinion may vary.
 *  BUT:
 *  Now that I compute the hash value right, it should be possible to simply
 *  check for the DCACHE_NFSFS_RENAMED flag in dentry->d_flag instead of
 *  doing the string compare.
 *  WHICH MEANS:
 *  This offers the opportunity to shorten the temp name. Currently, I use
 *  the hex representation of i_ino + an event counter. This sums up to
 *  as much as 36 characters for a 64 bit machine, and needs 20 chars on 
 *  a 32 bit machine.
 *  QUINTESSENCE
 *  The use of i_ino is simply cosmetic. All we need is a unique temp
 *  file name for the .nfs files. The event counter seemed to be adequate.
 *  And as we retry in case such a file already exists, we are guaranteed
 *  to succeed.
 */

static
struct dentry *nfs_silly_lookup(struct dentry *parent, char *silly, int slen)
{
	struct qstr    sqstr;
	struct dentry *sdentry;
	struct dentry *res;

	sqstr.name = silly;
	sqstr.len  = slen;
	sqstr.hash = full_name_hash(silly, slen);
	sdentry = d_lookup(parent, &sqstr);
	if (!sdentry) {
		sdentry = d_alloc(parent, &sqstr);
		if (sdentry == NULL)
			return ERR_PTR(-ENOMEM);
		res = nfs_lookup(parent->d_inode, sdentry);
		if (res) {
			dput(sdentry);
			return res;
		}
	}
	return sdentry;
}

static int nfs_sillyrename(struct inode *dir, struct dentry *dentry)
{
	static unsigned int sillycounter = 0;
	const int      i_inosize  = sizeof(dir->i_ino)*2;
	const int      countersize = sizeof(sillycounter)*2;
	const int      slen       = strlen(".nfs") + i_inosize + countersize;
	char           silly[slen+1];
	struct dentry *sdentry;
	int            error = -EIO;

	dfprintk(VFS, "NFS: silly-rename(%s/%s, ct=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name, 
		dentry->d_count);

	/*
	 * Note that a silly-renamed file can be deleted once it's
	 * no longer in use -- it's just an ordinary file now.
	 */
	if (dentry->d_count == 1) {
		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
		goto out;  /* No need to silly rename. */
	}

#ifdef NFS_PARANOIA
if (!dentry->d_inode)
printk("NFS: silly-renaming %s/%s, negative dentry??\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	/*
	 * We don't allow a dentry to be silly-renamed twice.
	 */
	error = -EBUSY;
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto out;

	sprintf(silly, ".nfs%*.*lx",
		i_inosize, i_inosize, dentry->d_inode->i_ino);

	sdentry = NULL;
	do {
		char *suffix = silly + slen - countersize;

		dput(sdentry);
		sillycounter++;
		sprintf(suffix, "%*.*x", countersize, countersize, sillycounter);

		dfprintk(VFS, "trying to rename %s to %s\n",
			 dentry->d_name.name, silly);
		
		sdentry = nfs_silly_lookup(dentry->d_parent, silly, slen);
		/*
		 * N.B. Better to return EBUSY here ... it could be
		 * dangerous to delete the file while it's in use.
		 */
		if (IS_ERR(sdentry))
			goto out;
	} while(sdentry->d_inode != NULL); /* need negative lookup */

	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_rename(NFS_SERVER(dir),
				NFS_FH(dentry->d_parent), dentry->d_name.name,
				NFS_FH(dentry->d_parent), silly);
	if (!error) {
		nfs_renew_times(dentry);
		d_move(dentry, sdentry);
		dentry->d_flags |= DCACHE_NFSFS_RENAMED;
 		/* If we return 0 we don't unlink */
	}
	dput(sdentry);
out:
	return error;
}

/*
 * Remove a file after making sure there are no pending writes,
 * and after checking that the file has only one user. 
 *
 * We update inode->i_nlink and free the inode prior to the operation
 * to avoid possible races if the server reuses the inode.
 */
static int nfs_safe_remove(struct dentry *dentry)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct inode *inode = dentry->d_inode;
	int error, rehash = 0;
		
	dfprintk(VFS, "NFS: safe_remove(%s/%s, %ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino);

	/* N.B. not needed now that d_delete is done in advance? */
	error = -EBUSY;
	if (!inode) {
#ifdef NFS_PARANOIA
printk("nfs_safe_remove: %s/%s already negative??\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	}

	if (dentry->d_count > 1) {
#ifdef NFS_PARANOIA
printk("nfs_safe_remove: %s/%s busy, d_count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count);
#endif
		goto out;
	}
#ifdef NFS_PARANOIA
if (inode && inode->i_count > inode->i_nlink)
printk("nfs_safe_remove: %s/%s inode busy?? i_count=%d, i_nlink=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->i_count, inode->i_nlink);
#endif
	/*
	 * Unhash the dentry while we remove the file ...
	 */
	if (!list_empty(&dentry->d_hash)) {
		d_drop(dentry);
		rehash = 1;
	}
	/*
	 * Update i_nlink and free the inode before unlinking.
	 */
	if (inode) {
		if (inode->i_nlink)
			inode->i_nlink --;
		d_delete(dentry);
	}
	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_remove(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name);
	/*
	 * Rehash the negative dentry if the operation succeeded.
	 */
	if (!error && rehash)
		d_add(dentry, NULL);
out:
	return error;
}

/*  We do silly rename. In case sillyrename() returns -EBUSY, the inode
 *  belongs to an active ".nfs..." file and we return -EBUSY.
 *
 *  If sillyrename() returns 0, we do nothing, otherwise we unlink.
 */
static int nfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: unlink(%x/%ld, %s)\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	error = nfs_sillyrename(dir, dentry);
	if (error && error != -EBUSY) {
		error = nfs_safe_remove(dentry);
		if (!error) {
			nfs_renew_times(dentry);
		}
	}
	return error;
}

static int
nfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct nfs_sattr sattr;
	int error;

	dfprintk(VFS, "NFS: symlink(%x/%ld, %s, %s)\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name, symname);

	error = -ENAMETOOLONG;
	if (strlen(symname) > NFS_MAXPATHLEN)
		goto out;

#ifdef NFS_PARANOIA
if (dentry->d_inode)
printk("nfs_proc_symlink: %s/%s not negative!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	/*
	 * Fill in the sattr for the call.
 	 * Note: SunOS 4.1.2 crashes if the mode isn't initialized!
	 */
	sattr.mode = S_IFLNK | S_IRWXUGO;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	/*
	 * Drop the dentry in advance to force a new lookup.
	 * Since nfs_proc_symlink doesn't return a fattr, we
	 * can't instantiate the new inode.
	 */
	d_drop(dentry);
	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_symlink(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name, symname, &sattr);
	if (!error) {
		nfs_renew_times(dentry->d_parent);
	} else if (error == -EEXIST) {
		printk("nfs_proc_symlink: %s/%s already exists??\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	}

out:
	return error;
}

static int 
nfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int error;

	dfprintk(VFS, "NFS: link(%s/%s -> %s/%s)\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		dentry->d_parent->d_name.name, dentry->d_name.name);

	/*
	 * Drop the dentry in advance to force a new lookup.
	 * Since nfs_proc_link doesn't return a file handle,
	 * we can't use the existing dentry.
	 */
	d_drop(dentry);
	invalidate_inode_pages(dir);
	nfs_flush_dircache(dir);
	error = nfs_proc_link(NFS_DSERVER(old_dentry), NFS_FH(old_dentry),
				NFS_FH(dentry->d_parent), dentry->d_name.name);
	if (!error) {
 		/*
		 * Update the link count immediately, as some apps
		 * (e.g. pine) test this after making a link.
		 */
		inode->i_nlink++;
	}
	return error;
}

/*
 * RENAME
 * FIXME: Some nfsds, like the Linux user space nfsd, may generate a
 * different file handle for the same inode after a rename (e.g. when
 * moving to a different directory). A fail-safe method to do so would
 * be to look up old_dir/old_name, create a link to new_dir/new_name and
 * rename the old file using the sillyrename stuff. This way, the original
 * file in old_dir will go away when the last process iput()s the inode.
 *
 * FIXED.
 * 
 * It actually works quite well. One needs to have the possibility for
 * at least one ".nfs..." file in each directory the file ever gets
 * moved or linked to which happens automagically with the new
 * implementation that only depends on the dcache stuff instead of
 * using the inode layer
 *
 * Unfortunately, things are a little more complicated than indicated
 * above. For a cross-directory move, we want to make sure we can get
 * rid of the old inode after the operation.  This means there must be
 * no pending writes (if it's a file), and the use count must be 1.
 * If these conditions are met, we can drop the dentries before doing
 * the rename.
 */
static int nfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct dentry *dentry = NULL;
	int error, rehash = 0, update = 1;

	dfprintk(VFS, "NFS: rename(%s/%s -> %s/%s, ct=%d)\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
		new_dentry->d_count);

	/*
	 * First check whether the target is busy ... we can't
	 * safely do _any_ rename if the target is in use.
	 *
	 * For files, make a copy of the dentry and then do a 
	 * silly-rename. If the silly-rename succeeds, the
	 * copied dentry is hashed and becomes the new target.
	 *
	 * With directories check is done in VFS.
	 */
	error = -EBUSY;
	if (new_dentry->d_count > 1 && new_inode) {
		int err;
		/* copy the target dentry's name */
		dentry = d_alloc(new_dentry->d_parent,
				 &new_dentry->d_name);
		if (!dentry)
			goto out;

		/* silly-rename the existing target ... */
		err = nfs_sillyrename(new_dir, new_dentry);
		if (!err) {
			new_dentry = dentry;
			new_inode = NULL;
			/* hash the replacement target */
			d_add(new_dentry, NULL);
		}

		/* dentry still busy? */
		if (new_dentry->d_count > 1) {
#ifdef NFS_PARANOIA
printk("nfs_rename: target %s/%s busy, d_count=%d\n",
new_dentry->d_parent->d_name.name,new_dentry->d_name.name,new_dentry->d_count);
#endif
			goto out;
		}
	}

	/*
	 * Check for within-directory rename ... no complications.
	 */
	if (new_dir == old_dir)
		goto do_rename;
	/*
	 * Cross-directory move ...
	 *
	 * ... prune child dentries and writebacks if needed.
	 */
	if (old_dentry->d_count > 1) {
		nfs_wb_all(old_inode);
		shrink_dcache_parent(old_dentry);
	}

	/*
	 * Now check the use counts ... we can't safely do the
	 * rename unless we can drop the dentries first.
	 */
	if (old_dentry->d_count > 1) {
#ifdef NFS_PARANOIA
printk("nfs_rename: old dentry %s/%s busy, d_count=%d\n",
old_dentry->d_parent->d_name.name,old_dentry->d_name.name,old_dentry->d_count);
#endif
		goto out;
	}
	if (new_dentry->d_count > 1 && new_inode) {
#ifdef NFS_PARANOIA
printk("nfs_rename: new dentry %s/%s busy, d_count=%d\n",
new_dentry->d_parent->d_name.name,new_dentry->d_name.name,new_dentry->d_count);
#endif
		goto out;
	}

	d_drop(old_dentry);
	update = 0;

do_rename:
	/*
	 * To prevent any new references to the target during the rename,
	 * we unhash the dentry and free the inode in advance.
	 */
#ifdef NFS_PARANOIA
if (new_inode && 
    new_inode->i_count > (S_ISDIR(new_inode->i_mode) ? 1 : new_inode->i_nlink))
printk("nfs_rename: %s/%s inode busy?? i_count=%d, i_nlink=%d\n",
new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
new_inode->i_count, new_inode->i_nlink);
#endif
	if (!list_empty(&new_dentry->d_hash)) {
		d_drop(new_dentry);
		rehash = update;
	}
	if (new_inode) {
		d_delete(new_dentry);
	}

	invalidate_inode_pages(new_dir);
	nfs_flush_dircache(new_dir);
	invalidate_inode_pages(old_dir);
	nfs_flush_dircache(old_dir);
	error = nfs_proc_rename(NFS_DSERVER(old_dentry),
			NFS_FH(old_dentry->d_parent), old_dentry->d_name.name,
			NFS_FH(new_dentry->d_parent), new_dentry->d_name.name);
	if (!error && !S_ISDIR(old_inode->i_mode)) {
		/* Update the dcache if needed */
		if (rehash)
			d_add(new_dentry, NULL);
		if (update)
			d_move(old_dentry, new_dentry);
	}

out:
	/* new dentry created? */
	if (dentry)
		dput(dentry);
	return error;
}

int nfs_init_fhcache(void)
{
	nfs_fh_cachep = kmem_cache_create("nfs_fh",
					  sizeof(struct nfs_fh),
					  0, SLAB_HWCACHE_ALIGN,
					  NULL, NULL);
	if (nfs_fh_cachep == NULL)
		return -ENOMEM;

	nfs_cookie_cachep = kmem_cache_create("nfs_dcookie",
					      sizeof(struct nfs_cookie_table),
					      0, SLAB_HWCACHE_ALIGN,
					      NULL, NULL);
	if (nfs_cookie_cachep == NULL)
		return -ENOMEM;

	return 0;
}

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
