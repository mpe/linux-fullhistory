/*
 * linux/fs/nfsd/nfsfh.c
 *
 * NFS server file handle treatment.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 * Portions Copyright (C) 1999 G. Allen Morris III <gam3@acm.org>
 * Extensive rewrite by Neil Brown <neilb@cse.unsw.edu.au> Southern-Spring 1999
 */

#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/dcache.h>
#include <asm/pgtable.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>

#define NFSDDBG_FACILITY		NFSDDBG_FH
#define NFSD_PARANOIA 1
/* #define NFSD_DEBUG_VERBOSE 1 */


static int nfsd_nr_verified = 0;
static int nfsd_nr_put = 0;


struct nfsd_getdents_callback {
	struct qstr *name;	/* name that was found. name->name already points to a buffer */
	unsigned long ino;	/* the inum we are looking for */
	int found;		/* inode matched? */
	int sequence;		/* sequence counter */
};

/*
 * A rather strange filldir function to capture
 * the name matching the specified inode number.
 */
static int filldir_one(void * __buf, const char * name, int len,
			off_t pos, ino_t ino)
{
	struct nfsd_getdents_callback *buf = __buf;
	struct qstr *qs = buf->name;
	char *nbuf = (char*)qs->name; /* cast is to get rid of "const" */
	int result = 0;

	buf->sequence++;
#ifdef NFSD_DEBUG_VERBOSE
dprintk("filldir_one: seq=%d, ino=%ld, name=%s\n", buf->sequence, ino, name);
#endif
	if (buf->ino == ino) {
		qs->len = len;
		memcpy(nbuf, name, len);
		nbuf[len] = '\0';
		buf->found = 1;
		result = -1;
	}
	return result;
}

/*
 * Read a directory and return the name of the specified entry.  i_sem is already down().
 */
static int get_ino_name(struct dentry *dentry, struct qstr *name, unsigned long ino)
{
	struct inode *dir = dentry->d_inode;
	int error;
	struct file file;
	struct nfsd_getdents_callback buffer;

	error = -ENOTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;
	error = -EINVAL;
	if (!dir->i_op || !dir->i_op->default_file_ops)
		goto out;
	/*
	 * Open the directory ...
	 */
	error = init_private_file(&file, dentry, FMODE_READ);
	if (error)
		goto out;
	error = -EINVAL;
	if (!file.f_op->readdir)
		goto out_close;

	buffer.name = name;
	buffer.ino = ino;
	buffer.found = 0;
	buffer.sequence = 0;
	while (1) {
		int old_seq = buffer.sequence;
		error = file.f_op->readdir(&file, &buffer, filldir_one);
		if (error < 0)
			break;

		error = 0;
		if (buffer.found)
			break;
		error = -ENOENT;
		if (old_seq == buffer.sequence)
			break;
	}

out_close:
	if (file.f_op->release)
		file.f_op->release(dir, &file);
out:
	return error;
}

/* this should be provided by each filesystem in an nfsd_operations interface as
 * iget isn't really the right interface
 */
static struct dentry *nfsd_iget(struct super_block *sb, unsigned long ino, __u32 generation)
{

	/* iget isn't really right if the inode is currently unallocated!!
	 * This should really all be done inside each filesystem
	 *
	 * ext2fs' read_inode has been strengthed to return a bad_inode if the inode
	 *   had been deleted.
	 *
	 * Currently we don't know the generation for parent directory, so a generation
	 * of 0 means "accept any"
	 */
	struct inode *inode;
	struct list_head *lp;
	struct dentry *result;
	inode = iget(sb, ino);
	if (is_bad_inode(inode)
	    || (generation && inode->i_generation != generation)
		) {
		/* we didn't find the right inode.. */
		dprintk("fh_verify: Inode %lu, Bad count: %d %d or version  %u %u\n",
			inode->i_ino,
			inode->i_nlink, inode->i_count,
			inode->i_generation,
			generation);

		iput(inode);
		return NULL;
	}
	/* now to find a dentry.
	 * If possible, get a well-connected one
	 */
	for (lp = inode->i_dentry.next; lp != &inode->i_dentry ; lp=lp->next) {
		result = list_entry(lp,struct dentry, d_alias);
		if (! IS_ROOT(result) || inode->i_sb->s_root == result) {
			dget(result);
			iput(inode);
			return result;
		}
	}
	result = d_alloc_root(inode);
	if (result == NULL) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	d_rehash(result); /* so a dput won't loose it */
	return result;
}

/* this routine links an IS_ROOT dentry into the dcache tree.  It gains "parent"
 * as a parent and "name" as a name
 * It should possibly go in dcache.c
 */
