/*
 * linux/fs/lockd/svcsubs.c
 *
 * Various support routines for the NLM server.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/export.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/share.h>
#include <linux/lockd/sm_inter.h>

#define NLMDBG_FACILITY		NLMDBG_SVCSUBS


/*
 * Global file hash table
 */
#define FILE_NRHASH		32
#define FILE_HASH_BITS		5
static struct nlm_file *	nlm_files[FILE_NRHASH];
static struct semaphore		nlm_file_sema = MUTEX;

static unsigned int file_hash(dev_t dev, ino_t ino)
{
	unsigned long tmp = (unsigned long) ino | (unsigned long) dev;
	tmp = tmp + (tmp >> FILE_HASH_BITS) + (tmp >> FILE_HASH_BITS*2);
	return tmp & (FILE_NRHASH - 1);
}

/*
 * Lookup file info. If it doesn't exist, create a file info struct
 * and open a (VFS) file for the given inode.
 *
 * FIXME:
 * Note that we open the file O_RDONLY even when creating write locks.
 * This is not quite right, but for now, we assume the client performs
 * the proper R/W checking.
 */
u32
nlm_lookup_file(struct svc_rqst *rqstp, struct nlm_file **result,
					struct nfs_fh *f)
{
	struct knfs_fh	*fh = (struct knfs_fh *) f;
	struct nlm_file	*file;
	unsigned int	hash;
	u32		nfserr;
	uid_t		saved_cr_uid;
	struct svc_cred	*cred;

	dprintk("lockd: nlm_file_lookup(%s/%u)\n",
		kdevname(u32_to_kdev_t(fh->fh_dev)), fh->fh_ino);

	hash = file_hash(u32_to_kdev_t(fh->fh_dev), u32_to_ino_t(fh->fh_ino));

	/* Lock file table */
	down(&nlm_file_sema);

	for (file = nlm_files[hash]; file; file = file->f_next) {
		if (file->f_handle.fh_dcookie == fh->fh_dcookie &&
		    !memcmp(&file->f_handle, fh, sizeof(*fh)))
			goto found;
	}

	dprintk("lockd: creating file for %s/%u\n",
		kdevname(u32_to_kdev_t(fh->fh_dev)), fh->fh_ino);
	nfserr = nlm_lck_denied_nolocks;
	file = (struct nlm_file *) kmalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		goto out_unlock;

	memset(file, 0, sizeof(*file));
	file->f_handle = *fh;
	file->f_sema   = MUTEX;

	/* Open the file. Note that this must not sleep for too long, else
	 * we would lock up lockd:-) So no NFS re-exports, folks.
	 *
	 * We have to make sure we have the right credential to open
	 * the file.
	 */
	cred = &rqstp->rq_cred;
	saved_cr_uid = cred->cr_uid;
	cred->cr_uid = 0;
	if ((nfserr = nlmsvc_ops->fopen(rqstp, fh, &file->f_file)) != 0) {
		dprintk("lockd: open failed (nfserr %ld)\n", ntohl(nfserr));
		cred->cr_uid = saved_cr_uid;
		goto out_free;
	}
	cred->cr_uid = saved_cr_uid;

	file->f_next = nlm_files[hash];
	nlm_files[hash] = file;

found:
	dprintk("lockd: found file %p (count %d)\n", file, file->f_count);
	*result = file;
	file->f_count++;
	nfserr = 0;

out_unlock:
	up(&nlm_file_sema);
	return nfserr;

out_free:
	kfree(file);
	nfserr = nlm_lck_denied;
	goto out_unlock;
}

/*
 * Delete a file after having released all locks, blocks and shares
 */
static inline void
nlm_delete_file(struct nlm_file *file)
{
	struct inode *inode = file->f_file.f_dentry->d_inode;
	struct nlm_file	**fp, *f;

	dprintk("lockd: closing file %s/%ld\n",
		kdevname(inode->i_dev), inode->i_ino);
	fp = nlm_files + file_hash(inode->i_dev, inode->i_ino);
	while ((f = *fp) != NULL) {
		if (f == file) {
			*fp = file->f_next;
			nlmsvc_ops->fclose(&file->f_file);
			kfree(file);
			return;
		}
		fp = &file->f_next;
	}

	printk(KERN_WARNING "lockd: attempt to release unknown file!\n");
}

/*
 * Loop over all locks on the given file and perform the specified
 * action.
 */
