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

/* Get more free filp's. */
static int grow_files(void)
{
	int i = 16;

	while(i--) {
		struct file * file = kmem_cache_alloc(filp_cache, SLAB_KERNEL);
		if(!file) {
			if(i == 15)
				return 0;
			goto got_some;
		}

		insert_file_free(file);
		nr_files++;
	}
got_some:
	return 1;
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
	struct file * f;

again:
	if((f = free_filps) != NULL) {
		remove_filp(f);
		memset(f, 0, sizeof(*f));
		f->f_count = 1;
		f->f_version = ++event;
		put_inuse(f);
	} else {
		int max = max_files;

		/* Reserve a few files for the super-user.. */
		if (current->euid)
			max -= 10;

		if (nr_files < max && grow_files())
			goto again;

		/* Big problems... */
	}
	return f;
}

#ifdef CONFIG_QUOTA

void add_dquot_ref(kdev_t dev, short type)
{
	struct file *filp;

	for (filp = inuse_filps; filp; filp = filp->f_next) {
		if (!filp->f_inode || filp->f_inode->i_dev != dev)
			continue;
		if (filp->f_mode & FMODE_WRITE && filp->f_inode->i_sb->dq_op) {
			filp->f_inode->i_sb->dq_op->initialize(filp->f_inode, type);
			filp->f_inode->i_flags |= S_WRITE;
		}
	}
}

void reset_dquot_ptrs(kdev_t dev, short type)
{
	struct file *filp;

	for (filp = inuse_filps; filp; filp = filp->f_next) {
		if (!filp->f_inode || filp->f_inode->i_dev != dev)
			continue;
		if (IS_WRITABLE(filp->f_inode)) {
			filp->f_inode->i_dquot[type] = NODQUOT;
			filp->f_inode->i_flags &= ~S_WRITE;
		}
	}
}

#endif
