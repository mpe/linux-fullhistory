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

#define this_dir_cached(dir) ((dir->i_sb == c_sb) && (dir->i_ino == c_ino))

static long
smb_dir_read(struct inode *inode, struct file *filp,
	     char *buf, unsigned long count);

static int
smb_readdir(struct file *filp, void *dirent, filldir_t filldir);

static int smb_lookup(struct inode *, struct dentry *);
static int smb_create(struct inode *, struct dentry *, int);
static int smb_mkdir(struct inode *, struct dentry *, int);
static int smb_rmdir(struct inode *, struct dentry *);
static int smb_unlink(struct inode *, struct dentry *);
static int smb_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);

static struct file_operations smb_dir_operations =
{
	NULL,			/* lseek - default */
	smb_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	smb_readdir,		/* readdir */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
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

static void smb_put_dentry(struct dentry *);
static struct dentry_operations smbfs_dentry_operations =
{
	NULL,			/* revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	smb_put_dentry		/* d_delete */
};

static long
smb_dir_read(struct inode *inode, struct file *filp, char *buf,
	     unsigned long count)
{
	return -EISDIR;
}

/*
 * This is the callback from dput().  We close the file so that
 * cached dentries don't keep the file open.
 */
void
smb_put_dentry(struct dentry *dentry)
{
	struct inode *ino = dentry->d_inode;
	if (ino)
		smb_close(ino);
}

/* Static variables for the dir cache */
static struct smb_dirent *c_entry = NULL;
static struct super_block * c_sb = NULL;
static unsigned long c_ino = 0;
static int c_seen_eof;
static int c_size;
static int c_last_returned_index;

static struct smb_dirent *
smb_search_in_cache(struct inode *dir, unsigned long f_pos)
{
	int i;

	if (this_dir_cached(dir))
		for (i = 0; i < c_size; i++)
		{
			if (c_entry[i].f_pos < f_pos)
				continue;
			if (c_entry[i].f_pos == f_pos)
			{
				c_last_returned_index = i;
				return &(c_entry[i]);
			}
			break;
		}
	return NULL;
}

/*
 * Compute the hash for a qstr ... move to include/linux/dcache.h?
 */
static unsigned int hash_it(const char * name, unsigned int len)
{
	unsigned long hash;
	hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return end_name_hash(hash);
}

static struct semaphore refill_cache_sem = MUTEX;
/*
 * Called with the refill semaphore held.
 */
static int
smb_refill_dir_cache(struct dentry *dentry, unsigned long f_pos)
{
	struct inode *dir = dentry->d_inode;
	ino_t ino_start;
	int i, result;

	result = smb_proc_readdir(dentry, f_pos,
					SMB_READDIR_CACHE_SIZE, c_entry);
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_refill_dir_cache: dir=%s/%s, pos=%lu, result=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, f_pos, result);
#endif

	if (result <= 0)
	{
		/*
		 * If an error occurred, the cache may have been partially
		 * filled prior to failing, so we must invalidate.
		 * N.B. Might not need to for 0 return ... save cache?
		 */
		c_sb = NULL;
		c_ino = 0;
		c_seen_eof = 0;
		goto out;
	}

	/* Suppose there are a multiple of cache entries? */
	c_seen_eof = (result < SMB_READDIR_CACHE_SIZE);
	c_sb = dir->i_sb;
	c_ino = dir->i_ino;
	c_size = result;
	c_last_returned_index = 0; /* is this used? */

	ino_start = smb_invent_inos(c_size);
	/*
	 * If a dentry already exists, we have to give the cache entry
	 * the correct inode number.  This is needed for getcwd().
	 */
	for (i = 0; i < c_size; i++)
	{
		struct dentry * new_dentry;
		struct qstr qname;

		c_entry[i].attr.f_ino = ino_start++;
		qname.name = c_entry[i].name;
		qname.len  = c_entry[i].len;
		qname.hash = hash_it(qname.name, qname.len);
		new_dentry = d_lookup(dentry, &qname);
		if (new_dentry)
		{
			struct inode * inode = new_dentry->d_inode;
			if (inode)
				c_entry[i].attr.f_ino = inode->i_ino;
			dput(new_dentry);
		}
	}
out:
	return result;
}

