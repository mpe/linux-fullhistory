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

#include <asm/segment.h>
#include <asm/system.h>

#define NFSDBG_FACILITY		NFSDBG_FILE

static int  nfs_file_mmap(struct file *, struct vm_area_struct *);
static ssize_t nfs_file_read(struct file *, char *, size_t, loff_t *);
static ssize_t nfs_file_write(struct file *, const char *, size_t, loff_t *);
static int  nfs_file_flush(struct file *);
static int  nfs_file_close(struct inode *, struct file *);
static int  nfs_fsync(struct file *, struct dentry *dentry);

static struct file_operations nfs_file_operations = {
	NULL,			/* lseek - default */
	nfs_file_read,		/* read */
	nfs_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	nfs_file_mmap,		/* mmap */
	NULL,			/* no special open is needed */
	nfs_file_flush,		/* flush */
	nfs_file_close,		/* release */
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
	nfs_readpage,		/* readpage */
	nfs_writepage,		/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	nfs_updatepage,		/* updatepage */
	nfs_revalidate,		/* revalidate */
};

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

/*
 * Sync the file..
 */
static int
nfs_file_close(struct inode *inode, struct file *file)
{
	int	status;

	dfprintk(VFS, "nfs: close(%x/%ld)\n", inode->i_dev, inode->i_ino);

	status = nfs_wb_all(inode);
	if (!status)
		status = nfs_write_error(inode);
	return status;
}

/*
 * Flush all dirty pages, and check for write errors.
 *
 * We should probably do this better - this does get called at every
 * close, so we should probably just flush the changes that "this"
 * file has done and report on only those.
 *
 * Right now we use the "flush everything" approach. Overkill, but
 * works.
 */
static int
nfs_file_flush(struct file *file)
{
	return nfs_file_close(file->f_dentry->d_inode, file);
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

	status = nfs_wb_pid(inode, current->pid);
	if (!status)
		status = nfs_write_error(inode);
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

	/* Check for an error from a previous async call */
	result = nfs_write_error(inode);
	if (!result)
		result = generic_file_write(file, buf, count, ppos);
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
	int	status;

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
	if (NFS_SERVER(inode)->flags & NFS_MOUNT_NONLM)
		return 0;

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

	/*
	 * Make sure we re-validate anything we've got cached.
	 * This makes locking act as a cache coherency point.
	 */
	NFS_CACHEINV(inode);
	return 0;
}
