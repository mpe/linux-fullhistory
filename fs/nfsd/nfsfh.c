/*
 * linux/fs/nfsd/nfsfh.c
 *
 * NFS server filehandle treatment.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/dcache.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>

#define NFSDDBG_FACILITY		NFSDDBG_FH
#define NFSD_PARANOIA 1
/* #define NFSD_DEBUG_VERBOSE 1 */

extern unsigned long num_physpages;

#define NFSD_FILE_CACHE 0
#define NFSD_DIR_CACHE  1
struct fh_entry {
	struct dentry * dentry;
	unsigned long reftime;
	ino_t	ino;
	dev_t	dev;
};

#define NFSD_MAXFH PAGE_SIZE/sizeof(struct fh_entry)
static struct fh_entry filetable[NFSD_MAXFH];
static struct fh_entry dirstable[NFSD_MAXFH];

static int nfsd_nr_verified = 0;
static int nfsd_nr_put = 0;
static unsigned long nfsd_next_expire = 0;

static int add_to_fhcache(struct dentry *, int);
static int nfsd_d_validate(struct dentry *);

static LIST_HEAD(fixup_head);
static LIST_HEAD(path_inuse);
static int nfsd_nr_paths = 0;
#define NFSD_MAX_PATHS 500

struct nfsd_fixup {
	struct list_head lru;
	struct dentry *dentry;
	unsigned long reftime;
	ino_t	ino;
	dev_t	dev;
};

struct nfsd_path {
	struct list_head lru;
	unsigned long reftime;
	int	users;
	ino_t	ino;
	dev_t	dev;
	char	name[1];
};

/*
 * Copy a dentry's path into the specified buffer.
 */
static int copy_path(char *buffer, struct dentry *dentry, int namelen)
{
	char *p, *b = buffer;
	int result = 0, totlen = 0, len; 

	while (1) {
		struct dentry *parent;
		dentry = dentry->d_covers;
		parent = dentry->d_parent;
		len = dentry->d_name.len;
		p = (char *) dentry->d_name.name + len;
		totlen += len;
		if (totlen > namelen)
			goto out;
		while (len--)
			*b++ = *(--p);
		if (dentry == parent)
			break;
		dentry = parent;
		totlen++;
		if (totlen > namelen)
			goto out;
		*b++ = '/';
	}
	*b = 0;

	/*
	 * Now reverse in place ...
	 */
	p = buffer;
	while (p < b) {
		char c = *(--b);
		*b = *p;
		*p++ = c;
	} 
	result = 1;
out:
	return result;
}

/*
 * Add a dentry's path to the path cache.
 */
