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

/* needed by smbfs as well ... move to dcache? */
extern void nfs_renew_times(struct dentry *);
extern void nfs_invalidate_dircache_sb(struct super_block *);
#define NFS_PARANOIA 1

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

static int nfs_dir_open(struct inode * inode, struct file * file);
static ssize_t nfs_dir_read(struct file *, char *, size_t, loff_t *);
static int nfs_readdir(struct file *, void *, filldir_t);
static int nfs_lookup(struct inode *, struct dentry *);
static int nfs_create(struct inode *, struct dentry *, int);
static int nfs_mkdir(struct inode *, struct dentry *, int);
static int nfs_rmdir(struct inode *, struct dentry *);
static int nfs_unlink(struct inode *, struct dentry *);
static int nfs_symlink(struct inode *, struct dentry *, const char *);
static int nfs_link(struct inode *, struct inode *, struct dentry *);
static int nfs_mknod(struct inode *, struct dentry *, int, int);
static int nfs_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);

static struct file_operations nfs_dir_operations = {
	NULL,			/* lseek - default */
	nfs_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	nfs_readdir,		/* readdir */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	NULL,			/* mmap */
	nfs_dir_open,		/* open - revalidate */
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
	dfprintk(VFS, "NFS: nfs_dir_open(%x/%ld)\n", dir->i_dev, dir->i_ino);
	return nfs_revalidate_inode(NFS_SERVER(dir), dir);
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
	struct inode 		*inode = filp->f_dentry->d_inode;
	static struct wait_queue *readdir_wait = NULL;
	struct wait_queue	**waitp = NULL;
	struct nfs_dirent	*cache, *free;
	unsigned long		age, dead;
	u32			cookie;
	int			ismydir, result;
	int			i, j, index = 0;
	__u32			*entry;
	char			*name, *start;

	dfprintk(VFS, "NFS: nfs_readdir(%x/%ld)\n", inode->i_dev, inode->i_ino);
	if (!inode || !S_ISDIR(inode->i_mode)) {
		printk("nfs_readdir: inode is NULL or not a directory\n");
		return -EBADF;
	}

	if ((result = nfs_revalidate_inode(NFS_SERVER(inode), inode)) < 0)
		return result;

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

		result = nfs_proc_readdir(NFS_SERVER(inode), NFS_FH(inode),
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
	int		freed = 0;

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
			freed++;
		}
	}
#ifdef NFS_PARANOIA
if (freed)
printk("nfs_invalidate_dircache_sb: freed %d pages from %s\n", 
freed, kdevname(sb->s_dev));
#endif
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
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that
 * lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 *
 * The decision to drop the dentry should probably be
 * smarter than this. Right now we believe in directories
 * for 10 seconds, and in normal files for five..
 */
static int nfs_lookup_revalidate(struct dentry * dentry)
{
	unsigned long time = jiffies - dentry->d_time;
	unsigned long max = 5*HZ;

	if (dentry->d_inode) {
		if (is_bad_inode(dentry->d_inode)) {
#ifdef NFS_PARANOIA
printk("nfs_lookup_validate: %s/%s has dud inode\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			goto bad;
		}
		if (S_ISDIR(dentry->d_inode->i_mode))
			max = 10*HZ;
	}

	return (time < max) || IS_ROOT(dentry);
bad:
	return 0;
}

static void nfs_silly_delete(struct dentry *);

static struct dentry_operations nfs_dentry_operations = {
	nfs_lookup_revalidate,	/* d_validate(struct dentry *) */
	0,			/* d_hash */
	0,			/* d_compare */
	nfs_silly_delete,	/* d_delete(struct dentry *) */
};

/*
 * Whenever a lookup succeeds, we know the parent directories
 * are all valid, so we want to update the dentry timestamps.
 */
void nfs_renew_times(struct dentry * dentry)
{
	for (;;) {
		dentry->d_time = jiffies;
		if (dentry == dentry->d_parent)
			break;
		dentry = dentry->d_parent;
	}
}

static int nfs_lookup(struct inode *dir, struct dentry * dentry)
{
	struct inode *inode;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;
	int len = dentry->d_name.len;
	int error;

	dfprintk(VFS, "NFS: lookup(%x/%ld, %.*s)\n",
				dir->i_dev, dir->i_ino, len, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_lookup: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	error = nfs_proc_lookup(NFS_SERVER(dir), NFS_FH(dir), 
				dentry->d_name.name, &fhandle, &fattr);
	inode = NULL;
	if (error == -ENOENT)
		goto no_entry;
	if (!error) {
		error = -EACCES;
		inode = nfs_fhget(dir->i_sb, &fhandle, &fattr);
		if (inode) {
	    no_entry:
			dentry->d_op = &nfs_dentry_operations;
			d_add(dentry, inode);
			nfs_renew_times(dentry);
			error = 0;
		}
	}
	return error;
}

/*
 * Code common to create, mkdir, and mknod.
 */
static int nfs_instantiate(struct inode *dir, struct dentry *dentry,
			struct nfs_fattr *fattr, struct nfs_fh *fhandle)
{
	struct inode *inode;
	int error = -EACCES;

	inode = nfs_fhget(dir->i_sb, fhandle, fattr);
	if (inode) {
		nfs_invalidate_dircache(dir);
		d_instantiate(dentry, inode);
		nfs_renew_times(dentry);
		error = 0;
	}
	return error;
}

static int nfs_create(struct inode *dir, struct dentry * dentry, int mode)
{
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;
	int error;

	dfprintk(VFS, "NFS: create(%x/%ld, %s\n",
				dir->i_dev, dir->i_ino, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_create: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dir),
			dentry->d_name.name, &sattr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dir, dentry, &fattr, &fhandle);
	return error;
}

static int nfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int rdev)
{
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;
	int error;

	dfprintk(VFS, "NFS: mknod(%x/%ld, %s\n",
				dir->i_dev, dir->i_ino, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_mknod: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	if (S_ISCHR(mode) || S_ISBLK(mode))
		sattr.size = rdev; /* get out your barf bag */

	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dir),
				dentry->d_name.name, &sattr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dir, dentry, &fattr, &fhandle);
	return error;
}

