/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp_lock.h>

/* SLAB cache for filp's. */
static kmem_cache_t *filp_cache;

/* sysctl tunables... */
int nr_files;		/* read only */
int nr_free_files;	/* read only */
int max_files = NR_FILE;/* tunable */

/* Here the new files go */
static LIST_HEAD(anon_list);
/* And here the free ones sit */
static LIST_HEAD(free_list);
/* public *and* exported. Not pretty! */
spinlock_t files_lock = SPIN_LOCK_UNLOCKED;

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
 *
 * SMP-safe.
 */
struct file * get_empty_filp(void)
{
	static int old_max = 0;
	struct file * f;

	file_list_lock();
	if (nr_free_files > NR_RESERVED_FILES) {
	used_one:
		f = list_entry(free_list.next, struct file, f_list);
		list_del(&f->f_list);
		nr_free_files--;
	new_one:
		file_list_unlock();
		memset(f, 0, sizeof(*f));
		atomic_set(&f->f_count,1);
		f->f_version = ++event;
		f->f_uid = current->fsuid;
		f->f_gid = current->fsgid;
		file_list_lock();
		list_add(&f->f_list, &anon_list);
		file_list_unlock();
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
		file_list_unlock();
		f = kmem_cache_alloc(filp_cache, SLAB_KERNEL);
		file_list_lock();
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
	file_list_unlock();
	return NULL;
}

/*
 * Clear and initialize a (private) struct file for the given dentry,
 * and call the open function (if any).  The caller must verify that
 * inode->i_fop is not NULL.
 */
int init_private_file(struct file *filp, struct dentry *dentry, int mode)
{
	memset(filp, 0, sizeof(*filp));
	filp->f_mode   = mode;
	atomic_set(&filp->f_count, 1);
	filp->f_dentry = dentry;
	filp->f_uid    = current->fsuid;
	filp->f_gid    = current->fsgid;
	filp->f_op     = dentry->d_inode->i_fop;
	if (filp->f_op->open)
		return filp->f_op->open(dentry->d_inode, filp);
	else
		return 0;
}

/*
 * Called when retiring the last use of a file pointer.
 */
static void __fput(struct file *filp)
{
	struct dentry * dentry = filp->f_dentry;
	struct vfsmount * mnt = filp->f_vfsmnt;
	struct inode * inode = dentry->d_inode;

	if (filp->f_op && filp->f_op->release)
		filp->f_op->release(inode, filp);
	fops_put(filp->f_op);
	filp->f_dentry = NULL;
	filp->f_vfsmnt = NULL;
	if (filp->f_mode & FMODE_WRITE)
		put_write_access(inode);
	dput(dentry);
	if (mnt)
		mntput(mnt);
}

void _fput(struct file *file)
{
	lock_kernel();
	locks_remove_flock(file);	/* Still need the */
	__fput(file);			/* big lock here. */
	unlock_kernel();

	file_list_lock();
	list_del(&file->f_list);
	list_add(&file->f_list, &free_list);
	nr_free_files++;
	file_list_unlock();
}

/* Here. put_filp() is SMP-safe now. */

void put_filp(struct file *file)
{
	if(atomic_dec_and_test(&file->f_count)) {
		file_list_lock();
		list_del(&file->f_list);
		list_add(&file->f_list, &free_list);
		nr_free_files++;
		file_list_unlock();
	}
}

void file_move(struct file *file, struct list_head *list)
{
	if (!list)
		return;
	file_list_lock();
	list_del(&file->f_list);
	list_add(&file->f_list, list);
	file_list_unlock();
}

void file_moveto(struct file *new, struct file *old)
{
	file_list_lock();
	list_del(&new->f_list);
	list_add(&new->f_list, &old->f_list);
	file_list_unlock();
}

int fs_may_remount_ro(struct super_block *sb)
{
	struct list_head *p;

	/* Check that no files are currently opened for writing. */
	file_list_lock();
	for (p = sb->s_files.next; p != &sb->s_files; p = p->next) {
		struct file *file = list_entry(p, struct file, f_list);
		struct inode *inode = file->f_dentry->d_inode;
		if (!inode)
			continue;

		/* File with pending delete? */
		if (inode->i_nlink == 0)
			goto too_bad;

		/* Writable file? */
		if (S_ISREG(inode->i_mode) && (file->f_mode & FMODE_WRITE))
			goto too_bad;
	}
	file_list_unlock();
	return 1; /* Tis' cool bro. */
too_bad:
	file_list_unlock();
	return 0;
}