int d_splice(struct dentry *target, struct dentry *parent, struct qstr *name)
{
	struct dentry *tdentry;
#ifdef NFSD_PARANOIA
	if (!IS_ROOT(target))
		printk("nfsd: d_splice with no-root target: %s/%s\n", parent->d_name.name, name->name);
#endif
	name->hash = full_name_hash(name->name, name->len);
	tdentry = d_alloc(parent, name);
	if (tdentry == NULL)
		return -ENOMEM;
	d_move(target, tdentry);

	/* tdentry will have been made a "child" of target (the parent of target)
	 * make it an IS_ROOT instead
	 */
	list_del(&tdentry->d_child);
	tdentry->d_parent = tdentry;
	d_rehash(target);
	dput(tdentry);
	return 0;
}

/* this routine finds the dentry of the parent of a given directory
 * it should be in the filesystem accessed by nfsd_operations
 * it assumes lookup("..") works.
 */
struct dentry *nfsd_findparent(struct dentry *child)
{
	struct dentry *tdentry, *pdentry;
	tdentry = d_alloc(child, &(const struct qstr) {"..", 2, 0});
	if (!tdentry)
		return ERR_PTR(-ENOMEM);

	/* I'm going to assume that if the returned dentry is different, then
	 * it is well connected.  But nobody returns different dentrys do they?
	 */
	pdentry = child->d_inode->i_op->lookup(child->d_inode, tdentry);
	d_drop(tdentry); /* we never want ".." hashed */
	if (!pdentry) {
		/* I don't want to return a ".." dentry.
		 * I would prefer to return an unconnected "IS_ROOT" dentry,
		 * though a properly connected dentry is even better
		 */
		/* if first or last of alias list is not tdentry, use that
		 * else make a root dentry
		 */
		struct list_head *aliases = &tdentry->d_inode->i_dentry;
		if (aliases->next != aliases) {
			pdentry = list_entry(aliases->next, struct dentry, d_alias);
			if (pdentry == tdentry)
				pdentry = list_entry(aliases->prev, struct dentry, d_alias);
			if (pdentry == tdentry)
				pdentry = NULL;
			if (pdentry) dget(pdentry);
		}
		if (pdentry == NULL) {
			pdentry = d_alloc_root(igrab(tdentry->d_inode));
			if (pdentry) d_rehash(pdentry);
		}
		if (pdentry == NULL)
			pdentry = ERR_PTR(-ENOMEM);
	}
	dput(tdentry); /* it is not hashed, it will be discarded */
	return pdentry;
}

static struct dentry *splice(struct dentry *child, struct dentry *parent)
{
	int err = 0;
	struct qstr qs;
	char namebuf[256];
	struct list_head *lp;
	struct dentry *tmp;
	/* child is an IS_ROOT (anonymous) dentry, but it is hypothesised that
	 * it should be a child of parent.
	 * We see if we can find a name and, if we can - splice it in.
	 * We hold the i_sem on the parent the whole time to try to follow locking protocols.
	 */
	qs.name = namebuf;
	down(&parent->d_inode->i_sem);

	/* Now, things might have changed while we waited.
	 * Possibly a friendly filesystem found child and spliced it in in response
	 * to a lookup (though nobody does this yet).  In this case, just succeed.
	 */
	if (child->d_parent == parent) goto out;
	/* Possibly a new dentry has been made for this child->d_inode in parent by
	 * a lookup.  In this case return that dentry. caller must notice and act accordingly
	 */
	for (lp = child->d_inode->i_dentry.next; lp != &child->d_inode->i_dentry ; lp=lp->next) {
		tmp = list_entry(lp,struct dentry, d_alias);
		if (tmp->d_parent == parent) {
			child = dget(tmp);
			goto out;
		}
	}
	/* well, if we can find a name for child in parent, it should be safe to splice it in */
	err = get_ino_name(parent, &qs, child->d_inode->i_ino);
	if (err)
		goto out;
	tmp = d_lookup(parent, &qs);
	if (tmp) {
		/* Now that IS odd.  I wonder what it means... */
		err = -EEXIST;
		printk("nfsd-fh: found a name that I didn't expect: %s/%s\n", parent->d_name.name, qs.name);
		dput(tmp);
		goto out;
	}
	err = d_splice(child, parent, &qs);
	dprintk("nfsd_fh: found name %s for ino %ld\n", child->d_name.name, child->d_inode->i_ino);
 out:
	up(&parent->d_inode->i_sem);
	if (err)
		return ERR_PTR(err);
	else
		return child;
}

