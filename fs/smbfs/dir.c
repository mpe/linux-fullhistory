/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/errno.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */
/* #define pr_debug printk */

static ssize_t smb_dir_read(struct file *, char *, size_t, loff_t *);
static int smb_readdir(struct file *, void *, filldir_t);
static int smb_dir_open(struct inode *, struct file *);

static int smb_lookup(struct inode *, struct dentry *);
static int smb_create(struct inode *, struct dentry *, int);
static int smb_mkdir(struct inode *, struct dentry *, int);
static int smb_rmdir(struct inode *, struct dentry *);
static int smb_unlink(struct inode *, struct dentry *);
static int smb_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *);

static struct file_operations smb_dir_operations =
{
	NULL,			/* lseek - default */
	smb_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	smb_readdir,		/* readdir */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl */
	NULL,			/* mmap */
	smb_dir_open,		/* open(struct inode *, struct file *) */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

struct inode_operations smb_dir_inode_operations =
{
	&smb_dir_operations,	/* default directory file ops */
	smb_create,		/* create */
	smb_lookup,		/* lookup */
	NULL,			/* link */
	smb_unlink,		/* unlink */
	NULL,			/* symlink */
	smb_mkdir,		/* mkdir */
	smb_rmdir,		/* rmdir */
	NULL,			/* mknod */
	smb_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static ssize_t
smb_dir_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EISDIR;
}

/*
 * Compute the hash for a qstr.
 * N.B. Move to include/linux/dcache.h?
 */
static unsigned int 
hash_it(const char * name, unsigned int len)
{
	unsigned long hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return end_name_hash(hash);
}

/*
 * If a dentry already exists, we have to give the cache entry
 * the correct inode number.  This is needed for getcwd().
 */
static unsigned long
smb_find_ino(struct dentry *dentry, struct cache_dirent *entry)
{
	struct dentry * new_dentry;
	struct qstr qname;
	unsigned long ino = 0;

	qname.name = entry->name;
	qname.len  = entry->len;
	qname.hash = hash_it(qname.name, qname.len);
	new_dentry = d_lookup(dentry, &qname);
	if (new_dentry)
	{
		struct inode * inode = new_dentry->d_inode;
		if (inode)
			ino = inode->i_ino;
		dput(new_dentry);
	}
	if (!ino)
		ino = smb_invent_inos(1);
	return ino;
}

static int 
smb_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	struct cache_head *cachep;
	int result;

	pr_debug("smb_readdir: filp->f_pos = %d\n", (int) filp->f_pos);
	pr_debug("smb_readdir: dir->i_ino = %ld, c_ino = %ld\n",
		 dir->i_ino, c_ino);
	/*
	 * Make sure our inode is up-to-date.
	 */
	result = smb_revalidate_inode(dir);
	if (result)
		goto out;
	/*
	 * Get the cache pointer ...
	 */
	cachep = smb_get_dircache(dentry);
	if (!cachep)
		goto out;
	/*
	 * Make sure the cache is up-to-date.
	 */
	if (!cachep->valid)
	{
		result = smb_refill_dircache(cachep, dentry);
		if (result)
			goto up_and_out;
	}

	switch ((unsigned int) filp->f_pos)
	{
	case 0:
		if (filldir(dirent, ".", 1, 0, dir->i_ino) < 0)
			goto up_and_out;
		filp->f_pos = 1;
	case 1:
		if (filldir(dirent, "..", 2, 1,
				dentry->d_parent->d_inode->i_ino) < 0)
			goto up_and_out;
		filp->f_pos = 2;
	}

	while (1)
	{
		struct cache_dirent this_dirent, *entry = &this_dirent;

		if (!smb_find_in_cache(cachep, filp->f_pos, entry))
			break;
		/*
		 * Check whether to look up the inode number.
		 */
		if (!entry->ino)
		{
			entry->ino = smb_find_ino(dentry, entry);
		}

		if (filldir(dirent, entry->name, entry->len, 
				    filp->f_pos, entry->ino) < 0)
			break;
		filp->f_pos += 1;
	}
	result = 0;

