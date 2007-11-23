/*
 *  file.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/locks.h>
#include <linux/malloc.h>

#include <linux/ncp_fs.h>
#include "ncplib_kernel.h"

static inline unsigned int min(unsigned int a, unsigned int b)
{
	return a < b ? a : b;
}

static int ncp_fsync(struct file *file, struct dentry *dentry)
{
	return 0;
}

/*
 * Open a file with the specified read/write mode.
 */
int ncp_make_open(struct inode *inode, int right)
{
	int error;
	int access;

	error = -EINVAL;
	if (!inode) {
		printk(KERN_ERR "ncp_make_open: got NULL inode\n");
		goto out;
	}

	DPRINTK("ncp_make_open: opened=%d, volume # %u, dir entry # %u\n",
		NCP_FINFO(inode)->opened, 
		NCP_FINFO(inode)->volNumber, 
		NCP_FINFO(inode)->dirEntNum);
	error = -EACCES;
	lock_super(inode->i_sb);
	if (!NCP_FINFO(inode)->opened) {
		struct ncp_entry_info finfo;
		int result;

		finfo.i.dirEntNum = NCP_FINFO(inode)->dirEntNum;
		finfo.i.volNumber = NCP_FINFO(inode)->volNumber;
		/* tries max. rights */
		finfo.access = O_RDWR;
		result = ncp_open_create_file_or_subdir(NCP_SERVER(inode),
					NULL, NULL, OC_MODE_OPEN,
					0, AR_READ | AR_WRITE, &finfo);
		if (!result)
			goto update;
		/* RDWR did not succeeded, try readonly or writeonly as requested */
		switch (right) {
			case O_RDONLY:
				finfo.access = O_RDONLY;
				result = ncp_open_create_file_or_subdir(NCP_SERVER(inode),
					NULL, NULL, OC_MODE_OPEN,
					0, AR_READ, &finfo);
				break;
			case O_WRONLY:
				finfo.access = O_WRONLY;
				result = ncp_open_create_file_or_subdir(NCP_SERVER(inode),
					NULL, NULL, OC_MODE_OPEN,
					0, AR_WRITE, &finfo);
				break;
		}
		if (result) {
			PPRINTK("ncp_make_open: failed, result=%d\n", result);
			goto out_unlock;
		}
		/*
		 * Update the inode information.
		 */
	update:
		ncp_update_inode(inode, &finfo);
	}

	access = NCP_FINFO(inode)->access;
	PPRINTK("ncp_make_open: file open, access=%x\n", access);
	if (access == right || access == O_RDWR)
		error = 0;

out_unlock:
	unlock_super(inode->i_sb);
out:
	return error;
}

static ssize_t
ncp_file_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	size_t already_read = 0;
	off_t pos;
	size_t bufsize;
	int error;
	void* freepage;
	size_t freelen;

	DPRINTK("ncp_file_read: enter %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -EINVAL;
	if (inode == NULL) {
		DPRINTK("ncp_file_read: inode = NULL\n");
		goto out;
	}
	error = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(inode)))
		goto out;
	error = -EINVAL;
	if (!S_ISREG(inode->i_mode)) {
		DPRINTK("ncp_file_read: read from non-file, mode %07o\n",
			inode->i_mode);
		goto out;
	}

	pos = *ppos;
/* leave it out on server ...
	if (pos + count > inode->i_size) {
		count = inode->i_size - pos;
	}
*/
	error = 0;
	if (!count)	/* size_t is never < 0 */
		goto out;

	error = ncp_make_open(inode, O_RDONLY);
	if (error) {
		DPRINTK(KERN_ERR "ncp_file_read: open failed, error=%d\n", error);
		goto out;
	}

	bufsize = NCP_SERVER(inode)->buffer_size;

	error = -EIO;
	freelen = ncp_read_bounce_size(bufsize);
	freepage = kmalloc(freelen, GFP_NFS);
	if (!freepage)
		goto out;
	error = 0;
	/* First read in as much as possible for each bufsize. */
	while (already_read < count) {
		int read_this_time;
		size_t to_read = min(bufsize - (pos % bufsize),
				  count - already_read);

		error = ncp_read_bounce(NCP_SERVER(inode),
			 	NCP_FINFO(inode)->file_handle,
				pos, to_read, buf, &read_this_time, 
				freepage, freelen);
		if (error) {
			kfree(freepage);
			error = -EIO;	/* This is not exact, i know.. */
			goto out;
		}
		pos += read_this_time;
		buf += read_this_time;
		already_read += read_this_time;

		if (read_this_time != to_read) {
			break;
		}
	}
	kfree(freepage);

	*ppos = pos;

	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
	}
	
	DPRINTK("ncp_file_read: exit %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
out:
	return already_read ? already_read : error;
}

static ssize_t
ncp_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	size_t already_written = 0;
	off_t pos;
	size_t bufsize;
	int errno;
	void* bouncebuffer;

	DPRINTK("ncp_file_write: enter %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	if (inode == NULL) {
		DPRINTK("ncp_file_write: inode = NULL\n");
		return -EINVAL;
	}
	errno = -EIO;
	if (!ncp_conn_valid(NCP_SERVER(inode)))
		goto out;
	if (!S_ISREG(inode->i_mode)) {
		DPRINTK("ncp_file_write: write to non-file, mode %07o\n",
			inode->i_mode);
		return -EINVAL;
	}

	errno = 0;
	if (!count)
		goto out;
	errno = ncp_make_open(inode, O_WRONLY);
	if (errno) {
		DPRINTK(KERN_ERR "ncp_file_write: open failed, error=%d\n", errno);
		return errno;
	}
	pos = *ppos;

	if (file->f_flags & O_APPEND) {
		pos = inode->i_size;
	}
	bufsize = NCP_SERVER(inode)->buffer_size;

	already_written = 0;

	bouncebuffer = kmalloc(bufsize, GFP_NFS);
	if (!bouncebuffer)
		return -EIO;	/* -ENOMEM */
	while (already_written < count) {
		int written_this_time;
		size_t to_write = min(bufsize - (pos % bufsize),
				   count - already_written);

		if (copy_from_user(bouncebuffer, buf, to_write)) {
			errno = -EFAULT;
			break;
		}
		if (ncp_write_kernel(NCP_SERVER(inode), 
		    NCP_FINFO(inode)->file_handle,
		    pos, to_write, buf, &written_this_time) != 0) {
			errno = -EIO;
			break;
		}
		pos += written_this_time;
		buf += written_this_time;
		already_written += written_this_time;

		if (written_this_time != to_write) {
			break;
		}
	}
	kfree(bouncebuffer);
	inode->i_mtime = inode->i_atime = CURRENT_TIME;
	
	*ppos = pos;

	if (pos > inode->i_size) {
		inode->i_size = pos;
	}
	DPRINTK("ncp_file_write: exit %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
out:
	return already_written ? already_written : errno;
}

struct file_operations ncp_file_operations =
{
	read:		ncp_file_read,
	write:		ncp_file_write,
	ioctl:		ncp_ioctl,
	mmap:		ncp_mmap,
	fsync:		ncp_fsync,
};

struct inode_operations ncp_file_inode_operations =
{
	setattr:	ncp_notify_change,
};
