/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/init.h>

/* SLAB cache for filp's. */
static kmem_cache_t *filp_cache;

/* sysctl tunables... */
int nr_files = 0;	/* read only */
int nr_free_files = 0;	/* read only */
int max_files = NR_FILE;/* tunable */

/* Free list management, if you are here you must have f_count == 0 */
static struct file * free_filps = NULL;

static void insert_file_free(struct file *file)
{
	if((file->f_next = free_filps) != NULL)
		free_filps->f_pprev = &file->f_next;
	free_filps = file;
	file->f_pprev = &free_filps;
	nr_free_files++;
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

/* It does not matter which list it is on. */
static inline void remove_filp(struct file *file)
{
	if(file->f_next)
		file->f_next->f_pprev = file->f_pprev;
	*file->f_pprev = file->f_next;
}


void __init file_table_init(void)
{
	filp_cache = kmem_cache_create("filp", sizeof(struct file),
				       0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!filp_cache)
		panic("VFS: Cannot alloc filp SLAB cache.");
	/*
	 * We could allocate the reserved files here, but really
	 * shouldn't need to: the normal boot process will create
	 * plenty of free files.
	 */
}

/* Find an unused file structure and return a pointer to it.
 * Returns NULL, if there are no more free file structures or
 * we run out of memory.
 */
struct file * get_empty_filp(void)
{
	static int old_max = 0;
	struct file * f;

	if (nr_free_files > NR_RESERVED_FILES) {
	used_one:
		f = free_filps;
		remove_filp(f);
		nr_free_files--;
	new_one:
		memset(f, 0, sizeof(*f));
		f->f_count = 1;
		f->f_version = ++event;
		put_inuse(f);
		return f;
	}
	/*
	 * Use a reserved one if we're the superuser
	 */
	if (nr_free_files && !current->euid)
		goto used_one;
	/*
	 * Allocate a new one if we're below the limit.
	 */
	if (nr_files < max_files) {
		f = kmem_cache_alloc(filp_cache, SLAB_KERNEL);
		if (f) {
			nr_files++;
			goto new_one;
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

void fput(struct file *file)
{
	int count = file->f_count-1;

	if (!count) {
		locks_remove_flock(file);
		__fput(file);
		file->f_count = 0;
		remove_filp(file);
		insert_file_free(file);
	} else
		file->f_count = count;
}

void put_filp(struct file *file)
{
	if(--file->f_count == 0) {
		remove_filp(file);
		insert_file_free(file);
	}
}