	/*
	 * Release the dircache.
	 */
up_and_out:
	smb_free_dircache(cachep);
out:
	return result;
}

static int
smb_dir_open(struct inode *dir, struct file *file)
{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_dir_open: (%s/%s)\n", file->f_dentry->d_parent->d_name.name, 
file->f_dentry->d_name.name);
#endif
	return smb_revalidate_inode(dir);
}

/*
 * Dentry operations routines
 */
static int smb_lookup_validate(struct dentry *);
static void smb_delete_dentry(struct dentry *);

static struct dentry_operations smbfs_dentry_operations =
{
	smb_lookup_validate,	/* d_validate(struct dentry *) */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	smb_delete_dentry	/* d_delete(struct dentry *) */
};

/*
 * This is the callback when the dcache has a lookup hit.
 */
static int smb_lookup_validate(struct dentry * dentry)
{
	struct inode * inode = dentry->d_inode;
	unsigned long age = jiffies - dentry->d_time;
	int valid;

	/*
	 * The default validation is based on dentry age:
	 * we believe in dentries for 5 seconds.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	valid = age < 5 * HZ || IS_ROOT(dentry);
#ifdef SMBFS_DEBUG_VERBOSE
if (!valid)
printk("smb_lookup_validate: %s/%s not valid, age=%d\n", 
dentry->d_parent->d_name.name, dentry->d_name.name, age)
#endif

	if (inode)
	{
		if (is_bad_inode(inode))
		{
#ifdef SMBFS_PARANOIA
printk("smb_lookup_validate: %s/%s has dud inode\n", 
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			valid = 0;
		}
	} else
	{
	/*
	 * What should we do for negative dentries?
	 */
	}
	return valid;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to close files and unhash dentries with bad inodes.
 */
static void smb_delete_dentry(struct dentry * dentry)
{
	if (dentry->d_inode)
	{
		if (is_bad_inode(dentry->d_inode))
		{
#ifdef SMBFS_PARANOIA
printk("smb_delete_dentry: bad inode, unhashing %s/%s\n", 
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			d_drop(dentry);
		}
		smb_close_dentry(dentry);
	} else
	{
	/* N.B. Unhash negative dentries? */
	}
}

/*
 * Whenever a lookup succeeds, we know the parent directories
 * are all valid, so we want to update the dentry timestamps.
 * N.B. Move this to dcache?
 */
void smb_renew_times(struct dentry * dentry)
{
	for (;;) {
		dentry->d_time = jiffies;
		if (dentry == dentry->d_parent)
			break;
		dentry = dentry->d_parent;
	}
}

static int
smb_lookup(struct inode *dir, struct dentry *dentry)
{
	struct smb_fattr finfo;
	struct inode *inode;
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	error = smb_proc_getattr(dentry->d_parent, &(dentry->d_name), &finfo);
#ifdef SMBFS_PARANOIA
if (error && error != -ENOENT)
printk("smb_lookup: find %s/%s failed, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, error);
#endif

	inode = NULL;
	if (error == -ENOENT)
		goto add_entry;
	if (!error)
	{
		finfo.f_ino = smb_invent_inos(1);
		inode = smb_iget(dir->i_sb, &finfo);
		error = -EACCES;
		if (inode)
		{
			/* cache the dentry pointer */
			inode->u.smbfs_i.dentry = dentry;
	add_entry:
			dentry->d_op = &smbfs_dentry_operations;
			d_add(dentry, inode);
			smb_renew_times(dentry);
			error = 0;
		}
	}
out:
	return error;
}

/*
 * This code is common to all routines creating a new inode.
 */
static int
smb_instantiate(struct dentry *dentry)
{
	struct smb_fattr fattr;
	int error;

	error = smb_proc_getattr(dentry->d_parent, &(dentry->d_name), &fattr);
	if (!error)
	{
		struct inode *inode;
		error = -EACCES;
		fattr.f_ino = smb_invent_inos(1);
		inode = smb_iget(dentry->d_sb, &fattr);
		if (inode)
		{
			/* cache the dentry pointer */
			inode->u.smbfs_i.dentry = dentry;
			d_instantiate(dentry, inode);
			smb_renew_times(dentry);
			error = 0;
		}
	}
	return error;
}

/* N.B. Should the mode argument be put into the fattr? */
static int
smb_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	/* FIXME: In the CIFS create call we get the file in open
         * state. Currently we close it directly again, although this
	 * is not necessary anymore. */

	smb_invalid_dir_cache(dir);
	error = smb_proc_create(dentry->d_parent, &(dentry->d_name), 
				0, CURRENT_TIME);
	if (!error)
	{
		error = smb_instantiate(dentry);
	}
out:
	return error;
}

/* N.B. Should the mode argument be put into the fattr? */
static int
smb_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	smb_invalid_dir_cache(dir);
	error = smb_proc_mkdir(dentry->d_parent, &(dentry->d_name));
	if (!error)
	{
		error = smb_instantiate(dentry);
	}
out:
	return error;
}