/*
 * This is the basic lookup mechanism for turning an NFS file handle
 * into a dentry.
 * We use nfsd_iget and if that doesn't return a suitably connected dentry,
 * we try to find the parent, and the parent of that and so-on until a
 * connection if made.
 */
static struct dentry *
find_fh_dentry(struct super_block *sb, struct knfs_fh *fh, int needpath)
{
	struct dentry *dentry, *result = NULL;
	struct dentry *tmp;
	int  found =0;
	int err;
	/* This semaphore is needed to make sure that only one unconnected (free)
	 * dcache path ever exists, as otherwise two partial paths might get
	 * joined together, which would be very confusing.
	 * If there is ever an unconnected non-root directory, then this lock
	 * must be held.  This could sensibly be per-filesystem.
	 */
	static DECLARE_MUTEX(free_path_sem);

	nfsdstats.fh_lookup++;
	/*
	 * Attempt to find the inode.
	 */
 retry:
	result = nfsd_iget(sb, fh->fh_ino, fh->fh_generation);
	err = PTR_ERR(result);
	if (IS_ERR(result))
		goto err_out;
	err = -ESTALE;
	if (!result) {
		dprintk("find_fh_dentry: No inode found.\n");
		goto err_out;
	}
	if (!IS_ROOT(result) || result->d_inode->i_sb->s_root ==result)
		return result;

	/* result is now an anonymous dentry, which may be adequate as it stands, or else
	 * will get spliced into the dcache tree */

	if (!S_ISDIR(result->d_inode->i_mode) && ! needpath) {
		nfsdstats.fh_anon++;
		return result;
	}

	/* It's a directory, or we are required to confirm the file's
	 * location in the tree.
	 */
	dprintk("nfs_fh: need to look harder for %d/%d\n",sb->s_dev,fh->fh_ino);
	down(&free_path_sem);
	found = 0;
	if (!S_ISDIR(result->d_inode->i_mode)) {
		nfsdstats.fh_nocache_nondir++;
		if (fh->fh_dirino == 0)
			goto err_result; /* don't know how to find parent */
		else {
			/* need to iget fh->fh_dirino and make sure this inode is in that directory */
			dentry = nfsd_iget(sb, fh->fh_dirino, 0);
			err = PTR_ERR(dentry);
			if (IS_ERR(dentry))
				goto err_result;
			err = -ESTALE;
			if (!dentry->d_inode
			    || !S_ISDIR(dentry->d_inode->i_mode)) {
				goto err_dentry;
			}
			if (!IS_ROOT(dentry) || dentry->d_inode->i_sb->s_root ==dentry)
				found = 1;
			tmp = splice(result, dentry);
			err = PTR_ERR(tmp);
			if (IS_ERR(tmp))
				goto err_dentry;
			if (tmp != result) {
				/* it is safe to just use tmp instead, but we must discard result first */
				d_drop(result);
				dput(result);
				result = tmp;
				/* If !found, then this is really wierd, but it shouldn't hurt */
			}
		}
	} else {
		nfsdstats.fh_nocache_dir++;
		dentry = dget(result);
	}

	while(!found) {
		/* LOOP INVARIANT */
		/* haven't found a place in the tree yet, but we do have a free path
		 * from dentry down to result, and dentry is a directory.
		 * Have a hold on dentry and result */
		struct dentry *pdentry;
		struct inode *parent;

		pdentry = nfsd_findparent(dentry);
		err = PTR_ERR(pdentry);
		if (IS_ERR(pdentry))
			goto err_dentry;
		parent = pdentry->d_inode;
		err = -EACCES;
		if (!parent) {
			dput(pdentry);
			goto err_dentry;
		}
		/* I'm not sure that this is the best test for
		 *  "is it not a floating dentry?"
		 */
		if (!IS_ROOT(pdentry) || parent->i_sb->s_root == pdentry)
			found = 1;

		tmp = splice(dentry, pdentry);
		if (tmp != dentry) {
			/* Something wrong.  We need to drop thw whole dentry->result path
			 * whatever it was
			 */
			struct dentry *d;
			for (d=result ; d ; d=(d->d_parent == d)?NULL:d->d_parent)
				d_drop(d);
		}
		if (IS_ERR(tmp)) {
			err = PTR_ERR(tmp);
			dput(pdentry);
			goto err_dentry;
		}
		if (tmp != dentry) {
			/* we lost a race,  try again
			 */
			dput(tmp);
			dput(dentry);
			dput(result);	/* this will discard the whole free path, so we can up the semaphore */
			up(&free_path_sem);
			goto retry;
		}
		dput(dentry);
		dentry = pdentry;
	}
	dput(dentry);
	up(&free_path_sem);
	return result;

err_dentry:
	dput(dentry);
err_result:
	dput(result);
	up(&free_path_sem);
err_out:
	if (err == -ESTALE)
		nfsdstats.fh_stale++;
	return ERR_PTR(err);
}

