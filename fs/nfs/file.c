/*
 *  linux/fs/nfs/file.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Changes Copyright (C) 1994 by Florian La Roche
 *   - Do not copy data too often around in the kernel.
 *   - In nfs_file_read the return value of kmalloc wasn't checked.
 *   - Put in a better version of read look-ahead buffering. Original idea
 *     and implementation by Wai S Kok elekokws@ee.nus.sg.
 *
 *  Expire cache on write to a file by Wai S Kok (Oct 1994).
 *
 *  Total rewrite of read side for new NFS buffer cache.. Linus.
 *
 *  nfs regular file handling functions
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/nfs_fs.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/segment.h>
#include <asm/system.h>

#define NFSDBG_FACILITY		NFSDBG_FILE

static int  nfs_file_mmap(struct file *, struct vm_area_struct *);
static ssize_t nfs_file_read(struct file *, char *, size_t, loff_t *);
static ssize_t nfs_file_write(struct file *, const char *, size_t, loff_t *);
static int  nfs_file_flush(struct file *);
static int  nfs_fsync(struct file *, struct dentry *dentry);

static struct file_operations nfs_file_operations = {
	NULL,			/* lseek - default */
	nfs_file_read,		/* read */
	nfs_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	nfs_file_mmap,		/* mmap */
	nfs_open,		/* open */
	nfs_file_flush,		/* flush */
	nfs_release,		/* release */
	nfs_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
	nfs_lock,		/* lock */
};

struct inode_operations nfs_file_inode_operations = {
	&nfs_file_operations,	/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* get_block */
	nfs_readpage,		/* readpage */
	nfs_writepage,		/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	nfs_revalidate,		/* revalidate */
};

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

/*
 * Flush all dirty pages, and check for write errors.
 *
 */
static int
nfs_file_flush(struct file *file)
{
	struct inode	*inode = file->f_dentry->d_inode;
	int		status;

	dfprintk(VFS, "nfs: flush(%x/%ld)\n", inode->i_dev, inode->i_ino);

	status = nfs_wb_file(inode, file);
	if (!status) {
		status = file->f_error;
		file->f_error = 0;
	}
	return status;
}

static ssize_t
nfs_file_read(struct file * file, char * buf, size_t count, loff_t *ppos)
{
	struct dentry * dentry = file->f_dentry;
	ssize_t result;

	dfprintk(VFS, "nfs: read(%s/%s, %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) count, (unsigned long) *ppos);

	result = nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
	if (!result)
		result = generic_file_read(file, buf, count, ppos);
	return result;
}

static int
nfs_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry *dentry = file->f_dentry;
	int	status;

	dfprintk(VFS, "nfs: mmap(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	status = nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
	if (!status)
		status = generic_file_mmap(file, vma);
	return status;
}

/*
 * Flush any dirty pages for this process, and check for write errors.
 * The return status from this call provides a reliable indication of
 * whether any write errors occurred for this process.
 */
static int
nfs_fsync(struct file *file, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int status;

	dfprintk(VFS, "nfs: fsync(%x/%ld)\n", inode->i_dev, inode->i_ino);

	status = nfs_wb_file(inode, file);
	if (!status) {
		status = file->f_error;
		file->f_error = 0;
	}
	return status;
}

/*
 * This does the "real" work of the write. The generic routine has
 * allocated the page, locked it, done all the page alignment stuff
 * calculations etc. Now we should just copy the data from user
 * space and write it back to the real medium..
 *
 * If the writer ends up delaying the write, the writer needs to
 * increment the page use counts until he is done with the page.
 */
static int nfs_write_one_page(struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char * buf)
{
	long status;

	bytes -= copy_from_user((u8*)page_address(page) + offset, buf, bytes);
	status = -EFAULT;
	if (bytes) {
		lock_kernel();
		status = nfs_updatepage(file, page, offset, bytes);
		unlock_kernel();
	}
	return status;
}

/* 
 * Write to a file (through the page cache).
 */
static ssize_t
nfs_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct dentry * dentry = file->f_dentry;
	struct inode * inode = dentry->d_inode;
	ssize_t result;

	dfprintk(VFS, "nfs: write(%s/%s(%ld), %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino, (unsigned long) count, (unsigned long) *ppos);

	result = -EBUSY;
	if (IS_SWAPFILE(inode))
		goto out_swapfile;
	result = nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
	if (result)
		goto out;

	result = count;
	if (!count)
		goto out;

	result = generic_file_write(file, buf, count, ppos, nfs_write_one_page);
out:
	return result;

out_swapfile:
	printk(KERN_INFO "NFS: attempt to write to active swap file!\n");
	goto out;
}

/*
 * Lock a (portion of) a file
 */
int
nfs_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode * inode = filp->f_dentry->d_inode;
	int	status = 0;

	dprintk("NFS: nfs_lock(f=%4x/%ld, t=%x, fl=%x, r=%ld:%ld)\n",
			inode->i_dev, inode->i_ino,
			fl->fl_type, fl->fl_flags,
			fl->fl_start, fl->fl_end);

	if (!inode)
		return -EINVAL;

	/* No mandatory locks over NFS */
	if ((inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	/* Fake OK code if mounted without NLM support */
	if (NFS_SERVER(inode)->flags & NFS_MOUNT_NONLM) {
		if (cmd == F_GETLK)
			status = LOCK_USE_CLNT;
		goto out_ok;
	}

	/*
	 * No BSD flocks over NFS allowed.
	 * Note: we could try to fake a POSIX lock request here by
	 * using ((u32) filp | 0x80000000) or some such as the pid.
	 * Not sure whether that would be unique, though, or whether
	 * that would break in other places.
	 */
	if (!fl->fl_owner || (fl->fl_flags & (FL_POSIX|FL_BROKEN)) != FL_POSIX)
		return -ENOLCK;

	/*
	 * Flush all pending writes before doing anything
	 * with locks..
	 */
	status = nfs_wb_all(inode);
	if (status < 0)
		return status;

	if ((status = nlmclnt_proc(inode, cmd, fl)) < 0)
		return status;
	else
		status = 0;

	/*
	 * Make sure we re-validate anything we've got cached.
	 * This makes locking act as a cache coherency point.
	 */
 out_ok:
	NFS_CACHEINV(inode);
	return status;
}