static int
smb_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	/*
	 * Since the dentry is holding an inode, the file
	 * is in use, so we have to close it first.
	 */
	if (dentry->d_inode)
		smb_close(dentry->d_inode);

	smb_invalid_dir_cache(dir);
	error = smb_proc_rmdir(dentry->d_parent, &(dentry->d_name));
	if (!error)
	{
		smb_renew_times(dentry);
		d_delete(dentry);
	}
out:
	return error;
}

static int
smb_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	/*
	 * Since the dentry is holding an inode, the file
	 * is in use, so we have to close it first.
	 */
	if (dentry->d_inode)
		smb_close(dentry->d_inode);

	smb_invalid_dir_cache(dir);
	error = smb_proc_unlink(dentry->d_parent, &(dentry->d_name));
	if (!error)
	{
		smb_renew_times(dentry);
		d_delete(dentry);
	}
out:
	return error;
}

static int
smb_rename(struct inode *old_dir, struct dentry *old_dentry,
	   struct inode *new_dir, struct dentry *new_dentry)
{
	int error;

	error = -ENAMETOOLONG;
	if (old_dentry->d_name.len > SMB_MAXNAMELEN ||
	    new_dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	/*
	 * Since the old and new dentries are holding the files open,
	 * we have to close the files first.
	 */
	if (old_dentry->d_inode)
		smb_close(old_dentry->d_inode);
	if (new_dentry->d_inode)
		smb_close(new_dentry->d_inode);

	smb_invalid_dir_cache(old_dir);
	smb_invalid_dir_cache(new_dir);
	error = smb_proc_mv(old_dentry->d_parent, &(old_dentry->d_name),
			    new_dentry->d_parent, &(new_dentry->d_name));
	/*
	 * If the new file exists, attempt to delete it.
	 */
	if (error == -EEXIST)
	{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_rename: existing file %s/%s, d_count=%d\n",
new_dentry->d_parent->d_name.name, new_dentry->d_name.name, 
new_dentry->d_count);
#endif
		error = smb_proc_unlink(new_dentry->d_parent, 
					&(new_dentry->d_name));
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_rename: after unlink error=%d\n", error);
#endif
		if (!error)
		{
			d_delete(new_dentry);
			error = smb_proc_mv(old_dentry->d_parent, 
						&(old_dentry->d_name),
				   	    new_dentry->d_parent, 
						&(new_dentry->d_name));
		}
	}

	/*
	 * Update the dcache
	 */
	if (!error)
	{
		smb_renew_times(old_dentry);
		smb_renew_times(new_dentry->d_parent);
		d_move(old_dentry, new_dentry);
	}
out:
	return error;
}
