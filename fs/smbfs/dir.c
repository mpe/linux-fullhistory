/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
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
 smb_dir_read(struct inode *inode, struct file *filp, char *buf, unsigned long count);

static int
 smb_readdir(struct inode *inode, struct file *filp,
	     void *dirent, filldir_t filldir);

static struct smb_inode_info *
 smb_find_dir_inode(struct inode *parent, const char *name, int len);

static int
 smb_lookup(struct inode *dir, const char *__name,
	    int len, struct inode **result);

static int
 smb_create(struct inode *dir, const char *name, int len, int mode,
	    struct inode **result);

static int
 smb_mkdir(struct inode *dir, const char *name, int len, int mode);

static int
 smb_rmdir(struct inode *dir, const char *name, int len);

static int
 smb_unlink(struct inode *dir, const char *name, int len);

static int
 smb_rename(struct inode *old_dir, const char *old_name, int old_len,
	    struct inode *new_dir, const char *new_name, int new_len);

static struct file_operations smb_dir_operations =
{
	NULL,			/* lseek - default */
	smb_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	smb_readdir,		/* readdir */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl - default */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static int
strncasecmp(const char *s1, const char *s2, int len)
{
	int result = 0;

	for (; len > 0; len -= 1)
	{
		char c1, c2;

		c1 = (*s1 >= 'a' && *s1 <= 'z') ? *s1 - ('a' - 'A') : *s1;
		c2 = (*s2 >= 'a' && *s2 <= 'z') ? *s2 - ('a' - 'A') : *s2;
		s1 += 1;
		s2 += 1;

		if ((result = c1 - c2) != 0 || c1 == 0)
		{
			return result;
		}
	}
	return result;
}

static int
compare_filename(const struct smb_server *server,
		 const char *s1, int len, struct smb_dirent *entry)
{
	if (len != entry->len)
	{
		return 1;
	}
	if (server->case_handling == CASE_DEFAULT)
	{
		return strncasecmp(s1, entry->name, len);
	}
	return strncmp(s1, entry->name, len);
}

struct smb_inode_info *
smb_find_inode(struct smb_server *server, ino_t ino)
{
	struct smb_inode_info *root = &(server->root);
	struct smb_inode_info *this = root;

	do
	{
		if (ino == smb_info_ino(this))
		{
			return this;
		}
		this = this->next;
	}
	while (this != root);

	return NULL;
}

static ino_t
smb_fresh_inodes(struct smb_server *server, int no)
{
	static ino_t seed = 1;
	struct smb_inode_info *root = &(server->root);
	struct smb_inode_info *this;

      retry:
	if (seed + no <= no)
	{
		/* avoid inode number of 0 at wrap-around */
		seed += no;
	}
	this = root;
	do
	{
		/* We assume that ino_t is unsigned! */
		if (this->finfo.f_ino - seed < no)
		{
			seed += no;
			goto retry;
		}
		this = this->next;
	}
	while (this != root);

	seed += no;

	return seed - no;
}

static long
smb_dir_read(struct inode *inode, struct file *filp, char *buf, unsigned long count)
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
smb_refill_dir_cache(struct smb_server *server, struct inode *dir,
		     unsigned long f_pos)
{
	int result;
	static struct semaphore sem = MUTEX;
	int i;
	ino_t ino;

	do
	{
		down(&sem);
		result = smb_proc_readdir(server, dir, f_pos,
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

		ino = smb_fresh_inodes(server, c_size);
		for (i = 0; i < c_size; i++)
		{
			c_entry[i].f_ino = ino;
			ino += 1;
		}

		up(&sem);

	}
	while ((c_dev != dir->i_dev) || (c_ino != dir->i_ino));

	return result;
}

static int
smb_readdir(struct inode *dir, struct file *filp,
	    void *dirent, filldir_t filldir)
{
	int result, i = 0;
	struct smb_dirent *entry = NULL;
	struct smb_server *server = SMB_SERVER(dir);

	DPRINTK("smb_readdir: filp->f_pos = %d\n", (int) filp->f_pos);
	DDPRINTK("smb_readdir: dir->i_ino = %ld, c_ino = %ld\n",
		 dir->i_ino, c_ino);

	if ((dir == NULL) || !S_ISDIR(dir->i_mode))
	{
		printk("smb_readdir: dir is NULL or not a directory\n");
		return -EBADF;
	}
	if (c_entry == NULL)
	{
		i = sizeof(struct smb_dirent) * SMB_READDIR_CACHE_SIZE;
		c_entry = (struct smb_dirent *) smb_vmalloc(i);
		if (c_entry == NULL)
		{
			printk("smb_readdir: no MEMORY for cache\n");
			return -ENOMEM;
		}
	}
	if (filp->f_pos == 0)
	{
		c_ino = 0;
		c_dev = 0;
		c_seen_eof = 0;

		if (filldir(dirent, ".", 1, filp->f_pos,
			    smb_info_ino(SMB_INOP(dir))) < 0)
		{
			return 0;
		}
		filp->f_pos += 1;
	}
	if (filp->f_pos == 1)
	{
		if (filldir(dirent, "..", 2, filp->f_pos,
			    smb_info_ino(SMB_INOP(dir)->dir)) < 0)
		{
			return 0;
		}
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
		result = smb_refill_dir_cache(server, dir, filp->f_pos);
		if (result <= 0)
		{
			return result;
		}
		entry = c_entry;
	}
	while (entry < &(c_entry[c_size]))
	{
		/* We found it.  For getwd(), we have to return the
		   correct inode in d_ino if the inode is currently in
		   use. Otherwise the inode number does not
		   matter. (You can argue a lot about this..) */

		struct smb_inode_info *ino_info
		= smb_find_dir_inode(dir, entry->name, entry->len);

		ino_t ino = entry->f_ino;

		if (ino_info != NULL)
		{
			ino = smb_info_ino(ino_info);
		}
		DDPRINTK("smb_readdir: entry->name = %s\n", entry->name);
		DDPRINTK("smb_readdir: entry->f_pos = %ld\n", entry->f_pos);

		if (filldir(dirent, entry->name, strlen(entry->name),
			    entry->f_pos, ino) < 0)
		{
			break;
		}
		if ((dir->i_dev != c_dev) || (dir->i_ino != c_ino)
		    || (entry->f_pos != filp->f_pos))
		{
			/* Someone has destroyed the cache while we slept
			   in filldir */
			break;
		}
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
	/* TODO: check for dev as well */
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

/* Insert a NEW smb_inode_info into the inode tree of our filesystem,
   under dir. The caller must assure that it's not already there. We
   assume that path is allocated for us. */

static struct inode *
smb_iget(struct inode *dir, struct smb_inode_info *new_inode_info)
{
	struct inode *inode;
	struct smb_inode_info *root;

	if ((dir == NULL) || (new_inode_info == NULL))
	{
		printk("smb_iget: parameter is NULL\n");
		return NULL;
	}
	new_inode_info->state = SMB_INODE_LOOKED_UP;
	new_inode_info->nused = 0;
	new_inode_info->dir = SMB_INOP(dir);

	SMB_INOP(dir)->nused += 1;

	/* We have to link the new inode_info into the doubly linked
	   list of inode_infos to make a complete linear search
	   possible. */

	root = &(SMB_SERVER(dir)->root);

	new_inode_info->prev = root;
	new_inode_info->next = root->next;
	root->next->prev = new_inode_info;
	root->next = new_inode_info;

	if (!(inode = iget(dir->i_sb, smb_info_ino(new_inode_info))))
	{
		new_inode_info->next->prev = new_inode_info->prev;
		new_inode_info->prev->next = new_inode_info->next;
		SMB_INOP(dir)->nused -= 1;

		printk("smb_iget: iget failed!");
		return NULL;
	}
	return inode;
}

void
smb_free_inode_info(struct smb_inode_info *i)
{
	if (i == NULL)
	{
		printk("smb_free_inode: i == NULL\n");
		return;
	}
	i->state = SMB_INODE_CACHED;
	while ((i->nused == 0) && (i->state == SMB_INODE_CACHED))
	{
		struct smb_inode_info *dir = i->dir;

		i->next->prev = i->prev;
		i->prev->next = i->next;

		smb_kfree_s(i, sizeof(struct smb_inode_info));

		if (dir == NULL)
		{
			return;
		}
		dir->nused -= 1;
		i = dir;
	}
}

void
smb_init_root(struct smb_server *server)
{
	struct smb_inode_info *root = &(server->root);

	root->state = SMB_INODE_LOOKED_UP;
	root->nused = 1;
	root->dir = NULL;
	root->next = root->prev = root;

	return;
}

void
smb_free_all_inodes(struct smb_server *server)
{
	/* Here nothing should be to do. I do not know whether it's
	   better to leave some memory allocated or be stuck in an
	   endless loop */
#if 1
	struct smb_inode_info *root = &(server->root);

	if (root->next != root)
	{
		printk("smb_free_all_inodes: INODES LEFT!!!\n");
	}
	while (root->next != root)
	{
		printk("smb_free_all_inodes: freeing inode\n");
		smb_free_inode_info(root->next);
		/* In case we have an endless loop.. */
		schedule();
	}
#endif

	return;
}

/* This has to be called when a connection has gone down, so that all
   file-handles we got from the server are invalid */
void
smb_invalidate_all_inodes(struct smb_server *server)
{
	struct smb_inode_info *ino = &(server->root);

	do
	{
		ino->finfo.opened = 0;
		ino = ino->next;
	}
	while (ino != &(server->root));

	return;
}

/* We will search the inode that belongs to this name, currently by a
   complete linear search through the inodes belonging to this
   filesystem. This has to be fixed. */
static struct smb_inode_info *
smb_find_dir_inode(struct inode *parent, const char *name, int len)
{
	struct smb_server *server = SMB_SERVER(parent);
	struct smb_inode_info *dir = SMB_INOP(parent);
	struct smb_inode_info *result = &(server->root);

	if (name == NULL)
	{
		return NULL;
	}
	if ((len == 1) && (name[0] == '.'))
	{
		return dir;
	}
	if ((len == 2) && (name[0] == '.') && (name[1] == '.'))
	{
		return dir->dir;
	}
	do
	{
		if (result->dir == dir)
		{
			if (compare_filename(server, name, len,
					     &(result->finfo)) == 0)
			{
				return result;
			}
		}
		result = result->next;
	}
	while (result != &(server->root));

	return NULL;
}

static int
smb_lookup(struct inode *dir, const char *name, int len,
	   struct inode **result)
{
	struct smb_dirent finfo;
	struct smb_inode_info *result_info;
	int error;
	int found_in_cache;

	struct smb_inode_info *new_inode_info = NULL;

	*result = NULL;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_lookup: inode is NULL or not a directory.\n");
		iput(dir);
		return -ENOENT;
	}
	DDPRINTK("smb_lookup: %s\n", name);

	/* Fast cheat for . */
	if (len == 0 || (len == 1 && name[0] == '.'))
	{
		*result = dir;
		return 0;
	}
	/* ..and for .. */
	if (len == 2 && name[0] == '.' && name[1] == '.')
	{
		struct smb_inode_info *parent = SMB_INOP(dir)->dir;

		if (parent->state == SMB_INODE_CACHED)
		{
			parent->state = SMB_INODE_LOOKED_UP;
		}
		*result = iget(dir->i_sb, smb_info_ino(parent));
		iput(dir);
		if (*result == 0)
		{
			return -EACCES;
		}
		return 0;
	}
	result_info = smb_find_dir_inode(dir, name, len);

      in_tree:
	if (result_info != NULL)
	{
		if (result_info->state == SMB_INODE_CACHED)
		{
			result_info->state = SMB_INODE_LOOKED_UP;
		}
		*result = iget(dir->i_sb, smb_info_ino(result_info));
		iput(dir);

		if (new_inode_info != NULL)
		{
			smb_kfree_s(new_inode_info,
				    sizeof(struct smb_inode_info));
		}
		if (*result == NULL)
		{
			return -EACCES;
		}
		return 0;
	}
	/* If the file is in the dir cache, we do not have to ask the
	   server. */
	found_in_cache = 0;

	if ((dir->i_dev == c_dev) && (dir->i_ino == c_ino) && (c_size != 0))
	{
		int first = c_last_returned_index;
		int i;

		i = first;
		do
		{
			if (compare_filename(SMB_SERVER(dir), name, len,
					     &(c_entry[i])) == 0)
			{
				finfo = c_entry[i];
				found_in_cache = 1;
				break;
			}
			i = (i + 1) % c_size;
		}
		while (i != first);
	}
	if (found_in_cache == 0)
	{
		DPRINTK("smb_lookup: not found in cache: %s\n", name);
		if (len > SMB_MAXNAMELEN)
		{
			iput(dir);
			return -ENAMETOOLONG;
		}
		error = smb_proc_getattr(dir, name, len, &finfo);
		if (error < 0)
		{
			iput(dir);
			return error;
		}
		finfo.f_ino = smb_fresh_inodes(SMB_SERVER(dir), 1);
	}
	new_inode_info = smb_kmalloc(sizeof(struct smb_inode_info),
				     GFP_KERNEL);

	/* Here somebody else might have inserted the inode */

	result_info = smb_find_dir_inode(dir, name, len);
	if (result_info != NULL)
	{
		goto in_tree;
	}
	new_inode_info->finfo = finfo;

	DPRINTK("attr: %x\n", finfo.attr);

	if ((*result = smb_iget(dir, new_inode_info)) == NULL)
	{
		smb_kfree_s(new_inode_info, sizeof(struct smb_inode_info));
		iput(dir);
		return -EACCES;
	}
	DDPRINTK("smb_lookup: %s => %lu\n", name, (unsigned long) result_info);
	iput(dir);
	return 0;
}

static int
smb_create(struct inode *dir, const char *name, int len, int mode,
	   struct inode **result)
{
	int error;
	struct smb_dirent entry;
	struct smb_inode_info *new_inode_info;

	*result = NULL;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_create: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	new_inode_info = smb_kmalloc(sizeof(struct smb_inode_info),
				     GFP_KERNEL);
	if (new_inode_info == NULL)
	{
		iput(dir);
		return -ENOMEM;
	}
	error = smb_proc_create(dir, name, len, 0, CURRENT_TIME);
	if (error < 0)
	{
		smb_kfree_s(new_inode_info, sizeof(struct smb_inode_info));
		iput(dir);
		return error;
	}
	smb_invalid_dir_cache(dir->i_ino);

	if ((error = smb_proc_getattr(dir, name, len, &entry)) < 0)
	{
		smb_kfree_s(new_inode_info, sizeof(struct smb_inode_info));
		iput(dir);
		return error;
	}
	entry.f_ino = smb_fresh_inodes(SMB_SERVER(dir), 1);

	new_inode_info->finfo = entry;

	if ((*result = smb_iget(dir, new_inode_info)) == NULL)
	{
		smb_kfree_s(new_inode_info, sizeof(struct smb_inode_info));
		iput(dir);
		return error;
	}
	iput(dir);
	return 0;
}

static int
smb_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		iput(dir);
		return -EINVAL;
	}
	if ((error = smb_proc_mkdir(dir, name, len)) == 0)
	{
		smb_invalid_dir_cache(dir->i_ino);
	}
	iput(dir);
	return error;
}

static int
smb_rmdir(struct inode *dir, const char *name, int len)
{
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_rmdir: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if (smb_find_dir_inode(dir, name, len) != NULL)
	{
		error = -EBUSY;
	} else
	{
		if ((error = smb_proc_rmdir(dir, name, len)) == 0)
		{
			smb_invalid_dir_cache(dir->i_ino);
		}
	}
	iput(dir);
	return error;
}

static int
smb_unlink(struct inode *dir, const char *name, int len)
{
	int error;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("smb_unlink: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if (smb_find_dir_inode(dir, name, len) != NULL)
	{
		error = -EBUSY;
	} else
	{
		if ((error = smb_proc_unlink(dir, name, len)) == 0)
		{
			smb_invalid_dir_cache(dir->i_ino);
		}
	}
	iput(dir);
	return error;
}

static int
smb_rename(struct inode *old_dir, const char *old_name, int old_len,
	   struct inode *new_dir, const char *new_name, int new_len)
{
	int res;

	if (!old_dir || !S_ISDIR(old_dir->i_mode))
	{
		printk("smb_rename: old inode is NULL or not a directory\n");
		res = -ENOENT;
		goto finished;
	}
	if (!new_dir || !S_ISDIR(new_dir->i_mode))
	{
		printk("smb_rename: new inode is NULL or not a directory\n");
		res = -ENOENT;
		goto finished;
	}
	if ((smb_find_dir_inode(old_dir, old_name, old_len) != NULL)
	    || (smb_find_dir_inode(new_dir, new_name, new_len) != NULL))
	{
		res = -EBUSY;
		goto finished;
	}
	res = smb_proc_mv(old_dir, old_name, old_len,
			  new_dir, new_name, new_len);

	if (res == -EEXIST)
	{
		int res1 = smb_proc_unlink(old_dir, new_name, new_len);

		if (res1 == 0)
		{
			res = smb_proc_mv(old_dir, old_name, old_len,
					  new_dir, new_name, new_len);
		}
	}
	if (res == 0)
	{
		smb_invalid_dir_cache(old_dir->i_ino);
		smb_invalid_dir_cache(new_dir->i_ino);
	}
      finished:
	iput(old_dir);
	iput(new_dir);
	return res;
}
