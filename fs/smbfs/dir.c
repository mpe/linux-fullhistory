/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include <linux/smb_fs.h>
#include <linux/smbno.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */
/* #define pr_debug printk */
#define SMBFS_MAX_AGE 5*HZ

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
	NULL,			/* flush */
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
	NULL,			/* smap */
	NULL,			/* updatepage */
	smb_revalidate_inode,	/* revalidate */
};

static ssize_t
smb_dir_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EISDIR;
}

static int 
smb_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	struct cache_head *cachep;
	int result;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_readdir: reading %s/%s, f_pos=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, (int) filp->f_pos);
#endif
	/*
	 * Make sure our inode is up-to-date.
	 */
	result = smb_revalidate_inode(dentry);
	if (result)
		goto out;
	/*
	 * Get the cache pointer ...
	 */
	result = -EIO;
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
			goto out_free;
	}

	result = 0;
	switch ((unsigned int) filp->f_pos)
	{
	case 0:
		if (filldir(dirent, ".", 1, 0, dir->i_ino) < 0)
			goto out_free;
		filp->f_pos = 1;
	case 1:
		if (filldir(dirent, "..", 2, 1,
				dentry->d_parent->d_inode->i_ino) < 0)
			goto out_free;
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
		if (!entry->ino) {
			struct qstr qname;
			/* N.B. Make cache_dirent name a qstr! */
			qname.name = entry->name;
			qname.len  = entry->len;
			entry->ino = find_inode_number(dentry, &qname);
			if (!entry->ino)
				entry->ino = smb_invent_inos(1);
		}

		if (filldir(dirent, entry->name, entry->len, 
				    filp->f_pos, entry->ino) < 0)
			break;
		filp->f_pos += 1;
	}

	/*
	 * Release the dircache.
	 */
out_free:
	smb_free_dircache(cachep);
out:
	return result;
}

/*
 * Note: in order to allow the smbclient process to open the
 * mount point, we don't revalidate if conn_pid is NULL.
 */
static int
smb_dir_open(struct inode *dir, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct smb_sb_info *server = server_from_dentry(dentry);
	int error = 0;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_dir_open: (%s/%s)\n", dentry->d_parent->d_name.name, 
file->f_dentry->d_name.name);
#endif
	/*
	 * Directory timestamps in the core protocol aren't updated
	 * when a file is added, so we give them a very short TTL.
	 */
	if (server->opt.protocol < SMB_PROTOCOL_LANMAN2)
	{
		unsigned long age = jiffies - dir->u.smbfs_i.oldmtime;
		if (age > 2*HZ)
			smb_invalid_dir_cache(dir);
	}

	if (server->conn_pid)
		error = smb_revalidate_inode(dentry);
	return error;
}

/*
 * Dentry operations routines
 */
static int smb_lookup_validate(struct dentry *);
static int smb_hash_dentry(struct dentry *, struct qstr *);
static int smb_compare_dentry(struct dentry *, struct qstr *, struct qstr *);
static void smb_delete_dentry(struct dentry *);

static struct dentry_operations smbfs_dentry_operations =
{
	smb_lookup_validate,	/* d_validate(struct dentry *) */
	smb_hash_dentry,	/* d_hash */
	smb_compare_dentry,	/* d_compare */
	smb_delete_dentry	/* d_delete(struct dentry *) */
};

/*
 * This is the callback when the dcache has a lookup hit.
 */
static int
smb_lookup_validate(struct dentry * dentry)
{
	struct inode * inode = dentry->d_inode;
	unsigned long age = jiffies - dentry->d_time;
	int valid;

	/*
	 * The default validation is based on dentry age:
	 * we believe in dentries for 5 seconds.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	valid = (age <= SMBFS_MAX_AGE);
#ifdef SMBFS_DEBUG_VERBOSE
if (!valid)
printk("smb_lookup_validate: %s/%s not valid, age=%lu\n", 
dentry->d_parent->d_name.name, dentry->d_name.name, age);
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
		} else if (!valid)
			valid = (smb_revalidate_inode(dentry) == 0);
	} else
	{
	/*
	 * What should we do for negative dentries?
	 */
	}
	return valid;
}

/*
 * XXX: It would be better to use the tolower from linux/ctype.h,
 * but _ctype is needed and it is not exported.
 */
#define tolower(c) (((c) >= 'A' && (c) <= 'Z') ? (c)-('A'-'a') : (c))

static int 
smb_hash_dentry(struct dentry *dir, struct qstr *this)
{
	unsigned long hash;
	int i;

	hash = init_name_hash();
	for (i=0; i < this->len ; i++)
		hash = partial_name_hash(tolower(this->name[i]), hash);
	this->hash = end_name_hash(hash);
  
	return 0;
}