static int add_to_path_cache(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *this;
	struct nfsd_path *new;
	int len, result = 0;

#ifdef NFSD_DEBUG_VERBOSE
printk("add_to_path_cache: cacheing %s/%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	/*
	 * Get the length of the full pathname.
	 */
restart:
	len = 0;
	this = dentry;
	while (1) {
		struct dentry *parent;
		this = this->d_covers;
		parent = this->d_parent;
		len += this->d_name.len;
		if (this == parent)
			break;
		this = parent;
		len++;
	}
	/*
	 * Allocate a structure to hold the path.
	 */
	new = kmalloc(sizeof(struct nfsd_path) + len, GFP_KERNEL);
	if (new) {
		new->users = 0;	
		new->reftime = jiffies;	
		new->ino = inode->i_ino;	
		new->dev = inode->i_dev;	
		result = copy_path(new->name, dentry, len);	
		if (!result)
			goto retry;
		list_add(&new->lru, &path_inuse);
		nfsd_nr_paths++;
#ifdef NFSD_DEBUG_VERBOSE
printk("add_to_path_cache: added %s, paths=%d\n", new->name, nfsd_nr_paths);
#endif
	}
	return result;

	/*
	 * If the dentry's path length changed, just try again ...
	 */
retry:
	kfree(new);
	printk("add_to_path_cache: path length changed, retrying\n");
	goto restart;
}

/*
 * Search for a path entry for the specified (dev, inode).
 */
struct nfsd_path *get_path_entry(dev_t dev, ino_t ino)
{
	struct nfsd_path *pe;
	struct list_head *tmp;

	for (tmp = path_inuse.next; tmp != &path_inuse; tmp = tmp->next) {
		pe = list_entry(tmp, struct nfsd_path, lru);
		if (pe->ino != ino)
			continue;
		if (pe->dev != dev)
			continue;
		list_del(tmp);
		list_add(tmp, &path_inuse);
		pe->users++;
		pe->reftime = jiffies;
#ifdef NFSD_PARANOIA
printk("get_path_entry: found %s for %s/%ld\n", pe->name, kdevname(dev), ino);
#endif
		return pe;
	}
	return NULL;
}

static void put_path(struct nfsd_path *pe)
{
	pe->users--;
}

static void free_path_entry(struct nfsd_path *pe)
{
	if (pe->users)
		printk("free_path_entry: %s in use, users=%d\n",
			pe->name, pe->users);
	list_del(&pe->lru);
	kfree(pe);
	nfsd_nr_paths--;
}

struct nfsd_getdents_callback {
	struct nfsd_dirent *dirent;
	int found;
	int checked;
};

struct nfsd_dirent {
	ino_t ino;
	int len;
	char name[256];
};

/*
 * A custom filldir function to search for the specified inode.
 */
static int filldir_ino(void * __buf, const char * name, int len, 
			off_t pos, ino_t ino)
{
	struct nfsd_getdents_callback *buf = __buf;
	struct nfsd_dirent *dirent = buf->dirent;
	int result = 0;

	buf->checked++;
	if (ino == dirent->ino) {
		buf->found = 1;
		dirent->len = len;
		memcpy(dirent->name, name, len);
		dirent->name[len] = 0;
		result = -1;
	}
	return result;
}

/*
 * Search a directory for the specified inode number.
 */
static int search_dir(struct dentry *parent, ino_t ino,
			struct nfsd_dirent *dirent)
{
	struct inode *inode = parent->d_inode;
	int error;
	struct file file;
	struct nfsd_getdents_callback buffer;

	error = -ENOTDIR;
	if (!inode || !S_ISDIR(inode->i_mode))
		goto out;
	error = -EINVAL;
	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto out;

	/*
	 * Open the directory ...
	 */
	error = init_private_file(&file, parent, FMODE_READ);
	if (error)
		goto out;
	error = -EINVAL;
	if (!file.f_op->readdir)
		goto out_close;

	/*
	 * Initialize the callback buffer.
	 */
	dirent->ino = ino;
	buffer.dirent = dirent;
	buffer.found = 0;

	while (1) {
		buffer.checked = 0;
		error = file.f_op->readdir(&file, &buffer, filldir_ino);
		if (error < 0)
			break;

		error = 0;
		if (buffer.found) {
#ifdef NFSD_DEBUG_VERBOSE
printk("search_dir: found %s\n", dirent->name);
#endif
			break;
		}
		error = -ENOENT;
		if (!buffer.checked)
			break;
	}

out_close:
	if (file.f_op->release)
		file.f_op->release(inode, &file);
out:
	return error;

}
	
/*
 * Find an entry in the cache matching the given dentry pointer.
 */
static struct fh_entry *find_fhe(struct dentry *dentry, int cache,
				struct fh_entry **empty)
{
	struct fh_entry *fhe;
	int i, found = (empty == NULL) ? 1 : 0;

	fhe = (cache == NFSD_FILE_CACHE) ? &filetable[0] : &dirstable[0];
	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (fhe->dentry == dentry) {
			fhe->reftime = jiffies;
			return fhe;
		}
		if (!found && !fhe->dentry) {
			found = 1;
			*empty = fhe;
		}
	}
	return NULL;
}

/*
 * Expire a cache entry.
 */
static void expire_fhe(struct fh_entry *empty, int cache)
{
	struct dentry *dentry = empty->dentry;

#ifdef NFSD_DEBUG_VERBOSE
printk("expire_fhe: expiring %s/%s, d_count=%d, ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count,empty->ino);
#endif
	empty->dentry = NULL;	/* no dentry */
	/*
	 * Add the parent to the dir cache before releasing the dentry.
	 */
	if (dentry != dentry->d_parent) {
		struct dentry *parent = dget(dentry->d_parent);
		if (add_to_fhcache(parent, NFSD_DIR_CACHE))
			nfsd_nr_verified++;
		else
			dput(parent);
	}
	/*
	 * If we're expiring a directory, copy its path.
	 */
	if (cache == NFSD_DIR_CACHE) {
		add_to_path_cache(dentry);
	}
	dput(dentry);
	nfsd_nr_put++;
}

/*
 * Look for an empty slot, or select one to expire.
 */
static void expire_slot(int cache)
{
	struct fh_entry *fhe, *empty = NULL;
	unsigned long oldest = -1;
	int i;

	fhe = (cache == NFSD_FILE_CACHE) ? &filetable[0] : &dirstable[0];
	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (!fhe->dentry)
			goto out;
		if (fhe->reftime < oldest) {
			oldest = fhe->reftime;
			empty = fhe;
		}
	}
	if (empty)
		expire_fhe(empty, cache);

out:
	return;
}

