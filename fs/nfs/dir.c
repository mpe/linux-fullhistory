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
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/sunrpc/types.h>
#include <linux/nfs_fs.h>

#include <asm/segment.h>	/* for fs functions */

#define NFS_PARANOIA 1
/* #define NFS_DEBUG_VERBOSE 1 */

/*
 * Head for a dircache entry. Currently still very simple; when
 * the cache grows larger, we will need a LRU list.
 */
struct nfs_dirent {
	dev_t			dev;		/* device number */
	ino_t			ino;		/* inode number */
	u32			cookie;		/* cookie of first entry */
	unsigned short		valid  : 1,	/* data is valid */
				locked : 1;	/* entry locked */
	unsigned int		size;		/* # of entries */
	unsigned long		age;		/* last used */
	unsigned long		mtime;		/* last attr stamp */
	struct wait_queue *	wait;
	__u32 *			entry;		/* three __u32's per entry */
};

static int nfs_safe_remove(struct dentry *);

static int nfs_dir_open(struct inode *, struct file *);
static ssize_t nfs_dir_read(struct file *, char *, size_t, loff_t *);
static int nfs_readdir(struct file *, void *, filldir_t);
static int nfs_lookup(struct inode *, struct dentry *);
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
	nfs_dir_open,		/* open - revalidate */
	NULL,			/* flush */
	NULL,			/* no special release code */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	nfs_revalidate,		/* revalidate */
};

static int
nfs_dir_open(struct inode *dir, struct file *file)
{
	struct dentry *dentry = file->f_dentry;

	dfprintk(VFS, "NFS: nfs_dir_open(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	return nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
}

static ssize_t
nfs_dir_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EISDIR;
}

static struct nfs_dirent	dircache[NFS_MAX_DIRCACHE];

/*
 * We need to do caching of directory entries to prevent an
 * incredible amount of RPC traffic.  Only the most recent open
 * directory is cached.  This seems sufficient for most purposes.
 * Technically, we ought to flush the cache on close but this is
 * not a problem in practice.
 *
 * XXX: Do proper directory caching by stuffing data into the
 * page cache (may require some fiddling for rsize < PAGE_SIZE).
 */

static int nfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry 		*dentry = filp->f_dentry;
	struct inode 		*inode = dentry->d_inode;
	static struct wait_queue *readdir_wait = NULL;
	struct wait_queue	**waitp = NULL;
	struct nfs_dirent	*cache, *free;
	unsigned long		age, dead;
	u32			cookie;
	int			ismydir, result;
	int			i, j, index = 0;
	__u32			*entry;
	char			*name, *start;

	dfprintk(VFS, "NFS: nfs_readdir(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	result = -EBADF;
	if (!inode || !S_ISDIR(inode->i_mode)) {
		printk("nfs_readdir: inode is NULL or not a directory\n");
		goto out;
	}

	result = nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
	if (result < 0)
		goto out;

	/*
	 * Try to find the entry in the cache
	 */
again:
	if (waitp) {
		interruptible_sleep_on(waitp);
		if (signal_pending(current))
			return -ERESTARTSYS;
		waitp = NULL;
	}

	cookie = filp->f_pos;
	entry  = NULL;
	free   = NULL;
	age    = ~(unsigned long) 0;
	dead   = jiffies - NFS_ATTRTIMEO(inode);

	for (i = 0, cache = dircache; i < NFS_MAX_DIRCACHE; i++, cache++) {
		/*
		dprintk("NFS: dircache[%d] valid %d locked %d\n",
					i, cache->valid, cache->locked);
		 */
		ismydir = (cache->dev == inode->i_dev
				&& cache->ino == inode->i_ino);
		if (cache->locked) {
			if (!ismydir || cache->cookie != cookie)
				continue;
			dfprintk(DIRCACHE, "NFS: waiting on dircache entry\n");
			waitp = &cache->wait;
			goto again;
		}

		if (ismydir && cache->mtime != inode->i_mtime)
			cache->valid = 0;

		if (!cache->valid || cache->age < dead) {
			free = cache;
			age  = 0;
		} else if (cache->age < age) {
			free = cache;
			age  = cache->age;
		}

		if (!ismydir || !cache->valid)
			continue;

		if (cache->cookie == cookie && cache->size > 0) {
			entry = cache->entry + (index = 0);
			cache->locked = 1;
			break;
		}
		for (j = 0; j < cache->size; j++) {
			__u32 *this_ent = cache->entry + j*3;

			if (*(this_ent+1) != cookie)
				continue;
			if (j < cache->size - 1) {
				index = j + 1;
				entry = this_ent + 3;
			} else if (*(this_ent+2) & (1 << 15)) {
				/* eof */
				return 0;
			}
			break;
		}
		if (entry) {
			dfprintk(DIRCACHE, "NFS: found dircache entry %d\n",
						(int)(cache - dircache));
			cache->locked = 1;
			break;
		}
	}

	/*
	 * Okay, entry not present in cache, or locked and inaccessible.
	 * Set up the cache entry and attempt a READDIR call.
	 */
	if (entry == NULL) {
		if ((cache = free) == NULL) {
			dfprintk(DIRCACHE, "NFS: dircache contention\n");
			waitp = &readdir_wait;
			goto again;
		}
		dfprintk(DIRCACHE, "NFS: using free dircache entry %d\n",
				(int)(free - dircache));
		cache->cookie = cookie;
		cache->locked = 1;
		cache->valid  = 0;
		cache->dev    = inode->i_dev;
		cache->ino    = inode->i_ino;
		if (!cache->entry) {
			result = -ENOMEM;
			cache->entry = (__u32 *) get_free_page(GFP_KERNEL);
			if (!cache->entry)
				goto done;
		}

		result = nfs_proc_readdir(NFS_SERVER(inode), NFS_FH(dentry),
					cookie, PAGE_SIZE, cache->entry);
		if (result <= 0)
			goto done;
		cache->size  = result;
		cache->valid = 1;
		entry = cache->entry + (index = 0);
	}
	cache->mtime = inode->i_mtime;
	cache->age = jiffies;

	/*
	 * Yowza! We have a cache entry...
	 */
	start = (char *) cache->entry;
	while (index < cache->size) {
		__u32	fileid  = *entry++;
		__u32	nextpos = *entry++; /* cookie */
		__u32	length  = *entry++;

		/*
		 * Unpack the eof flag, offset, and length
		 */
		result = length & (1 << 15); /* eof flag */
		name = start + ((length >> 16) & 0xFFFF);
		length &= 0x7FFF;
		/*
		dprintk("NFS: filldir(%p, %.*s, %d, %d, %x, eof %x)\n", entry,
				(int) length, name, length,
				(unsigned int) filp->f_pos,
				fileid, result);
		 */

		if (filldir(dirent, name, length, cookie, fileid) < 0)
			break;
		cookie = nextpos;
		index++;
	}
	filp->f_pos = cookie;
	result = 0;

	/* XXX: May want to kick async readdir-ahead here. Not too hard
	 * to do. */

done:
	dfprintk(DIRCACHE, "NFS: nfs_readdir complete\n");
	cache->locked = 0;
	wake_up(&cache->wait);
	wake_up(&readdir_wait);

out:
	return result;
}