static int nfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;
	int error;

	dfprintk(VFS, "NFS: mkdir(%x/%ld, %s\n",
				dir->i_dev, dir->i_ino, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_mkdir: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	sattr.mode = mode;
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	error = nfs_proc_mkdir(NFS_SERVER(dir), NFS_FH(dir),
				dentry->d_name.name, &sattr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dir, dentry, &fattr, &fhandle);
	return error;
}

/*
 *  Update inode->i_nlink immediately after a successful operation.
 *  (See comments for nfs_unlink.)
 */
static int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%x/%ld, %s\n",
				dir->i_dev, dir->i_ino, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_rmdir: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	error = nfs_proc_rmdir(NFS_SERVER(dir), NFS_FH(dir), dentry->d_name.name);
	if (!error) {
		if (dentry->d_inode->i_nlink)
			dentry->d_inode->i_nlink --;
		nfs_invalidate_dircache(dir);
		nfs_renew_times(dentry);
		d_delete(dentry);
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
 *  somebody attempts to remove the "silly renamed" dentry
 *  itself. Which is something that I consider evil. Your opinion may
 *  vary.
 *  BUT:
 *  Now that I compute the hash value right, it should be possible to simply
 *  check for the DCACHE_NFSFS_RENAMED flag in dentry->d_flag instead of
 *  doing the string compare.
 *  WHICH MEANS:
 *  This offers the opportunity to shorten the temp name. Currently, I use
 *  the hex representation of i_ino + the hex value of jiffies. This
 *  sums up to as much as 36 characters for a 64 bit machine, and needs
 *  20 chars on a 32 bit machine. Have a look at jiffiesize etc.
 *  QUINTESSENCE
 *  The use of i_ino is simply cosmetic. All we need is a unique temp
 *  file name for the .nfs files. The hex representation of "jiffies"
 *  seemed to be adequate. And as we retry in case such a file already
 *  exists we are guaranteed to succed (after some jiffies have passed
 *  by :)
 */

static
struct dentry *nfs_silly_lookup(struct dentry *parent, char *silly, int slen)
{
	struct qstr    sqstr;
	struct dentry *sdentry;
	int i, error;

	sqstr.name = silly;
	sqstr.len  = slen;
	sqstr.hash = init_name_hash();
	for (i= 0; i < slen; i++)
		sqstr.hash = partial_name_hash(silly[i], sqstr.hash);
	sqstr.hash = end_name_hash(sqstr.hash);
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
	int            error;
	struct dentry *sdentry;

	if (dentry->d_count == 1) {
		return -EIO;  /* No need to silly rename. */
	}

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		return -EBUSY; /* don't allow to unlink silly inode -- nope,
				* think a bit: silly DENTRY, NOT inode --
				* itself
				*/
	}

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
		if (IS_ERR(sdentry)) {
			return -EIO; /* FIXME ? */
		}		
	} while(sdentry->d_inode != NULL); /* need negative lookup */

	error = nfs_proc_rename(NFS_SERVER(dir),
				NFS_FH(dir), dentry->d_name.name,
				NFS_FH(dir), silly);
	if (!error) {
		nfs_invalidate_dircache(dir);
		nfs_renew_times(dentry);
		d_move(dentry, sdentry);
		dentry->d_flags |= DCACHE_NFSFS_RENAMED;
 		/* If we return 0 we don't unlink */
	}
	dput(sdentry);
	return error;
}

static void nfs_silly_delete(struct dentry *dentry)
{
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		struct inode *dir = dentry->d_parent->d_inode;
		int error;
		
		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;

		/* Unhash it first */
		d_drop(dentry);
		dfprintk(VFS, "trying to unlink %s\n", dentry->d_name.name);
		error = nfs_proc_remove(NFS_SERVER(dir),
					NFS_FH(dir), dentry->d_name.name);
		if (error < 0)
			printk("NFS " __FUNCTION__ " failed (err = %d)\n",
			       -error);
		if (dentry->d_inode) {
			if (dentry->d_inode->i_nlink)
				dentry->d_inode->i_nlink --;
		} else
			printk("nfs_silly_delete: negative dentry %s/%s\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name);
		nfs_invalidate_dircache(dir);
		/*
		 * The dentry is unhashed, but we want to make it negative.
		 */
		d_delete(dentry);
	}
}