/*
 * Expire any cache entries older than a certain age.
 */
static void expire_old(int cache, int age)
{
	struct fh_entry *fhe;
	int i;

#ifdef NFSD_DEBUG_VERBOSE
printk("expire_old: expiring %s older than %d\n",
(cache == NFSD_FILE_CACHE) ? "file" : "dir", age);
#endif
	fhe = (cache == NFSD_FILE_CACHE) ? &filetable[0] : &dirstable[0];
	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (!fhe->dentry)
			continue;
		if ((jiffies - fhe->reftime) > age)
			expire_fhe(fhe, cache);
	}

	/*
	 * Trim the path cache ...
	 */
	while (nfsd_nr_paths > NFSD_MAX_PATHS) {
		struct nfsd_path *pe;
		pe = list_entry(path_inuse.prev, struct nfsd_path, lru);
		if (pe->users)
			break;
		free_path_entry(pe);
	}
}

/*
 * Add a dentry to the file or dir cache.
 *
 * Note: As NFS filehandles must have an inode, we don't accept
 * negative dentries.
 */
static int add_to_fhcache(struct dentry *dentry, int cache)
{
	struct fh_entry *fhe, *empty = NULL;
	struct inode *inode = dentry->d_inode;

	if (!inode) {
#ifdef NFSD_PARANOIA
printk("add_to_fhcache: %s/%s rejected, no inode!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
		return 0;
	}

repeat:
	fhe = find_fhe(dentry, cache, &empty);
	if (fhe) {
		return 0;
	}

	/*
	 * Not found ... make a new entry.
	 */
	if (empty) {
		empty->dentry = dentry;
		empty->reftime = jiffies;
		empty->ino = inode->i_ino;
		empty->dev = inode->i_dev;
		return 1;
	}

	expire_slot(cache);
	goto repeat;
}

/*
 * Find an entry in the dir cache for the specified inode number.
 */
static struct fh_entry *find_fhe_by_ino(dev_t dev, ino_t ino)
{
	struct fh_entry * fhe = &dirstable[0];
	int i;

	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (fhe->ino == ino && fhe->dev == dev) {
			fhe->reftime = jiffies;
			return fhe;
		}
	}
	return NULL;
}

/*
 * Find the (directory) dentry with the specified (dev, inode) number.
 * Note: this leaves the dentry in the cache.
 */
static struct dentry *find_dentry_by_ino(dev_t dev, ino_t ino)
{
	struct fh_entry *fhe;
	struct nfsd_path *pe;
	struct dentry * dentry;

#ifdef NFSD_DEBUG_VERBOSE
printk("find_dentry_by_ino: looking for inode %ld\n", ino);
#endif
	fhe = find_fhe_by_ino(dev, ino);
	if (fhe) {
		dentry = dget(fhe->dentry);
		goto out;
	}

	/*
	 * Search the path cache ...
	 */
	dentry = NULL;
	pe = get_path_entry(dev, ino);
	if (pe) {
		struct dentry *res;
		res = lookup_dentry(pe->name, NULL, 0);
		if (!IS_ERR(res)) {
			struct inode *inode = res->d_inode;
			if (inode && inode->i_ino == ino &&
				     inode->i_dev == dev) {
				dentry = res;
#ifdef NFSD_PARANOIA
printk("find_dentry_by_ino: found %s/%s, ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, ino);
#endif
				if (add_to_fhcache(dentry, NFSD_DIR_CACHE)) {
					dget(dentry);
					nfsd_nr_verified++;
				}
			} else {
				dput(res);
			}
		} else {
#ifdef NFSD_PARANOIA
printk("find_dentry_by_ino: %s lookup failed\n", pe->name);
#endif
		}
		put_path(pe);
	}
out:
	return dentry;
}

/*
 * Look for an entry in the file cache matching the dentry pointer,
 * and verify that the (dev, inode) numbers are correct. If found,
 * the entry is removed from the cache.
 */
static struct dentry *find_dentry_in_fhcache(struct knfs_fh *fh)
{
	struct fh_entry * fhe;

