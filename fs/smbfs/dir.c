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

static long
smb_dir_read(struct inode *inode, struct file *filp, char *buf,
	     unsigned long count)
{
	return -EISDIR;
}

static unsigned long c_ino = 0;
static kdev_t c_dev;
static int c_size;
static int c_seen_eof;
static int c_last_returned_index;
static struct smb_dirent *c_entry = NULL;

static struct smb_dirent *
smb_search_in_cache(struct inode *dir, unsigned long f_pos)
{
	int i;

	if ((dir->i_dev != c_dev) || (dir->i_ino != c_ino))
	{
		return NULL;
	}
	for (i = 0; i < c_size; i++)
	{
		if (f_pos == c_entry[i].f_pos)
		{
			c_last_returned_index = i;
			return &(c_entry[i]);
		}
	}
	return NULL;
}

static int
smb_refill_dir_cache(struct dentry *dentry, unsigned long f_pos)
{
	int result;
	struct inode *dir = dentry->d_inode;
	static struct semaphore sem = MUTEX;
	int i;
	ino_t ino;

	do
	{
		down(&sem);
		result = smb_proc_readdir(dentry, f_pos,
					  SMB_READDIR_CACHE_SIZE, c_entry);

		if (result <= 0)
		{
			smb_invalid_dir_cache(dir->i_ino);
			up(&sem);
			return result;
		}
		c_seen_eof = (result < SMB_READDIR_CACHE_SIZE);
		c_dev = dir->i_dev;
		c_ino = dir->i_ino;
		c_size = result;
		c_last_returned_index = 0;

		ino = smb_invent_inos(c_size);

		for (i = 0; i < c_size; i++)
			c_entry[i].attr.f_ino = ino++;

		up(&sem);
	}
	while ((c_dev != dir->i_dev) || (c_ino != dir->i_ino));

	return result;
}

static int smb_readdir(struct file *filp,
	    void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	int result, i = 0;
	struct smb_dirent *entry = NULL;

	pr_debug("smb_readdir: filp->f_pos = %d\n", (int) filp->f_pos);
	pr_debug("smb_readdir: dir->i_ino = %ld, c_ino = %ld\n",
		 dir->i_ino, c_ino);

	if ((dir == NULL) || !S_ISDIR(dir->i_mode))
	{
		return -EBADF;
	}
	if (c_entry == NULL)
	{
		i = sizeof(struct smb_dirent) * SMB_READDIR_CACHE_SIZE;
		c_entry = (struct smb_dirent *) smb_vmalloc(i);
		if (c_entry == NULL)
		{
			return -ENOMEM;
		}
	}
	if (filp->f_pos == 0)
	{
		c_ino = 0;
		c_dev = 0;
		c_seen_eof = 0;

		if (filldir(dirent, ".", 1, filp->f_pos, dir->i_ino) < 0)
			return 0;

		filp->f_pos += 1;
	}
	if (filp->f_pos == 1)
	{
		if (filldir(dirent, "..", 2, filp->f_pos,
			    filp->f_dentry->d_parent->d_inode->i_ino) < 0)
			return 0;

		filp->f_pos += 1;
	}
	entry = smb_search_in_cache(dir, filp->f_pos);

	if (entry == NULL)
	{
		if (c_seen_eof)
		{
			/* End of directory */
			return 0;
		}
		result = smb_refill_dir_cache(dentry, filp->f_pos);
		if (result <= 0)
		{
			return result;
		}
		entry = c_entry;
	}
	while (entry < &(c_entry[c_size]))
	{
		ino_t ino = entry->attr.f_ino;

		pr_debug("smb_readdir: entry->name = %s\n", entry->name);

		if (filldir(dirent, entry->name, strlen(entry->name),
			    entry->f_pos, ino) < 0)
			break;

		if ((dir->i_dev != c_dev) || (dir->i_ino != c_ino)
		    || (entry->f_pos != filp->f_pos))
			break;

		filp->f_pos += 1;
		entry += 1;
	}
	return 0;
}

void
smb_init_dir_cache(void)
{
	c_ino = 0;
	c_dev = 0;
	c_entry = NULL;
}

void
smb_invalid_dir_cache(unsigned long ino)
{
	/* FIXME: check for dev as well */
	if (ino == c_ino)
	{
		c_ino = 0;
		c_seen_eof = 0;
	}
}

