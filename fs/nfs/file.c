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

static int  nfs_file_mmap(struct inode *, struct file *,
					struct vm_area_struct *);
static long nfs_file_read(struct inode *, struct file *, char *, unsigned long);
static long nfs_file_write(struct inode *, struct file *,
					const char *, unsigned long);
static int  nfs_file_close(struct inode *, struct file *);
static int  nfs_fsync(struct inode *, struct file *);

static struct file_operations nfs_file_operations = {
	NULL,			/* lseek - default */
	nfs_file_read,		/* read */
	nfs_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* select - default */
	NULL,			/* ioctl - default */
	nfs_file_mmap,		/* mmap */
	NULL,			/* no special open is needed */
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


static int
nfs_file_close(struct inode *inode, struct file *file)
{
	int	status;

	dfprintk(VFS, "nfs: close(%x/%ld)\n", inode->i_dev, inode->i_ino);

	if ((status = nfs_flush_dirty_pages(inode, 0, 0)) < 0)
		return status;
	return nfs_write_error(inode);
}

static long
nfs_file_read(struct inode * inode, struct file * file,
				char * buf, unsigned long count)
{
	int	status;

	dfprintk(VFS, "nfs: read(%x/%ld, %lu@%lu)\n",
			inode->i_dev, inode->i_ino, count,
			(unsigned long) file->f_pos);

	if ((status = nfs_revalidate_inode(NFS_SERVER(inode), inode)) < 0)
		return status;
	return generic_file_read(inode, file, buf, count);
}

static int
nfs_file_mmap(struct inode * inode, struct file * file,
				struct vm_area_struct * vma)
{
	int	status;

	dfprintk(VFS, "nfs: mmap(%x/%ld)\n", inode->i_dev, inode->i_ino);

	if ((status = nfs_revalidate_inode(NFS_SERVER(inode), inode)) < 0)
		return status;
	return generic_file_mmap(inode, file, vma);
}

static int nfs_fsync(struct inode *inode, struct file *file)
{
	dfprintk(VFS, "nfs: fsync(%x/%ld)\n", inode->i_dev, inode->i_ino);

	return nfs_flush_dirty_pages(inode, 0, 0);
}

/* 
 * Write to a file (through the page cache).
 */
static long
nfs_file_write(struct inode *inode, struct file *file,
			const char *buf, unsigned long count)
{
	int	result;

	dfprintk(VFS, "nfs: write(%x/%ld (%d), %lu@%lu)\n",
			inode->i_dev, inode->i_ino, atomic_read(&inode->i_count),
			count, (unsigned long) file->f_pos);

	if (!inode) {
		printk("nfs_file_write: inode = NULL\n");
		return -EINVAL;
	}
	if (IS_SWAPFILE(inode)) {
		printk("NFS: attempt to write to active swap file!\n");
		return -EBUSY;
	}
	if ((result = nfs_revalidate_inode(NFS_SERVER(inode), inode)) < 0)
		return result;
	if (!S_ISREG(inode->i_mode)) {
		printk("nfs_file_write: write to non-file, mode %07o\n",
			inode->i_mode);
		return -EINVAL;
	}
	if (count <= 0)
		return 0;

	/* Return error from previous async call */
	if ((result = nfs_write_error(inode)) < 0)
		return result;

	return generic_file_write(inode, file, buf, count);
}

/*
 * Lock a (portion of) a file
 */
int
nfs_lock(struct inode *inode, struct file *filp, int cmd, struct file_lock *fl)
{
	int	status;

	dprintk("NFS: nfs_lock(f=%4x/%ld, t=%x, fl=%x, r=%ld:%ld)\n",
			filp->f_inode->i_dev, filp->f_inode->i_ino,
			fl->fl_type, fl->fl_flags,
			fl->fl_start, fl->fl_end);

	if (!(inode = filp->f_inode))
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

	/* If unlocking a file region, flush dirty pages (unless we've
	 * been killed by a signal, that is). */
	if (cmd == F_SETLK && fl->fl_type == F_UNLCK
	 && !(current->signal & ~current->blocked)) {
		status = nfs_flush_dirty_pages(inode,
			fl->fl_start, fl->fl_end == NLM_OFFSET_MAX? 0 :
			fl->fl_end - fl->fl_start + 1);
		if (status < 0)
			return status;
	}

	if ((status = nlmclnt_proc(inode, cmd, fl)) < 0)
		return status;

	/* Here, we could turn off write-back of pages in the
	 * locked file region */

	return 0;
}