	fhe = find_fhe(fh->fh_dcookie, NFSD_FILE_CACHE, NULL);
	if (fhe) {
		struct dentry *parent, *dentry = fhe->dentry;
		struct inode *inode = dentry->d_inode;
		if (!inode) {
#ifdef NFSD_PARANOIA
printk("find_dentry_in_fhcache: %s/%s has no inode!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			goto out;
		}
		if (inode->i_ino != fh->fh_ino || inode->i_dev != fh->fh_dev) {
#ifdef NFSD_PARANOIA
printk("find_dentry_in_fhcache: %s/%s mismatch, ino=%ld, fh_ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, inode->i_ino, fh->fh_ino);
#endif
			goto out;
		}
		fhe->dentry = NULL;
		fhe->ino = 0;
		fhe->dev = 0;
		nfsd_nr_put++;
		/*
		 * Make sure the parent is in the dir cache ...
		 */
		parent = dget(dentry->d_parent);
		if (add_to_fhcache(parent, NFSD_DIR_CACHE))
			nfsd_nr_verified++;
		else
			dput(parent);
		return dentry;
	}
out:
	return NULL;
}

/*
 * Look for an entry in the parent directory with the specified
 * inode number.
 */
static struct dentry *lookup_by_inode(struct dentry *parent, ino_t ino)
{
	struct dentry *dentry;
	int error;
	struct nfsd_dirent dirent;

	/*
	 * Search the directory for the inode number.
	 */
	error = search_dir(parent, ino, &dirent);
	if (error) {
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: ino %ld not found in %s\n", ino, parent->d_name.name);
#endif
		goto no_entry;
	}

	dentry = lookup_dentry(dirent.name, dget(parent), 0);
	if (!IS_ERR(dentry)) {
		if (dentry->d_inode && dentry->d_inode->i_ino == ino)
			goto out;
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: %s/%s inode mismatch??\n",
parent->d_name.name, dentry->d_name.name);
#endif
		dput(dentry);
	} else {
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: %s lookup failed, error=%ld\n",
dirent.name, PTR_ERR(dentry));
#endif
	}

no_entry:
	dentry = NULL;
out:
	return dentry;
}

/*
 * The is the basic lookup mechanism for turning an NFS filehandle 
 * into a dentry. There are several levels to the search:
 * (1) Look for the dentry pointer the short-term fhcache,
 *     and verify that it has the correct inode number.
 *
 * (2) Try to validate the dentry pointer in the filehandle,
 *     and verify that it has the correct inode number.
 *
 * (3) Search for the parent dentry in the dir cache, and then
 *     look for the name matching the inode number.
 *
 * (4) The most general case ... search the whole volume for the inode.
 *
 * If successful, we return a dentry with the use count incremented.
 */
static struct dentry *
find_fh_dentry(struct knfs_fh *fh)
{
	struct dentry *dentry, *parent;

	/*
	 * Stage 1: Look for the dentry in the short-term fhcache.
	 */
	dentry = find_dentry_in_fhcache(fh);
	if (dentry) {
#ifdef NFSD_DEBUG_VERBOSE
printk("find_fh_dentry: found %s/%s, d_count=%d, ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count,fh->fh_ino);
#endif
		goto out;
	}

	/*
	 * Stage 2: Attempt to validate the dentry in the filehandle.
	 */
	dentry = fh->fh_dcookie;
	if (nfsd_d_validate(dentry)) {
		struct inode * dir = dentry->d_parent->d_inode;

		if (dir->i_ino == fh->fh_dirino && dir->i_dev == fh->fh_dev) {
			struct inode * inode = dentry->d_inode;

			/*
			 * NFS filehandles must always have an inode,
			 * so we won't accept a negative dentry.
			 */
			if (!inode) {
#ifdef NFSD_PARANOIA
printk("find_fh_dentry: %s/%s negative, can't match %ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, fh->fh_ino);
#endif
			} else if (inode->i_ino == fh->fh_ino &&
				   inode->i_dev == fh->fh_dev) {
				dget(dentry);
#ifdef NFSD_DEBUG_VERBOSE
printk("find_fh_dentry: validated %s/%s, ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, inode->i_ino);
#endif
				goto out;
			} else {
#ifdef NFSD_PARANOIA
printk("find_fh_dentry: %s/%s mismatch, ino=%ld, fh_ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, inode->i_ino, fh->fh_ino);
#endif
			}
		} else {
#ifdef NFSD_PARANOIA
printk("find_fh_dentry: %s/%s mismatch, parent ino=%ld, dirino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dir->i_ino, fh->fh_dirino);
#endif
		}
	}

	/*
	 * Stage 3: Look for the parent dentry in the fhcache ...
	 */
	parent = find_dentry_by_ino(fh->fh_dev, fh->fh_dirino);
	if (parent) {
		/*
		 * ... then search for the inode in the parent directory.
		 */
		dentry = lookup_by_inode(parent, fh->fh_ino);
		dput(parent);
		if (dentry)
			goto out;
	}

	/*
	 * Stage 4: Search the whole volume.
	 */
#ifdef NFSD_PARANOIA
printk("find_fh_dentry: %s, %ld/%ld not found -- need full search!\n",
kdevname(fh->fh_dev), fh->fh_dirino, fh->fh_ino);
#endif
	dentry = NULL;
	
out:
	/*
	 * Perform any needed housekeeping ...
	 * N.B. move this into one of the daemons ...
	 */
	if (jiffies >= nfsd_next_expire) {
		expire_old(NFSD_FILE_CACHE,  5*HZ);
		expire_old(NFSD_DIR_CACHE , 60*HZ);
		nfsd_next_expire = jiffies + 5*HZ;
	}
	return dentry;
}

/*
 * Perform sanity checks on the dentry in a client's file handle.
 *
 * Note that the filehandle dentry may need to be freed even after
 * an error return.
 */
u32
fh_verify(struct svc_rqst *rqstp, struct svc_fh *fhp, int type, int access)
{
	struct svc_export *exp;
	struct dentry	*dentry;
	struct inode	*inode;
	struct knfs_fh	*fh = &fhp->fh_handle;
	u32		error = 0;

	if(fhp->fh_dverified)
		goto out;

	dprintk("nfsd: fh_lookup(exp %x/%ld fh %p)\n",
			fh->fh_xdev, fh->fh_xino, fh->fh_dcookie);

	/*
	 * Look up the export entry.
	 * N.B. We need to lock this while in use ...
	 */
	error = nfserr_stale;
	exp = exp_get(rqstp->rq_client, fh->fh_xdev, fh->fh_xino);
	if (!exp) /* export entry revoked */
		goto out;

	/* Check if the request originated from a secure port. */
	error = nfserr_perm;
	if (!rqstp->rq_secure && EX_SECURE(exp)) {
		printk(KERN_WARNING
			"nfsd: request from insecure port (%08lx:%d)!\n",
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port));
		goto out;
	}

	/* Set user creds if we haven't done so already. */
	nfsd_setuser(rqstp, exp);

	/*
	 * Look up the dentry using the NFS fh.
	 */
	error = nfserr_stale;
	dentry = find_fh_dentry(fh);
	if (!dentry)
		goto out;
	/*
	 * Note: it's possible that the returned dentry won't be the
	 * one in the filehandle.  We can correct the FH for our use,
	 * but unfortunately the client will keep sending the broken
	 * one.  Hopefully the lookup will keep patching things up..
	 */
	fhp->fh_dentry = dentry;
	fhp->fh_export = exp;
	fhp->fh_dverified = 1;
	nfsd_nr_verified++;

	/* Type check. The correct error return for type mismatches
	 * does not seem to be generally agreed upon. SunOS seems to
	 * use EISDIR if file isn't S_IFREG; a comment in the NFSv3
	 * spec says this is incorrect (implementation notes for the
	 * write call).
	 */
	inode = dentry->d_inode;
	if (type > 0 && (inode->i_mode & S_IFMT) != type) {
		error = (type == S_IFDIR)? nfserr_notdir : nfserr_isdir;
		goto out;
	}
	if (type < 0 && (inode->i_mode & S_IFMT) == -type) {
		error = (type == -S_IFDIR)? nfserr_notdir : nfserr_isdir;
		goto out;
	}

	/* Finally, check access permissions. */
	error = nfsd_permission(fhp->fh_export, dentry, access);
out:
	return error;
}

/*
 * Compose a filehandle for an NFS reply.
 *
 * Note that when first composed, the dentry may not yet have
 * an inode.  In this case a call to fh_update should be made
 * before the fh goes out on the wire ...
 */
void
fh_compose(struct svc_fh *fhp, struct svc_export *exp, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;

	dprintk("nfsd: fh_compose(exp %x/%ld dentry %p)\n",
			exp->ex_dev, exp->ex_ino, dentry);

	/*
	 * N.B. We shouldn't need to init the fh -- the call to fh_compose
	 * may not be done on error paths, but the cleanup must call fh_put.
	 * Fix this soon!
	 */
	fh_init(fhp);
	fhp->fh_handle.fh_dcookie = dentry;
	if (inode) {
		fhp->fh_handle.fh_ino = inode->i_ino;
	}
	fhp->fh_handle.fh_dirino = dentry->d_parent->d_inode->i_ino;
	fhp->fh_handle.fh_dev = dentry->d_parent->d_inode->i_dev;
	fhp->fh_handle.fh_xdev = exp->ex_dev;
	fhp->fh_handle.fh_xino = exp->ex_ino;

	fhp->fh_dentry = dentry; /* our internal copy */
	fhp->fh_export = exp;

	/* We stuck it there, we know it's good. */
	fhp->fh_dverified = 1;
	nfsd_nr_verified++;
}

/*
 * Update filehandle information after changing a dentry.
 */
void
fh_update(struct svc_fh *fhp)
{
	struct dentry *dentry;
	struct inode *inode;

	if (!fhp->fh_dverified) {
		printk("fh_update: fh not verified!\n");
		goto out;
	}

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;
	if (!inode) {
		printk("fh_update: %s/%s still negative!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
		goto out;
	}
	fhp->fh_handle.fh_ino = inode->i_ino;
out:
	return;
}

/*
 * Release a filehandle.  If the filehandle carries a dentry count,
 * we add the dentry to the short-term cache rather than release it.
 */
void
fh_put(struct svc_fh *fhp)
{
	if (fhp->fh_dverified) {
		struct dentry * dentry = fhp->fh_dentry;
		fh_unlock(fhp);
		fhp->fh_dverified = 0;
		if (!dentry->d_count) {
			printk("fh_put: %s/%s has d_count 0!\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
			return;
		}
		if (!dentry->d_inode || !add_to_fhcache(dentry, 0)) {
			dput(dentry);
			nfsd_nr_put++;
		}
	}
}

/*
 * Verify that the FH dentry is still a valid dentry pointer.
 * After making some preliminary checks, we ask VFS to verify
 * that it is indeed a dentry.
 */
static int nfsd_d_validate(struct dentry *dentry)
{
	unsigned long dent_addr = (unsigned long) dentry;
	unsigned long min_addr = PAGE_OFFSET;
	unsigned long max_addr = min_addr + (num_physpages << PAGE_SHIFT);
	unsigned long align_mask = 0x1F;
	unsigned int len;
	int valid = 0;

	if (dent_addr < min_addr)
		goto bad_addr;
	if (dent_addr > max_addr - sizeof(struct dentry))
		goto bad_addr;
	if ((dent_addr & ~align_mask) != dent_addr)
		goto bad_addr;
	/*
	 * Looks safe enough to dereference ...
	 */
	len = dentry->d_name.len;
	if (len > NFS_MAXNAMLEN)
		goto bad_length;

	valid = d_validate(dentry, dentry->d_parent, dentry->d_name.hash, len);

out:
	return valid;

bad_addr:
	printk("nfsd_d_validate: invalid address %lx\n", dent_addr);
	goto out;
bad_length:
	printk("nfsd_d_validate: invalid length %d\n", len);
	goto out;
}

/*
 * Free the dentry and path caches.
 */
void nfsd_fh_free(void)
{
	struct fh_entry *fhe;
	struct list_head *tmp;
	int i, pass = 2;

	fhe = &filetable[0];
	while (pass--) {
		for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
			struct dentry *dentry = fhe->dentry;
			if (!dentry)
				continue;
			fhe->dentry = NULL;
			dput(dentry);
			nfsd_nr_put++;
		}
		fhe = &dirstable[0];
	}

	i = 0;
	while ((tmp = path_inuse.next) != &path_inuse) {
		struct nfsd_path *pe = list_entry(tmp, struct nfsd_path, lru);
		free_path_entry(pe);
		i++;
	}
	printk("nfsd_fh_free: %d paths freed\n", i);

	printk("nfsd_fh_free: verified %d, put %d\n",
		nfsd_nr_verified, nfsd_nr_put);
}

void
nfsd_fh_init(void)
{
	memset(filetable, 0, NFSD_MAXFH*sizeof(struct fh_entry));
	memset(dirstable, 0, NFSD_MAXFH*sizeof(struct fh_entry));
	INIT_LIST_HEAD(&path_inuse);

	printk("nfsd_init: initialized fhcache, entries=%lu\n", NFSD_MAXFH);
}
