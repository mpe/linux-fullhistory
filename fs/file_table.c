/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>

/* SLAB cache for filp's. */
static kmem_cache_t *filp_cache;

/* sysctl tunables... */
int nr_files = 0;
int max_files = NR_FILE;

/* Free list management, if you are here you must have f_count == 0 */
static struct file * free_filps = NULL;

void insert_file_free(struct file *file)
{
	if((file->f_next = free_filps) != NULL)
		free_filps->f_pprev = &file->f_next;
	free_filps = file;
	file->f_pprev = &free_filps;
}

/* The list of in-use filp's must be exported (ugh...) */
struct file *inuse_filps = NULL;

static inline void put_inuse(struct file *file)
{
	if((file->f_next = inuse_filps) != NULL)
		inuse_filps->f_pprev = &file->f_next;
	inuse_filps = file;
	file->f_pprev = &inuse_filps;
}

void file_table_init(void)
{
	filp_cache = kmem_cache_create("filp", sizeof(struct file),
				       0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!filp_cache)
		panic("VFS: Cannot alloc filp SLAB cache.");
}

/* Find an unused file structure and return a pointer to it.
 * Returns NULL, if there are no more free file structures or
 * we run out of memory.
 */
struct file * get_empty_filp(void)
{
	static int old_max = 0;
	struct file * f;

	f = free_filps;
	if (!f) 
		goto get_more;
	remove_filp(f);
got_one:
	memset(f, 0, sizeof(*f));
	f->f_count = 1;
	f->f_version = ++event;
	put_inuse(f);
	return f;

get_more:
	/* Reserve a few files for the super-user.. */
	if (nr_files < (current->euid ? max_files - 10 : max_files)) {
		f = kmem_cache_alloc(filp_cache, SLAB_KERNEL);
		if (f) {
			nr_files++;
			goto got_one;
		}
		/* Big problems... */
		printk("VFS: filp allocation failed\n");

	} else if (max_files > old_max) {
		printk("VFS: file-max limit %d reached\n", max_files);
		old_max = max_files;
	}
	return NULL;
}

/*
 * Clear and initialize a (private) struct file for the given dentry,
 * and call the open function (if any).  The caller must verify that
 * inode->i_op and inode->i_op->default_file_ops are not NULL.
 */
int init_private_file(struct file *filp, struct dentry *dentry, int mode)
{
	memset(filp, 0, sizeof(*filp));
	filp->f_mode   = mode;
	filp->f_count  = 1;
	filp->f_dentry = dentry;
	filp->f_op     = dentry->d_inode->i_op->default_file_ops;
	if (filp->f_op->open)
		return filp->f_op->open(dentry->d_inode, filp);
	else
		return 0;
}

#ifdef CONFIG_QUOTA

void add_dquot_ref(kdev_t dev, short type)
{
	struct file *filp;

	for (filp = inuse_filps; filp; filp = filp->f_next) {
		struct inode * inode;
		if (!filp->f_dentry)
			continue;
		inode = filp->f_dentry->d_inode;
		if (!inode || inode->i_dev != dev)
			continue;
		if (filp->f_mode & FMODE_WRITE && inode->i_sb->dq_op) {
			inode->i_sb->dq_op->initialize(inode, type);
			inode->i_flags |= S_WRITE;
		}
	}
}

void reset_dquot_ptrs(kdev_t dev, short type)
{
	struct file *filp;

	for (filp = inuse_filps; filp; filp = filp->f_next) {
		struct inode * inode;
		if (!filp->f_dentry)
			continue;
		inode = filp->f_dentry->d_inode;
		if (!inode || inode->i_dev != dev)
			continue;
		if (IS_WRITABLE(inode)) {
			inode->i_dquot[type] = NODQUOT;
			inode->i_flags &= ~S_WRITE;
		}
	}
}

#endif
