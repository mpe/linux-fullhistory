/*
 *  file.c
 *
 *  Copyright (C) 1995, 1996, 1997 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/smbno.h>
#include <linux/smb_fs.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */
/* #define pr_debug printk */

static int
smb_fsync(struct file *file, struct dentry * dentry)
{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_fsync: sync file %s/%s\n", 
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	return 0;
}

/*
 * Read a page synchronously.
 */
static int
smb_readpage_sync(struct dentry *dentry, struct page *page)
{
	char *buffer = (char *) page_address(page);
	unsigned long offset = page->index << PAGE_CACHE_SHIFT;
	int rsize = smb_get_rsize(server_from_dentry(dentry));
	int count = PAGE_SIZE;
	int result;

	/* We can't replace this with ClearPageError. why? is it a problem? 
	   fs/buffer.c:brw_page does the same. */
	/* ClearPageError(page); */

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_readpage_sync: file %s/%s, count=%d@%ld, rsize=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, count, offset, rsize);
#endif
	result = smb_open(dentry, SMB_O_RDONLY);
	if (result < 0)
	{
#ifdef SMBFS_PARANOIA
printk("smb_readpage_sync: %s/%s open failed, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, result);
#endif
		goto io_error;
	}

	do {
		if (count < rsize)
			rsize = count;

		result = smb_proc_read(dentry, offset, rsize, buffer);
		if (result < 0)
			goto io_error;

		count -= result;
		offset += result;
		buffer += result;
		dentry->d_inode->i_atime = CURRENT_TIME;
		if (result < rsize)
			break;
	} while (count);

	memset(buffer, 0, count);
	SetPageUptodate(page);
	result = 0;

io_error:
	UnlockPage(page);
	return result;
}

static int
smb_readpage(struct file *file, struct page *page)
{
	int		error;
	struct dentry  *dentry = file->f_dentry;

	pr_debug("SMB: smb_readpage %08lx\n", page_address(page));
#ifdef SMBFS_PARANOIA
	if (!PageLocked(page))
		printk("smb_readpage: page not already locked!\n");
#endif

	get_page(page);
	error = smb_readpage_sync(dentry, page);
	put_page(page);
	return error;
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
smb_writepage_sync(struct dentry *dentry, struct page *page,
		   unsigned long offset, unsigned int count)
{
	struct inode *inode = dentry->d_inode;
	u8 *buffer = (u8 *) page_address(page) + offset;
	int wsize = smb_get_wsize(server_from_dentry(dentry));
	int result, written = 0;

	offset += page->index << PAGE_CACHE_SHIFT;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_writepage_sync: file %s/%s, count=%d@%ld, wsize=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, count, offset, wsize);
#endif

	do {
		if (count < wsize)
			wsize = count;

		result = smb_proc_write(dentry, offset, wsize, buffer);
		if (result < 0)
			break;
		/* N.B. what if result < wsize?? */
#ifdef SMBFS_PARANOIA
if (result < wsize)
printk("smb_writepage_sync: short write, wsize=%d, result=%d\n", wsize, result);
#endif
		buffer += wsize;
		offset += wsize;
		written += wsize;
		count -= wsize;
		/*
		 * Update the inode now rather than waiting for a refresh.
		 */
		inode->i_mtime = inode->i_atime = CURRENT_TIME;
		if (offset > inode->i_size)
			inode->i_size = offset;
		inode->u.smbfs_i.cache_valid |= SMB_F_LOCALWRITE;
	} while (count);
	return written ? written : result;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 *
 * We are called with the page locked and the caller unlocks.
 */
static int
smb_writepage(struct file *file, struct page *page)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	unsigned offset = PAGE_CACHE_SIZE;
	int err;

	/* easy case */
	if (page->index < end_index)
		goto do_it;
	/* things got complicated... */
	offset = inode->i_size & (PAGE_CACHE_SIZE-1);
	/* OK, are we completely out? */
	if (page->index >= end_index+1 || !offset)
		return -EIO;
do_it:
	get_page(page);
	err = smb_writepage_sync(dentry, page, 0, offset);
	SetPageUptodate(page);
	put_page(page);
	return err;
}