static int 
smb_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	struct smb_dirent *entry;
	int result;

	pr_debug("smb_readdir: filp->f_pos = %d\n", (int) filp->f_pos);
	pr_debug("smb_readdir: dir->i_ino = %ld, c_ino = %ld\n",
		 dir->i_ino, c_ino);

	result = -EBADF;
	if ((dir == NULL) || !S_ISDIR(dir->i_mode))
		goto out;

	/*
	 * Check whether the directory cache exists yet
	 */
	if (c_entry == NULL)
	{
		int size = sizeof(struct smb_dirent) * SMB_READDIR_CACHE_SIZE;
		result = -ENOMEM;
		entry = (struct smb_dirent *) smb_vmalloc(size);
		/*
		 * Somebody else may have allocated the cache,
		 * so we check again to avoid a memory leak.
		 */
		if (!c_entry)
		{
			if (!entry)
				goto out;
			c_entry = entry;
		} else if (entry) {
			printk("smb_readdir: cache already alloced!\n");
			smb_vfree(entry);
		}
	}

	result = 0;
	switch ((unsigned int) filp->f_pos)
	{
	case 0:
		if (filldir(dirent, ".", 1, 0, dir->i_ino) < 0)
			goto out;
		filp->f_pos = 1;
	case 1:
		if (filldir(dirent, "..", 2, 1,
				dentry->d_parent->d_inode->i_ino) < 0)
			goto out;
		filp->f_pos = 2;
	}

	/*
	 * Since filldir() could block if dirent is paged out,
	 * we hold the refill semaphore while using the cache.
	 * N.B. It's possible that the directory could change
	 * between calls to readdir ... what to do??
	 */
	down(&refill_cache_sem);
	entry = smb_search_in_cache(dir, filp->f_pos);
	if (entry == NULL)
	{
		/* Past the end of _this_ directory? */
		if (this_dir_cached(dir) && c_seen_eof &&
		    filp->f_pos == c_entry[c_size-1].f_pos + 1)
		{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_readdir: eof reached for %s/%s, c_size=%d, pos=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, c_size, (int) filp->f_pos);
#endif
			goto up_and_out;
		}
		result = smb_refill_dir_cache(dentry, filp->f_pos);
		if (result <= 0)
			goto up_and_out;
		entry = c_entry;
	}

	while (entry < &(c_entry[c_size]))
	{
		pr_debug("smb_readdir: entry->name = %s\n", entry->name);

		if (filldir(dirent, entry->name, entry->len, 
				    entry->f_pos, entry->attr.f_ino) < 0)
			break;
#if SMBFS_PARANOIA
/* should never happen */
if (!this_dir_cached(dir) || (entry->f_pos != filp->f_pos))
printk("smb_readdir: cache changed!\n");
#endif
		filp->f_pos += 1;
		entry += 1;
	}
	result = 0;

up_and_out:
	up(&refill_cache_sem);
out:
	return result;
}

void
smb_init_dir_cache(void)
{
	c_entry = NULL;
	c_sb = NULL;
	c_ino = 0;
	c_seen_eof = 0;
}

void
smb_invalid_dir_cache(struct inode * dir)
{
	if (this_dir_cached(dir))
	{
		c_sb = NULL;
		c_ino = 0;
		c_seen_eof = 0;
	}
}

void
smb_free_dir_cache(void)
{
	if (c_entry != NULL)
	{
		/* N.B. can this block?? */
		smb_vfree(c_entry);
	}
	c_entry = NULL;
}

static int
smb_lookup(struct inode *dir, struct dentry *d_entry)
{
	struct smb_fattr finfo;
	struct inode *inode;
	int error;

	error = -ENAMETOOLONG;
	if (d_entry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	error = smb_proc_getattr(d_entry->d_parent, &(d_entry->d_name), &finfo);
#if SMBFS_PARANOIA
if (error && error != -ENOENT)
printk("smb_lookup: find %s/%s failed, error=%d\n",
d_entry->d_parent->d_name.name, d_entry->d_name.name, error);
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
			inode->u.smbfs_i.dentry = d_entry;
	add_entry:
			d_entry->d_op = &smbfs_dentry_operations;
			d_add(d_entry, inode);
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
smb_instantiate(struct inode *dir, struct dentry *dentry)
{
	struct smb_fattr fattr;
	int error;

	smb_invalid_dir_cache(dir);

	error = smb_proc_getattr(dentry->d_parent, &(dentry->d_name), &fattr);
	if (!error)
	{
		struct inode *inode;
		error = -EACCES;
		fattr.f_ino = smb_invent_inos(1);
		inode = smb_iget(dir->i_sb, &fattr);
		if (inode)
		{
			/* cache the dentry pointer */
			inode->u.smbfs_i.dentry = dentry;
			d_instantiate(dentry, inode);
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

	error = smb_proc_create(dentry->d_parent, &(dentry->d_name), 
				0, CURRENT_TIME);
	if (!error)
	{
		error = smb_instantiate(dir, dentry);
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

	error = smb_proc_mkdir(dentry->d_parent, &(dentry->d_name));
	if (!error)
	{
		error = smb_instantiate(dir, dentry);
	}
out:
	return error;
}

static int
smb_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
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

	error = -ENOTDIR;
	if (!old_dir || !S_ISDIR(old_dir->i_mode))
	{
		printk("smb_rename: old inode is NULL or not a directory\n");
		goto out;
	}

	if (!new_dir || !S_ISDIR(new_dir->i_mode))
	{
		printk("smb_rename: new inode is NULL or not a directory\n");
		goto out;
	}

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

	/* Assume success and invalidate now */
	smb_invalid_dir_cache(old_dir);
	smb_invalid_dir_cache(new_dir);

	error = smb_proc_mv(old_dentry->d_parent, &(old_dentry->d_name),
			    new_dentry->d_parent, &(new_dentry->d_name));
	/*
	 * If the new file exists, attempt to delete it.
	 */
	if (error == -EEXIST)
	{
#ifdef SMBFS_PARANOIA
printk("smb_rename: existing file %s/%s, d_count=%d\n",
new_dentry->d_parent->d_name.name, new_dentry->d_name.name, 
new_dentry->d_count);
#endif
		error = smb_proc_unlink(new_dentry->d_parent, 
					&(new_dentry->d_name));
#ifdef SMBFS_PARANOIA
printk("smb_rename: after unlink error=%d\n", error);
#endif
		if (error)
			goto out;
		d_delete(new_dentry);

		error = smb_proc_mv(old_dentry->d_parent, &(old_dentry->d_name),
				    new_dentry->d_parent, &(new_dentry->d_name));
	}

	/*
	 * Update the dcache
	 */
	if (!error)
	{
		d_move(old_dentry, new_dentry);
	}
out:
	return error;
}
