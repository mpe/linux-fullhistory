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
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/eventpoll.h>
#include <linux/mount.h>


/* sysctl tunables... */
struct files_stat_struct files_stat = {
	.max_files = NR_FILE
};

/* list of free filps for root */
static LIST_HEAD(free_list);
/* public *and* exported. Not pretty! */
spinlock_t __cacheline_aligned_in_smp files_lock = SPIN_LOCK_UNLOCKED;

static spinlock_t filp_count_lock = SPIN_LOCK_UNLOCKED;

/* slab constructors and destructors are called from arbitrary
 * context and must be fully threaded - use a local spinlock
 * to protect files_stat.nr_files
 */
void filp_ctor(void * objp, struct kmem_cache_s *cachep, unsigned long cflags)
{
	if ((cflags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		unsigned long flags;
		spin_lock_irqsave(&filp_count_lock, flags);
		files_stat.nr_files++;
		spin_unlock_irqrestore(&filp_count_lock, flags);
	}
}

void filp_dtor(void * objp, struct kmem_cache_s *cachep, unsigned long dflags)
{
	unsigned long flags;
	spin_lock_irqsave(&filp_count_lock, flags);
	files_stat.nr_files--;
	spin_unlock_irqrestore(&filp_count_lock, flags);
}

static inline void file_free(struct file* f)
{
	if (files_stat.nr_free_files <= NR_RESERVED_FILES) {
		list_add(&f->f_list, &free_list);
		files_stat.nr_free_files++;
	} else {
		kmem_cache_free(filp_cachep, f);
	}
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

	if (likely(files_stat.nr_files < files_stat.max_files)) {
		f = kmem_cache_alloc(filp_cachep, SLAB_KERNEL);
		if (f) {
got_one:
			memset(f, 0, sizeof(*f));
			if (security_file_alloc(f)) {
				file_list_lock();
				file_free(f);
				file_list_unlock();

				return NULL;
			}
			eventpoll_init_file(f);
			atomic_set(&f->f_count,1);
			f->f_uid = current->fsuid;
			f->f_gid = current->fsgid;
			f->f_owner.lock = RW_LOCK_UNLOCKED;
			/* f->f_version, f->f_list.next: 0 */
			return f;
		}
	}
	/* Use a reserved one if we're the superuser */
	file_list_lock();
	if (files_stat.nr_free_files && !current->euid) {
		f = list_entry(free_list.next, struct file, f_list);
		list_del(&f->f_list);
		files_stat.nr_free_files--;
		file_list_unlock();
		goto got_one;
	}
	file_list_unlock();
	/* Ran out of filps - report that */
	if (files_stat.max_files > old_max) {
		printk(KERN_INFO "VFS: file-max limit %d reached\n", files_stat.max_files);
		old_max = files_stat.max_files;
	} else {
		/* Big problems... */
		printk(KERN_WARNING "VFS: filp allocation failed\n");
	}
	return NULL;
}

/*
 * Clear and initialize a (private) struct file for the given dentry,
 * allocate the security structure, and call the open function (if any).  
 * The file should be released using close_private_file.
 */
int open_private_file(struct file *filp, struct dentry *dentry, int flags)
{
	int error;
	memset(filp, 0, sizeof(*filp));
	eventpoll_init_file(filp);
	filp->f_flags  = flags;
	filp->f_mode   = (flags+1) & O_ACCMODE;
	atomic_set(&filp->f_count, 1);
	filp->f_dentry = dentry;
	filp->f_uid    = current->fsuid;
	filp->f_gid    = current->fsgid;
	filp->f_op     = dentry->d_inode->i_fop;
	filp->f_list.next = NULL;
	error = security_file_alloc(filp);
	if (!error)
		if (filp->f_op && filp->f_op->open) {
			error = filp->f_op->open(dentry->d_inode, filp);
			if (error)
				security_file_free(filp);
		}
	return error;
}

/*
 * Release a private file by calling the release function (if any) and
 * freeing the security structure.
 */
void close_private_file(struct file *file)
{
	struct inode * inode = file->f_dentry->d_inode;

	if (file->f_op && file->f_op->release)
		file->f_op->release(inode, file);
	security_file_free(file);
}

void fput(struct file * file)
{
	if (atomic_dec_and_test(&file->f_count))
		__fput(file);
}

/* __fput is called from task context when aio completion releases the last
 * last use of a struct file *.  Do not use otherwise.
 */
void __fput(struct file * file)
{
	struct dentry * dentry = file->f_dentry;
	struct vfsmount * mnt = file->f_vfsmnt;
	struct inode * inode = dentry->d_inode;

	/*
	 * The function eventpoll_release() should be the first called
	 * in the file cleanup chain.
	 */
	eventpoll_release(file);
	locks_remove_flock(file);

	if (file->f_op && file->f_op->release)
		file->f_op->release(inode, file);
	security_file_free(file);
	fops_put(file->f_op);
	if (file->f_mode & FMODE_WRITE)
		put_write_access(inode);
	file_list_lock();
	file->f_dentry = NULL;
	file->f_vfsmnt = NULL;
	if (file->f_list.next)
		list_del(&file->f_list);
	file_free(file);
	file_list_unlock();
	dput(dentry);
	mntput(mnt);
}

struct file * fget(unsigned int fd)
{
	struct file * file;
	struct files_struct *files = current->files;

	read_lock(&files->file_lock);
	file = fcheck(fd);
	if (file)
		get_file(file);
	read_unlock(&files->file_lock);
	return file;
}

/* Here. put_filp() is SMP-safe now. */

void put_filp(struct file *file)
{
	if(atomic_dec_and_test(&file->f_count)) {
		security_file_free(file);
		file_list_lock();
		if (file->f_list.next)
			list_del(&file->f_list);
		file_free(file);
		file_list_unlock();
	}
}

void file_move(struct file *file, struct list_head *list)
{
	if (!list)
		return;
	file_list_lock();
	if (file->f_list.next)
		list_del(&file->f_list);
	list_add(&file->f_list, list);
	file_list_unlock();
}

void file_kill(struct file *file)
{
	file_list_lock();
	if (file->f_list.next)
		list_del(&file->f_list);
	file->f_list.next = NULL;
	file_list_unlock();
}

int fs_may_remount_ro(struct super_block *sb)
{
	struct list_head *p;

	/* Check that no files are currently opened for writing. */
	file_list_lock();
	list_for_each(p, &sb->s_files) {
		struct file *file = list_entry(p, struct file, f_list);
		struct inode *inode = file->f_dentry->d_inode;

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

void __init files_init(unsigned long mempages)
{ 
	int n; 
	/* One file with associated inode and dcache is very roughly 1K. 
	 * Per default don't use more than 10% of our memory for files. 
	 */ 

	n = (mempages * (PAGE_SIZE / 1024)) / 10;
	files_stat.max_files = n; 
	if (files_stat.max_files < NR_FILE)
		files_stat.max_files = NR_FILE;
} 