/*
 * Perform sanity checks on the dentry in a client's file handle.
 *
 * Note that the file handle dentry may need to be freed even after
 * an error return.
 *
 * This is only called at the start of an nfsproc call, so fhp points to
 * a svc_fh which is all 0 except for the over-the-wire file handle.
 */
u32
fh_verify(struct svc_rqst *rqstp, struct svc_fh *fhp, int type, int access)
{
	struct knfs_fh	*fh = &fhp->fh_handle;
	struct svc_export *exp;
	struct dentry	*dentry;
	struct inode	*inode;
	u32		error = 0;

	dprintk("nfsd: fh_verify(exp %s/%u file (%s/%u dir %u)\n",
		kdevname(fh->fh_xdev),
		fh->fh_xino,
		kdevname(fh->fh_dev),
		fh->fh_ino,
		fh->fh_dirino);

	if (!fhp->fh_dverified) {
		/*
		 * Security: Check that the fh is internally consistant (from <gam3@acm.org>)
		 */
		if (fh->fh_dev != fh->fh_xdev) {
			printk("fh_verify: Security: export on other device (%s, %s).\n",
			       kdevname(fh->fh_dev), kdevname(fh->fh_xdev));
			error = nfserr_stale;
			nfsdstats.fh_stale++;
			goto out;
		}

		/*
		 * Look up the export entry.
		 */
		error = nfserr_stale; 
		exp = exp_get(rqstp->rq_client,
			      u32_to_kdev_t(fh->fh_xdev),
			      u32_to_ino_t(fh->fh_xino));
		if (!exp) {
			/* export entry revoked */
			nfsdstats.fh_stale++;
			goto out;
		}

		/* Check if the request originated from a secure port. */
		error = nfserr_perm;
		if (!rqstp->rq_secure && EX_SECURE(exp)) {
			printk(KERN_WARNING
			       "nfsd: request from insecure port (%08x:%d)!\n",
			       ntohl(rqstp->rq_addr.sin_addr.s_addr),
			       ntohs(rqstp->rq_addr.sin_port));
			goto out;
		}

		/* Set user creds if we haven't done so already. */
		nfsd_setuser(rqstp, exp);

		/*
		 * Look up the dentry using the NFS file handle.
		 */

		dentry = find_fh_dentry(exp->ex_dentry->d_inode->i_sb,
					fh,
					!(exp->ex_flags & NFSEXP_NOSUBTREECHECK));

		if (IS_ERR(dentry)) {
			error = nfserrno(-PTR_ERR(dentry));
			goto out;
		}

		fhp->fh_dentry = dentry;
		fhp->fh_export = exp;
		fhp->fh_dverified = 1;
		nfsd_nr_verified++;
	} else {
		/* just rechecking permissions
		 * (e.g. nfsproc_create calls fh_verify, then nfsd_create does as well)
		 */
		dprintk("nfsd: fh_verify - just checking\n");
		dentry = fhp->fh_dentry;
		exp = fhp->fh_export;
	}

	inode = dentry->d_inode;

	/* Type check. The correct error return for type mismatches
	 * does not seem to be generally agreed upon. SunOS seems to
	 * use EISDIR if file isn't S_IFREG; a comment in the NFSv3
	 * spec says this is incorrect (implementation notes for the
	 * write call).
	 */

	/* When is type ever negative? */
	if (type > 0 && (inode->i_mode & S_IFMT) != type) {
		error = (type == S_IFDIR)? nfserr_notdir : nfserr_isdir;
		goto out;
	}
	if (type < 0 && (inode->i_mode & S_IFMT) == -type) {
		error = (type == -S_IFDIR)? nfserr_notdir : nfserr_isdir;
		goto out;
	}

	/*
	 * Security: Check that the export is valid for dentry <gam3@acm.org>
	 */
	error = 0;

	if (!(exp->ex_flags & NFSEXP_NOSUBTREECHECK)) {
		if (exp->ex_dentry != dentry) {
			struct dentry *tdentry = dentry;

			do {
				tdentry = tdentry->d_parent;
				if (exp->ex_dentry == tdentry)
					break;
				/* executable only by root and we can't be root */
				if (current->fsuid
				    && (exp->ex_flags & NFSEXP_ROOTSQUASH)
				    && !(tdentry->d_inode->i_uid
					 && (tdentry->d_inode->i_mode & S_IXUSR))
				    && !(tdentry->d_inode->i_gid
					 && (tdentry->d_inode->i_mode & S_IXGRP))
				    && !(tdentry->d_inode->i_mode & S_IXOTH)
					) {
					error = nfserr_stale;
					nfsdstats.fh_stale++;
					dprintk("fh_verify: no root_squashed access.\n");
				}
			} while ((tdentry != tdentry->d_parent));
			if (exp->ex_dentry != tdentry) {
				error = nfserr_stale;
				nfsdstats.fh_stale++;
				printk("nfsd Security: %s/%s bad export.\n",
				       dentry->d_parent->d_name.name,
				       dentry->d_name.name);
				goto out;
			}
		}
	}

	/* Finally, check access permissions. */
	if (!error) {
		error = nfsd_permission(exp, dentry, access);
	}
#ifdef NFSD_PARANOIA
	if (error) {
		printk("fh_verify: %s/%s permission failure, acc=%x, error=%d\n",
		       dentry->d_parent->d_name.name, dentry->d_name.name, access, (error >> 24));
	}
#endif
out:
	return error;
}