void
smb_free_dir_cache(void)
{
	if (c_entry != NULL)
	{
		smb_vfree(c_entry);
	}
	c_entry = NULL;
}

static int
smb_lookup(struct inode *dir, struct dentry *d_entry)
{
	struct smb_fattr finfo;
	struct inode *inode;
	int len = d_entry->d_name.len;
	int error;

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("smb_lookup: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (len > SMB_MAXNAMELEN)
		return -ENAMETOOLONG;

	error = smb_proc_getattr(d_entry, &(d_entry->d_name), &finfo);

	inode = NULL;
	if (!error) {
		error = -ENOENT;
		finfo.f_ino = smb_invent_inos(1);
		inode = smb_iget(dir->i_sb, &finfo);
		if (!inode)
			return -EACCES;
	} else if (error != -ENOENT)
		return error;

	d_add(d_entry, inode);
	return 0;
}

static int smb_create(struct inode *dir, struct dentry *dentry, int mode)
{
	struct smb_fattr fattr;
	struct inode *inode;
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_create: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > SMB_MAXNAMELEN)
		return -ENAMETOOLONG;

	error = smb_proc_create(dentry, &(dentry->d_name), 0, CURRENT_TIME);
	if (error < 0)
		return error;

	smb_invalid_dir_cache(dir->i_ino);

	/* FIXME: In the CIFS create call we get the file in open
         * state. Currently we close it directly again, although this
	 * is not necessary anymore. */

	error = smb_proc_getattr(dentry, &(dentry->d_name), &fattr);
	if (error < 0)
		return error;

	fattr.f_ino = smb_invent_inos(1);

	inode = smb_iget(dir->i_sb, &fattr);
	if (!inode)
		return -EACCES;

	d_instantiate(dentry, inode);
	return 0;
}

static int
smb_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct smb_fattr fattr;
	struct inode *inode;
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_mkdir: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > SMB_MAXNAMELEN)
		return -ENAMETOOLONG;

	error = smb_proc_mkdir(dentry, &(dentry->d_name));
	if (error)
		return error;

	smb_invalid_dir_cache(dir->i_ino);

	error = smb_proc_getattr(dentry, &(dentry->d_name), &fattr);
	if (error < 0)
		return error;

	fattr.f_ino = smb_invent_inos(1);

	inode = smb_iget(dir->i_sb, &fattr);
	if (!inode)
		return -EACCES;

	d_instantiate(dentry, inode);
	return 0;
}

static int
smb_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_rmdir: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > NFS_MAXNAMLEN)
		return -ENAMETOOLONG;

	error = smb_proc_rmdir(dentry, &(dentry->d_name));
	if (error)
		return error;

	d_delete(dentry);
	return 0;
}

static int
smb_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_unlink: inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (dentry->d_name.len > SMB_MAXNAMELEN)
		return -ENAMETOOLONG;

	error = smb_proc_unlink(dentry, &(dentry->d_name));
	if (error)
		return error;

	smb_invalid_dir_cache(dir->i_ino);

	d_delete(dentry);
	return 0;
}

static int smb_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	int error;

	if (!old_dir || !S_ISDIR(old_dir->i_mode))
	{
		printk("smb_rename: old inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (!new_dir || !S_ISDIR(new_dir->i_mode))
	{
		printk("smb_rename: new inode is NULL or not a directory\n");
		return -ENOENT;
	}

	if (old_dentry->d_name.len > SMB_MAXNAMELEN ||
	    new_dentry->d_name.len > SMB_MAXNAMELEN)
		return -ENAMETOOLONG;

	error = smb_proc_mv(old_dentry, &(old_dentry->d_name),
			    new_dentry, &(new_dentry->d_name));

	if (error == -EEXIST)
	{
		error = smb_proc_unlink(old_dentry, &(new_dentry->d_name));
					
		if (error)
			return error;

		error = smb_proc_mv(old_dentry, &(old_dentry->d_name),
				    new_dentry, &(new_dentry->d_name));
	}

	if (error)
		return error;

	smb_invalid_dir_cache(old_dir->i_ino);
	smb_invalid_dir_cache(new_dir->i_ino);
	return 0;
}