/*
 * Invalidate dircache entries for an inode.
 */
void
nfs_invalidate_dircache(struct inode *inode)
{
	struct nfs_dirent *cache = dircache;
	dev_t		dev = inode->i_dev;
	ino_t		ino = inode->i_ino;
	int		i;

	dfprintk(DIRCACHE, "NFS: invalidate dircache for %x/%ld\n", dev, (long)ino);
	for (i = NFS_MAX_DIRCACHE; i--; cache++) {
		if (cache->ino != ino)
			continue;
		if (cache->dev != dev)
			continue;
		if (cache->locked) {
			printk("NFS: cache locked for %s/%ld\n",
				kdevname(dev), (long) ino);
			continue;
		}
		cache->valid = 0;	/* brute force */
	}
}

/*
 * Invalidate the dircache for a super block (or all caches),
 * and release the cache memory.
 */
void
nfs_invalidate_dircache_sb(struct super_block *sb)
{
	struct nfs_dirent *cache = dircache;
	int		i;

	for (i = NFS_MAX_DIRCACHE; i--; cache++) {
		if (sb && sb->s_dev != cache->dev)
			continue;
		if (cache->locked) {
			printk("NFS: cache locked at umount %s\n",
				(cache->entry ? "(lost a page!)" : ""));
			continue;
		}
		cache->valid = 0;	/* brute force */
		if (cache->entry) {
			free_page((unsigned long) cache->entry);
			cache->entry = NULL;
		}
	}
}

/*
 * Free directory cache memory
 * Called from cleanup_module
 */
void
nfs_free_dircache(void)
{
	dfprintk(DIRCACHE, "NFS: freeing dircache\n");
	nfs_invalidate_dircache_sb(NULL);
}

/*
 * Whenever an NFS operation succeeds, we know that the dentry
 * is valid, so we update the revalidation timestamp.
 */
static inline void nfs_renew_times(struct dentry * dentry)
{
	dentry->d_time = jiffies;
}

