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
#include <linux/smb_fs.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */
/* #define pr_debug printk */

extern int smb_get_rsize(struct smb_sb_info *);
extern int smb_get_wsize(struct smb_sb_info *);

static inline int
min(int a, int b)
{
	return a < b ? a : b;
}

static inline void
smb_unlock_page(struct page *page)
{
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
}

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
smb_readpage_sync(struct inode *inode, struct page *page)
{
	char *buffer = (char *) page_address(page);
	unsigned long offset = page->offset;
	struct dentry * dentry = inode->u.smbfs_i.dentry;
	int rsize = smb_get_rsize(SMB_SERVER(inode));
	int count = PAGE_SIZE;
	int result;

	clear_bit(PG_error, &page->flags);

	result = -EIO;
	if (!dentry) {
		printk("smb_readpage_sync: no dentry for inode %ld\n",
			inode->i_ino);
		goto io_error;
	}
 
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_readpage_sync: file %s/%s, count=%d@%ld, rsize=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, count, offset, rsize);
#endif
	result = smb_open(dentry, O_RDONLY);
	if (result < 0)
		goto io_error;

	do {
		if (count < rsize)
			rsize = count;

		result = smb_proc_read(inode, offset, rsize, buffer);
		if (result < 0)
			goto io_error;

		count -= result;
		offset += result;
		buffer += result;
		if (result < rsize)
			break;
	} while (count);

	memset(buffer, 0, count);
	set_bit(PG_uptodate, &page->flags);
	result = 0;

io_error:
	smb_unlock_page(page);
	return result;
}

int
smb_readpage(struct inode *inode, struct page *page)
{
	int		error;

	pr_debug("SMB: smb_readpage %08lx\n", page_address(page));
#ifdef SMBFS_PARANOIA
	if (test_bit(PG_locked, &page->flags))
		printk("smb_readpage: page already locked!\n");
#endif
	set_bit(PG_locked, &page->flags);
	atomic_inc(&page->count);
	error = smb_readpage_sync(inode, page);
	free_page(page_address(page));
	return error;
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
smb_writepage_sync(struct inode *inode, struct page *page,
		   unsigned long offset, unsigned int count)
{
	u8 *buffer = (u8 *) page_address(page) + offset;
	int wsize = smb_get_wsize(SMB_SERVER(inode));
	int result, refresh = 0, written = 0;

	offset += page->offset;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_writepage_sync: file %s/%s, count=%d@%ld, wsize=%d\n",
((struct dentry *) inode->u.smbfs_i.dentry)->d_parent->d_name.name, 
((struct dentry *) inode->u.smbfs_i.dentry)->d_name.name, count, offset, wsize);
#endif

	do {
		if (count < wsize)
			wsize = count;

		result = smb_proc_write(inode, offset, wsize, buffer);
		if (result < 0)
		{
			/* Must mark the page invalid after I/O error */
			clear_bit(PG_uptodate, &page->flags);
			goto io_error;
		}
		/* N.B. what if result < wsize?? */
#ifdef SMBFS_PARANOIA
if (result < wsize)
printk("smb_writepage_sync: short write, wsize=%d, result=%d\n", wsize, result);
#endif
		refresh = 1;
		buffer += wsize;
		offset += wsize;
		written += wsize;
		count -= wsize;
	} while (count);

io_error:
#if 0
	if (refresh)
		smb_refresh_inode(inode);
#endif
	smb_unlock_page(page);
	return written ? written : result;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 */
static int
smb_writepage(struct inode *inode, struct page *page)
{
	int 	result;

#ifdef SMBFS_PARANOIA
	if (test_bit(PG_locked, &page->flags))
		printk("smb_writepage: page already locked!\n");
#endif
	set_bit(PG_locked, &page->flags);
	atomic_inc(&page->count);
	result = smb_writepage_sync(inode, page, 0, PAGE_SIZE);
	free_page(page_address(page));
	return result;
}

static int
smb_updatepage(struct inode *inode, struct page *page, const char *buffer,
	       unsigned long offset, unsigned int count, int sync)
{
	unsigned long page_addr = page_address(page);
	int result;

	pr_debug("SMB:      smb_updatepage(%x/%ld %d@%ld, sync=%d)\n",
		 inode->i_dev, inode->i_ino,
		 count, page->offset+offset, sync);

#ifdef SMBFS_PARANOIA
	if (test_bit(PG_locked, &page->flags))
		printk("smb_updatepage: page already locked!\n");
#endif
	set_bit(PG_locked, &page->flags);
	atomic_inc(&page->count);

	if (copy_from_user((char *) page_addr + offset, buffer, count))
		goto bad_fault;
	result = smb_writepage_sync(inode, page, offset, count);
out:
	free_page(page_addr);
	return result;

bad_fault:
#ifdef SMBFS_PARANOIA
printk("smb_updatepage: fault at addr=%lu, offset=%lu, buffer=%p\n", 
page_addr, offset, buffer);
#endif
	result = -EFAULT;
	clear_bit(PG_uptodate, &page->flags);
	smb_unlock_page(page);
	goto out;
}

static long
smb_file_read(struct inode * inode, struct file * file,
	      char * buf, unsigned long count)
{
	int	status;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_read: file %s/%s, count=%lu@%lu\n",
file->f_dentry->d_parent->d_name.name, file->f_dentry->d_name.name,
count, (unsigned long) file->f_pos);
#endif

	status = smb_revalidate_inode(inode);
	if (status >= 0)
	{
		status = generic_file_read(inode, file, buf, count);
	}
	return status;
}