static int
nlm_traverse_locks(struct nlm_host *host, struct nlm_file *file, int action)
{
	struct inode	 *inode = nlmsvc_file_inode(file);
	struct file_lock *fl;
	struct nlm_host	 *lockhost;

again:
	file->f_locks = 0;
	for (fl = inode->i_flock; fl; fl = fl->fl_next) {
		if (!(fl->fl_flags & FL_LOCKD))
			continue;

		/* update current lock count */
		file->f_locks++;
		lockhost = (struct nlm_host *) fl->fl_owner;
		if (action == NLM_ACT_MARK)
			lockhost->h_inuse = 1;
		else if (action == NLM_ACT_CHECK)
			return 1;
		else if (action == NLM_ACT_UNLOCK) {
			struct file_lock lock = *fl;

			if (host && lockhost != host)
				continue;

			lock.fl_type  = F_UNLCK;
			lock.fl_start = 0;
			lock.fl_end   = NLM_OFFSET_MAX;
			if (posix_lock_file(&file->f_file, &lock, 0) < 0) {
				printk("lockd: unlock failure in %s:%d\n",
						__FILE__, __LINE__);
				return 1;
			}
			goto again;
		}
	}

	return 0;
}

/*
 * Operate on a single file
 */
static inline int
nlm_inspect_file(struct nlm_host *host, struct nlm_file *file, int action)
{
	if (action == NLM_ACT_CHECK) {
		/* Fast path for mark and sweep garbage collection */
		if (file->f_count || file->f_blocks || file->f_shares)
			return 1;
	} else {
		if (nlmsvc_traverse_blocks(host, file, action)
		 || nlmsvc_traverse_shares(host, file, action))
			return 1;
	}
	return nlm_traverse_locks(host, file, action);
}

/*
 * Loop over all files in the file table.
 */
static int
nlm_traverse_files(struct nlm_host *host, int action)
{
	struct nlm_file	*file, **fp;
	int		i;

	down(&nlm_file_sema);
	for (i = 0; i < FILE_NRHASH; i++) {
		fp = nlm_files + i;
		while ((file = *fp) != NULL) {
			/* Traverse locks, blocks and shares of this file
			 * and update file->f_locks count */
			if (nlm_inspect_file(host, file, action)) {
				up(&nlm_file_sema);
				return 1;
			}

			/* No more references to this file. Let go of it. */
			if (!file->f_blocks && !file->f_locks
			 && !file->f_shares && !file->f_count) {
				*fp = file->f_next;
				nlmsvc_ops->fclose(&file->f_file);
				kfree(file);
			} else {
				fp = &file->f_next;
			}
		}
	}
	up(&nlm_file_sema);
	return 0;
}

/*
 * Release file. If there are no more remote locks on this file,
 * close it and free the handle.
 *
 * Note that we can't do proper reference counting without major
 * contortions because the code in fs/locks.c creates, deletes and
 * splits locks without notification. Our only way is to walk the
 * entire lock list each time we remove a lock.
 */
void
nlm_release_file(struct nlm_file *file)
{
	dprintk("lockd: nlm_release_file(%p, ct = %d)\n",
				file, file->f_count);

	/* Lock file table */
	down(&nlm_file_sema);

	/* If there are no more locks etc, delete the file */
	if(--file->f_count == 0) {
		if(!nlm_inspect_file(NULL, file, NLM_ACT_CHECK))
			nlm_delete_file(file);
	}

	up(&nlm_file_sema);
}

/*
 * Mark all hosts that still hold resources
 */
void
nlmsvc_mark_resources(void)
{
	dprintk("lockd: nlmsvc_mark_resources\n");

	nlm_traverse_files(NULL, NLM_ACT_MARK);
}

/*
 * Release all resources held by the given client
 */
void
nlmsvc_free_host_resources(struct nlm_host *host)
{
	dprintk("lockd: nlmsvc_free_host_resources\n");

	if (nlm_traverse_files(host, NLM_ACT_UNLOCK))
		printk(KERN_WARNING
			"lockd: couldn't remove all locks held by %s",
			host->h_name);
}

/*
 * Delete a client when the nfsd entry is removed.
 */
void
nlmsvc_invalidate_client(struct svc_client *clnt)
{
	struct nlm_host	*host;

	if ((host = nlm_lookup_host(clnt, NULL, 0, 0)) != NULL) {
		dprintk("lockd: invalidating client for %s\n", host->h_name);
		nlmsvc_free_host_resources(host);
		host->h_expires = 0;
		nlm_release_host(host);
	}
}