#define NFS_REVALIDATE_INTERVAL (5*HZ)
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
static int nfs_lookup_revalidate(struct dentry * dentry)
{
	struct dentry * parent = dentry->d_parent;
	struct inode * inode = dentry->d_inode;
	unsigned long time = jiffies - dentry->d_time;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	/*
	 * If we don't have an inode, let's just assume
	 * a 5-second "live" time for negative dentries.
	 */
	if (!inode) {
		if (time < NFS_REVALIDATE_INTERVAL)
			goto out_valid;
		goto out_bad;
	}

	if (is_bad_inode(inode)) {
#ifdef NFS_PARANOIA
printk("nfs_lookup_validate: %s/%s has dud inode\n",
parent->d_name.name, dentry->d_name.name);
#endif
		goto out_bad;
	}

	if (time < NFS_ATTRTIMEO(inode))
		goto out_valid;

	if (IS_ROOT(dentry))
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
	if (memcmp(dentry->d_fsdata, &fhandle, sizeof(struct nfs_fh)))
		goto out_bad;

	/* Ok, remeber that we successfully checked it.. */
	nfs_renew_times(dentry);
	nfs_refresh_inode(inode, &fattr);

out_valid:
	return 1;
out_bad:
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

/*
 * Called when the dentry is being freed to release private memory.
 */
static void nfs_dentry_release(struct dentry *dentry)
{
	if (dentry->d_fsdata)
		kfree(dentry->d_fsdata);
}

struct dentry_operations nfs_dentry_operations = {
	nfs_lookup_revalidate,	/* d_validate(struct dentry *) */
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

static int nfs_lookup(struct inode *dir, struct dentry * dentry)
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
		dentry->d_fsdata = kmalloc(sizeof(struct nfs_fh), GFP_KERNEL);
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
	return error;
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

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	/*
	 * Invalidate the dir cache before the operation to avoid a race.
	 */
	nfs_invalidate_dircache(dir);
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
			dentry->d_name.name, &sattr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dentry, &fhandle, &fattr);
	if (error)
		d_drop(dentry);
out:
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

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	if (S_ISCHR(mode) || S_ISBLK(mode))
		sattr.size = rdev; /* get out your barf bag */
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	nfs_invalidate_dircache(dir);
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

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

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
	nfs_invalidate_dircache(dir);
	error = nfs_proc_mkdir(NFS_DSERVER(dentry), NFS_FH(dentry->d_parent),
				dentry->d_name.name, &sattr, &fhandle, &fattr);
out:
	return error;
}

/*
 * To avoid retaining a stale inode reference, we check the dentry
 * use count prior to the operation, and return EBUSY if it has
 * multiple users.
 *
 * We update inode->i_nlink and free the inode prior to the operation
 * to avoid possible races if the server reuses the inode.
 *
 * FIXME! We don't do it anymore (2.1.131) - it interacts badly with
 * new rmdir().  -- AV
 */
static int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	error = -EBUSY;
	if (!list_empty(&dentry->d_hash))
		goto out;

#ifdef NFS_PARANOIA
if (dentry->d_inode->i_count > 1)
printk("nfs_rmdir: %s/%s inode busy?? i_count=%d, i_nlink=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
dentry->d_inode->i_count, dentry->d_inode->i_nlink);
#endif

	/*
	 * Update i_nlink and free the inode before unlinking.
	 */
	if (dentry->d_inode->i_nlink)
		dentry->d_inode->i_nlink --;
	nfs_invalidate_dircache(dir);
	error = nfs_proc_rmdir(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name);
out:
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
	int error;

	sqstr.name = silly;
	sqstr.len  = slen;
	sqstr.hash = full_name_hash(silly, slen);
	sdentry = d_lookup(parent, &sqstr);
	if (!sdentry) {
		sdentry = d_alloc(parent, &sqstr);
		if (sdentry == NULL)
			return ERR_PTR(-ENOMEM);
		error = nfs_lookup(parent->d_inode, sdentry);
		if (error) {
			dput(sdentry);
			return ERR_PTR(error);
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

	nfs_invalidate_dircache(dir);
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
	nfs_invalidate_dircache(dir);
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

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	error = nfs_sillyrename(dir, dentry);
	if (error && error != -EBUSY) {
		error = nfs_safe_remove(dentry);
		if (!error) {
			nfs_renew_times(dentry);
		}
	}
out:
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
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

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
	nfs_invalidate_dircache(dir);
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

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	/*
	 * Drop the dentry in advance to force a new lookup.
	 * Since nfs_proc_link doesn't return a file handle,
	 * we can't use the existing dentry.
	 */
	d_drop(dentry);
	nfs_invalidate_dircache(dir);
	error = nfs_proc_link(NFS_DSERVER(old_dentry), NFS_FH(old_dentry),
				NFS_FH(dentry->d_parent), dentry->d_name.name);
	if (!error) {
 		/*
		 * Update the link count immediately, as some apps
		 * (e.g. pine) test this after making a link.
		 */
		inode->i_nlink++;
	}
out:
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

	error = -ENAMETOOLONG;
	if (old_dentry->d_name.len > NFS_MAXNAMLEN ||
	    new_dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	/*
	 * First check whether the target is busy ... we can't
	 * safely do _any_ rename if the target is in use.
	 *
	 * For files, make a copy of the dentry and then do a 
	 * silly-rename. If the silly-rename succeeds, the
	 * copied dentry is hashed and becomes the new target.
	 *
	 * For directories, prune any unused children.
	 */
	error = -EBUSY;
	if (new_dentry->d_count > 1 && new_inode) {
		if (S_ISREG(new_inode->i_mode)) {
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
		} else if (!list_empty(&new_dentry->d_subdirs)) {
			shrink_dcache_parent(new_dentry);
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

	nfs_invalidate_dircache(new_dir);
	nfs_invalidate_dircache(old_dir);
	error = nfs_proc_rename(NFS_DSERVER(old_dentry),
			NFS_FH(old_dentry->d_parent), old_dentry->d_name.name,
			NFS_FH(new_dentry->d_parent), new_dentry->d_name.name);
	if (!error) {
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

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