static int
smb_updatepage(struct file *file, struct page *page, unsigned long offset, unsigned int count)
{
	struct dentry *dentry = file->f_dentry;

	pr_debug("SMBFS: smb_updatepage(%s/%s %d@%ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
	 	count, (page->index << PAGE_CACHE_SHIFT)+offset);

	return smb_writepage_sync(dentry, page, offset, count);
}

static ssize_t
smb_file_read(struct file * file, char * buf, size_t count, loff_t *ppos)
{
	struct dentry * dentry = file->f_dentry;
	ssize_t	status;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_read: file %s/%s, count=%lu@%lu\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
(unsigned long) count, (unsigned long) *ppos);
#endif

	status = smb_revalidate_inode(dentry);
	if (status)
	{
#ifdef SMBFS_PARANOIA
printk("smb_file_read: %s/%s validation failed, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, status);
#endif
		goto out;
	}

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_read: before read, size=%ld, pages=%ld, flags=%x, atime=%ld\n",
dentry->d_inode->i_size, dentry->d_inode->i_nrpages, dentry->d_inode->i_flags,
dentry->d_inode->i_atime);
#endif
	status = generic_file_read(file, buf, count, ppos);
out:
	return status;
}

static int
smb_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry * dentry = file->f_dentry;
	int	status;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_mmap: file %s/%s, address %lu - %lu\n",
dentry->d_parent->d_name.name, dentry->d_name.name, vma->vm_start, vma->vm_end);
#endif

	status = smb_revalidate_inode(dentry);
	if (status)
	{
#ifdef SMBFS_PARANOIA
printk("smb_file_mmap: %s/%s validation failed, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, status);
#endif
		goto out;
	}
	status = generic_file_mmap(file, vma);
out:
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
static int smb_prepare_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	kmap(page);
	return 0;
}

static int smb_commit_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	int status;

	status = -EFAULT;
	lock_kernel();
	status = smb_updatepage(file, page, offset, to-offset);
	unlock_kernel();
	kunmap(page);
	return status;
}

struct address_space_operations smb_file_aops = {
	readpage: smb_readpage,
	writepage: smb_writepage,
	prepare_write: smb_prepare_write,
	commit_write: smb_commit_write
};

/* 
 * Write to a file (through the page cache).
 */
static ssize_t
smb_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct dentry * dentry = file->f_dentry;
	ssize_t	result;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_write: file %s/%s, count=%lu@%lu, pages=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
(unsigned long) count, (unsigned long) *ppos, dentry->d_inode->i_nrpages);
#endif

	result = smb_revalidate_inode(dentry);
	if (result)
	{
#ifdef SMBFS_PARANOIA
printk("smb_file_write: %s/%s validation failed, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, result);
#endif
			goto out;
	}

	result = smb_open(dentry, SMB_O_WRONLY);
	if (result)
		goto out;

	if (count > 0)
	{
		result = generic_file_write(file, buf, count, ppos);
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_write: pos=%ld, size=%ld, mtime=%ld, atime=%ld\n",
(long) file->f_pos, dentry->d_inode->i_size, dentry->d_inode->i_mtime,
dentry->d_inode->i_atime);
#endif
	}
out:
	return result;
}

static int
smb_file_open(struct inode *inode, struct file * file)
{
	inode->u.smbfs_i.openers++;
	return 0;
}

static int
smb_file_release(struct inode *inode, struct file * file)
{
	if (!--inode->u.smbfs_i.openers)
		smb_close(inode);
	return 0;
}

/*
 * Check whether the required access is compatible with
 * an inode's permission. SMB doesn't recognize superuser
 * privileges, so we need our own check for this.
 */
static int
smb_file_permission(struct inode *inode, int mask)
{
	int mode = inode->i_mode;
	int error = 0;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_permission: mode=%x, mask=%x\n", mode, mask);
#endif
	/* Look at user permissions */
	mode >>= 6;
	if ((mode & 7 & mask) != mask)
		error = -EACCES;
	return error;
}

struct file_operations smb_file_operations =
{
	read:		smb_file_read,
	write:		smb_file_write,
	ioctl:		smb_ioctl,
	mmap:		smb_file_mmap,
	open:		smb_file_open,
	release:	smb_file_release,
	fsync:		smb_fsync,
};

struct inode_operations smb_file_inode_operations =
{
	permission:	smb_file_permission,
	revalidate:	smb_revalidate_inode,
	setattr:	smb_notify_change,
};