/*
 * Compose a file handle for an NFS reply.
 *
 * Note that when first composed, the dentry may not yet have
 * an inode.  In this case a call to fh_update should be made
 * before the fh goes out on the wire ...
 */
void
fh_compose(struct svc_fh *fhp, struct svc_export *exp, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	struct dentry *parent = dentry->d_parent;

	dprintk("nfsd: fh_compose(exp %x/%ld %s/%s, ino=%ld)\n",
		exp->ex_dev, (long) exp->ex_ino,
		parent->d_name.name, dentry->d_name.name,
		(inode ? inode->i_ino : 0));

	/*
	 * N.B. We shouldn't need to init the fh -- the call to fh_compose
	 * may not be done on error paths, but the cleanup must call fh_put.
	 * Fix this soon!
	 */
	if (fhp->fh_dverified || fhp->fh_locked || fhp->fh_dentry) {
		printk(KERN_ERR "fh_compose: fh %s/%s not initialized!\n",
			parent->d_name.name, dentry->d_name.name);
	}
	fh_init(fhp);

	fhp->fh_handle.fh_dirino = ino_t_to_u32(parent->d_inode->i_ino);
	fhp->fh_handle.fh_dev    = kdev_t_to_u32(parent->d_inode->i_dev);
	fhp->fh_handle.fh_xdev   = kdev_t_to_u32(exp->ex_dev);
	fhp->fh_handle.fh_xino   = ino_t_to_u32(exp->ex_ino);
	fhp->fh_handle.fh_dcookie = (struct dentry *)0xfeebbaca;
	if (inode) {
		fhp->fh_handle.fh_ino = ino_t_to_u32(inode->i_ino);
		fhp->fh_handle.fh_generation = inode->i_generation;
		if (S_ISDIR(inode->i_mode) || (exp->ex_flags & NFSEXP_NOSUBTREECHECK))
			fhp->fh_handle.fh_dirino = 0;
	}

	fhp->fh_dentry = dentry; /* our internal copy */
	fhp->fh_export = exp;

	/* We stuck it there, we know it's good. */
	fhp->fh_dverified = 1;
	nfsd_nr_verified++;
}

/*
 * Update file handle information after changing a dentry.
 * This is only called by nfsd_create
 */
void
fh_update(struct svc_fh *fhp)
{
	struct dentry *dentry;
	struct inode *inode;

	if (!fhp->fh_dverified)
		goto out_bad;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;
	if (!inode)
		goto out_negative;
	fhp->fh_handle.fh_ino = ino_t_to_u32(inode->i_ino);
	fhp->fh_handle.fh_generation = inode->i_generation;
	if (S_ISDIR(inode->i_mode) || (fhp->fh_export->ex_flags & NFSEXP_NOSUBTREECHECK))
		fhp->fh_handle.fh_dirino = 0;

out:
	return;

out_bad:
	printk(KERN_ERR "fh_update: fh not verified!\n");
	goto out;
out_negative:
	printk(KERN_ERR "fh_update: %s/%s still negative!\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	goto out;
}

/*
 * Release a file handle.
 */
void
fh_put(struct svc_fh *fhp)
{
	struct dentry * dentry = fhp->fh_dentry;
	if (fhp->fh_dverified) {
		fh_unlock(fhp);
		fhp->fh_dverified = 0;
		if (!dentry->d_count)
			goto out_bad;
		dput(dentry);
		nfsd_nr_put++;
	}
	return;

out_bad:
	printk(KERN_ERR "fh_put: %s/%s has d_count 0!\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	return;
}