static int
smb_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry * dentry = file->f_dentry;
	struct inode * inode = dentry->d_inode;
	int	status;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_mmap: file %s/%s, address %lu - %lu\n",
file->f_dentry->d_parent->d_name.name, file->f_dentry->d_name.name,
vma->vm_start, vma->vm_end);
#endif
	status = smb_revalidate_inode(inode);
	if (status >= 0)
	{
		status = generic_file_mmap(file, vma);
	}
	return status;
}

/* 
 * Write to a file (through the page cache).
 */
static long
smb_file_write(struct inode *inode, struct file *file,
	       const char *buf, unsigned long count)
{
	int	result;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_write: file %s/%s, count=%lu@%lu\n",
file->f_dentry->d_parent->d_name.name, file->f_dentry->d_name.name,
count, (unsigned long) file->f_pos);
#endif

#ifdef SMBFS_PARANOIA
	/* Should be impossible now that inodes can't change mode */
	result = -EINVAL;
	if (!S_ISREG(inode->i_mode))
	{
		printk("smb_file_write: write to non-file, mode %07o\n",
			inode->i_mode);
		goto out;
	}
#endif

	result = smb_revalidate_inode(inode);
	if (result)
		goto out;

	result = smb_open(file->f_dentry, O_WRONLY);
	if (result)
		goto out;

	if (count > 0)
	{
		result = generic_file_write(inode, file, buf, count);
		if (result > 0)
			smb_refresh_inode(inode);
	}
out:
	return result;
}

static int
smb_file_open(struct inode *inode, struct file * file)
{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_open: inode=%p, file=%p\n", inode, file);
#endif
	return 0;
}

static int
smb_file_release(struct inode *inode, struct file * file)
{
	struct dentry * dentry = file->f_dentry;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_file_release: closing file %s/%s, d_count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count);
#endif
	
	if (dentry->d_count == 1)
	{
		smb_close(inode);
	}
	return 0;
}

static struct file_operations smb_file_operations =
{
	NULL,			/* lseek - default */
	smb_file_read,		/* read */
	smb_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl */
	smb_file_mmap,		/* mmap(struct file*, struct vm_area_struct*) */
	smb_file_open,		/* open(struct inode*, struct file*) */
	smb_file_release,	/* release(struct inode*, struct file*) */
	smb_fsync,		/* fsync(struct file*, struct dentry*) */
	NULL,			/* fasync(struct file*, int) */
	NULL,			/* check_media_change(kdev_t dev) */
	NULL,			/* revalidate(kdev_t dev) */
	NULL			/* lock(struct file*, int, struct file_lock*) */
};

struct inode_operations smb_file_inode_operations =
{
	&smb_file_operations,	/* default file operations */
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
	smb_readpage,		/* readpage */
	smb_writepage,		/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	smb_updatepage,		/* updatepage */
	smb_revalidate_inode,	/* revalidate */
};