/*  We do silly rename. In case sillyrename() returns -EBUSY, the inode
 *  belongs to an active ".nfs..." file and we return -EBUSY.
 *
 *  If sillyrename() returns 0, we do nothing, otherwise we unlink.
 * 
 *  Updating inode->i_nlink here rather than waiting for the next
 *  nfs_refresh_inode() is not merely cosmetic; once an object has
 *  been deleted, we want to get rid of the inode locally.  The NFS
 *  server may reuse the fileid for a new inode, and we don't want
 *  that to be confused with this inode.
 */
static int nfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: unlink(%x/%ld, %s)\n",
				dir->i_dev, dir->i_ino, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_unlink: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	error = nfs_sillyrename(dir, dentry);

	if (error && error != -EBUSY) {
		error = nfs_proc_remove(NFS_SERVER(dir),
					NFS_FH(dir), dentry->d_name.name);
		if (!error) {
			if (dentry->d_inode->i_nlink)
				dentry->d_inode->i_nlink --;
			nfs_invalidate_dircache(dir);
			nfs_renew_times(dentry);
			d_delete(dentry);
		}
	}

	return error;
}

static int nfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct nfs_sattr sattr;
	int error;

	dfprintk(VFS, "NFS: symlink(%x/%ld, %s, %s)\n",
				dir->i_dev, dir->i_ino, dentry->d_name.name, symname);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_symlink: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	if (strlen(symname) > NFS_MAXPATHLEN)
		return -ENAMETOOLONG;

	sattr.mode = S_IFLNK | S_IRWXUGO; /* SunOS 4.1.2 crashes without this! */
	sattr.uid = sattr.gid = sattr.size = (unsigned) -1;
	sattr.atime.seconds = sattr.mtime.seconds = (unsigned) -1;

	error = nfs_proc_symlink(NFS_SERVER(dir), NFS_FH(dir),
				dentry->d_name.name, symname, &sattr);
	if (!error) {
		nfs_invalidate_dircache(dir);
		nfs_renew_times(dentry->d_parent);
		/*  this looks _funny_ doesn't it? But: nfs_proc_symlink()
		 *  only fills in sattr, not fattr. Thus nfs_fhget() cannot be
		 *  called, it would be pointless, without a valid fattr
		 *  argument. Other possibility: call nfs_proc_lookup()
		 *  HERE. But why? If somebody wants to reference this
		 *  symlink, the cached_lookup() will fail, and
		 *  nfs_proc_symlink() will be called anyway.
		 */
		d_drop(dentry);
	}
	return error;
}

static int nfs_link(struct inode *inode, struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: link(%x/%ld -> %x/%ld, %s)\n",
				inode->i_dev, inode->i_ino,
				dir->i_dev, dir->i_ino, dentry->d_name.name);

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("nfs_link: dir is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	error = nfs_proc_link(NFS_SERVER(inode), NFS_FH(inode), NFS_FH(dir),
				dentry->d_name.name);
	if (!error) {
		nfs_invalidate_dircache(dir);
		inode->i_count ++;
		inode->i_nlink ++; /* no need to wait for nfs_refresh_inode() */
		d_instantiate(dentry, inode);
		error = 0;
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
 */
static int nfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	int error;

	dfprintk(VFS, "NFS: rename(%x/%ld, %s -> %x/%ld, %s)\n",
				old_dir->i_dev, old_dir->i_ino, old_dentry->d_name.name,
				new_dir->i_dev, new_dir->i_ino, new_dentry->d_name.name);

	if (!old_dir || !S_ISDIR(old_dir->i_mode)) {
		printk("nfs_rename: old inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (!new_dir || !S_ISDIR(new_dir->i_mode)) {
		printk("nfs_rename: new inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (old_dentry->d_name.len > NFS_MAXNAMLEN || new_dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	if (new_dir != old_dir) {
		error = nfs_sillyrename(old_dir, old_dentry);

		if (error == -EBUSY) {
			return -EBUSY;
		} else if (error == 0) { /* did silly rename stuff */
			error = nfs_link(old_dentry->d_inode,
					 new_dir, new_dentry);
			
			return error;
		}
		/* no need for silly rename, proceed as usual */
	}
	error = nfs_proc_rename(NFS_SERVER(old_dir),
				NFS_FH(old_dir), old_dentry->d_name.name,
				NFS_FH(new_dir), new_dentry->d_name.name);
	if (!error) {
		nfs_invalidate_dircache(old_dir);
		nfs_invalidate_dircache(new_dir);
		/*
		 * We know these paths are still valid ...
		 */
		nfs_renew_times(old_dentry);
		nfs_renew_times(new_dentry->d_parent);

		/* Update the dcache */
		d_move(old_dentry, new_dentry);
	}
	return error;
}

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