static int
smb_compare_dentry(struct dentry *dir, struct qstr *a, struct qstr *b)
{
	int i, result = 1;

	if (a->len != b->len)
		goto out;
	for (i=0; i < a->len; i++)
	{
		if (tolower(a->name[i]) != tolower(b->name[i]))
			goto out;
	}
	result = 0;
out:
	return result;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes and close files.
 */
static void
smb_delete_dentry(struct dentry * dentry)
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
void
smb_renew_times(struct dentry * dentry)
{
	for (;;)
	{
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

	error = smb_proc_getattr(dentry, &finfo);
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
		error = -EACCES;
		finfo.f_ino = smb_invent_inos(1);
		inode = smb_iget(dir->i_sb, &finfo);
		if (inode)
		{
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
smb_instantiate(struct dentry *dentry, __u16 fileid, int have_id)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	struct inode *inode;
	int error;
	struct smb_fattr fattr;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_instantiate: file %s/%s, fileid=%u\n",
dentry->d_parent->d_name.name, dentry->d_name.name, fileid);
#endif
	error = smb_proc_getattr(dentry, &fattr);
	if (error)
		goto out_close;

	smb_renew_times(dentry);
	fattr.f_ino = smb_invent_inos(1);
	inode = smb_iget(dentry->d_sb, &fattr);
	if (!inode)
		goto out_no_inode;

	if (have_id)
	{
		inode->u.smbfs_i.fileid = fileid;
		inode->u.smbfs_i.access = SMB_O_RDWR;
		inode->u.smbfs_i.open = server->generation;
	}
	d_instantiate(dentry, inode);
out:
	return error;

out_no_inode:
	error = -EACCES;
out_close:
	if (have_id)
	{
#ifdef SMBFS_PARANOIA
printk("smb_instantiate: %s/%s failed, error=%d, closing %u\n",
dentry->d_parent->d_name.name, dentry->d_name.name, error, fileid);
#endif
		smb_close_fileid(dentry, fileid);
	}
	goto out;
}

/* N.B. How should the mode argument be used? */
static int
smb_create(struct inode *dir, struct dentry *dentry, int mode)
{
	__u16 fileid;
	int error;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_create: creating %s/%s, mode=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, mode);
#endif
	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	smb_invalid_dir_cache(dir);
	error = smb_proc_create(dentry, 0, CURRENT_TIME, &fileid);
	if (!error)
	{
		error = smb_instantiate(dentry, fileid, 1);
	} else
	{
#ifdef SMBFS_PARANOIA
printk("smb_create: %s/%s failed, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, error);
#endif
	}
out:
	return error;
}

/* N.B. How should the mode argument be used? */
static int
smb_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	smb_invalid_dir_cache(dir);
	error = smb_proc_mkdir(dentry);
	if (!error)
	{
		error = smb_instantiate(dentry, 0, 0);
	}
out:
	return error;
}

static int
smb_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int error;

	/*
	 * Close the directory if it's open.
	 */
	smb_close(inode);

	/*
	 * Check that nobody else is using the directory..
	 */
	error = -EBUSY;
	if (!list_empty(&dentry->d_hash))
		goto out;

	smb_invalid_dir_cache(dir);
	error = smb_proc_rmdir(dentry);

out:
	return error;
}

static int
smb_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	/*
	 * Close the file if it's open.
	 */
	smb_close(dentry->d_inode);

	smb_invalid_dir_cache(dir);
	error = smb_proc_unlink(dentry);
	if (!error)
	{
		smb_renew_times(dentry);
		d_delete(dentry);
	}
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
	 * Close any open files, and check whether to delete the
	 * target before attempting the rename.
	 */
	if (old_dentry->d_inode)
		smb_close(old_dentry->d_inode);
	if (new_dentry->d_inode)
	{
		smb_close(new_dentry->d_inode);
		error = smb_proc_unlink(new_dentry);
		if (error)
		{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_rename: unlink %s/%s, error=%d\n",
new_dentry->d_parent->d_name.name, new_dentry->d_name.name, error);
#endif
			goto out;
		}
		d_delete(new_dentry);
	}

	smb_invalid_dir_cache(old_dir);
	smb_invalid_dir_cache(new_dir);
	error = smb_proc_mv(old_dentry, new_dentry);
	if (!error)
	{
		smb_renew_times(old_dentry);
		smb_renew_times(new_dentry);
		d_move(old_dentry, new_dentry);
	}
out:
	return error;
}
