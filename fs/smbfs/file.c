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

static inline int
min(int a, int b)
{
	return a < b ? a : b;
}

static int
smb_fsync(struct file *file, struct dentry * dentry)
{
	printk("smb_fsync: sync file %s/%s\n", 
		dentry->d_parent->d_name.name, dentry->d_name.name);
	return 0;
}

/*
 * Read a page synchronously.
 */
static int
smb_readpage_sync(struct inode *inode, struct page *page)
{
	unsigned long offset = page->offset;
	char *buffer = (char *) page_address(page);
	struct dentry * dentry = inode->u.smbfs_i.dentry;
	int rsize = SMB_SERVER(inode)->opt.max_xmit - (SMB_HEADER_LEN+15);
	int result, refresh = 0;
	int count = PAGE_SIZE;

	pr_debug("SMB: smb_readpage_sync(%p)\n", page);
	clear_bit(PG_error, &page->flags);

	result = -EIO;
	if (!dentry) {
		printk("smb_readpage_sync: no dentry for inode %ld\n",
			inode->i_ino);
		goto io_error;
	}
 
	result = smb_open(dentry, O_RDONLY);
	if (result < 0)
		goto io_error;
	/* Should revalidate inode ... */

	do {
		if (count < rsize)
			rsize = count;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_readpage: reading %s/%s, offset=%ld, buffer=%p, size=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, offset, buffer, rsize);
#endif
		result = smb_proc_read(inode, offset, rsize, buffer);
		if (result < 0)
			goto io_error;

		refresh = 1;
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
	if (refresh)
		smb_refresh_inode(inode);
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
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
	__free_page(page);
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
	int wsize = SMB_SERVER(inode)->opt.max_xmit - (SMB_HEADER_LEN+15);
	int result, refresh = 0, written = 0;
	u8 *buffer;

	pr_debug("SMB:      smb_writepage_sync(%x/%ld %d@%ld)\n",
		 inode->i_dev, inode->i_ino,
		 count, page->offset + offset);

	buffer = (u8 *) page_address(page) + offset;
	offset += page->offset;

	do {
		if (count < wsize)
			wsize = count;

		result = smb_proc_write(inode, offset, wsize, buffer);

		if (result < 0) {
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
	if (refresh)
		smb_refresh_inode(inode);

	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
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
	__free_page(page);
	return result;
}

static int
smb_updatepage(struct inode *inode, struct page *page, const char *buffer,
	       unsigned long offset, unsigned int count, int sync)
{
	u8		*page_addr;
	int 	result;

	pr_debug("SMB:      smb_updatepage(%x/%ld %d@%ld, sync=%d)\n",
		 inode->i_dev, inode->i_ino,
		 count, page->offset+offset, sync);

#ifdef SMBFS_PARANOIA
	if (test_bit(PG_locked, &page->flags))
		printk("smb_updatepage: page already locked!\n");
#endif
	set_bit(PG_locked, &page->flags);
	atomic_inc(&page->count);

	page_addr = (u8 *) page_address(page);

	if (copy_from_user(page_addr + offset, buffer, count))
		goto bad_fault;
	result = smb_writepage_sync(inode, page, offset, count);
out:
	__free_page(page);
	return result;

bad_fault:
	printk("smb_updatepage: fault at page=%p buffer=%p\n", page, buffer);
	result = -EFAULT;
	clear_bit(PG_uptodate, &page->flags);
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
	goto out;
}

static long
smb_file_read(struct inode * inode, struct file * file,
	      char * buf, unsigned long count)
{
	int	status;

	pr_debug("SMB: read(%x/%ld (%d), %lu@%lu)\n",
		 inode->i_dev, inode->i_ino, inode->i_count,
		 count, (unsigned long) file->f_pos);

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

	pr_debug("SMB: write(%x/%ld (%d), %lu@%lu)\n",
		 inode->i_dev, inode->i_ino, inode->i_count,
		 count, (unsigned long) file->f_pos);

	result = -EINVAL;
	if (!inode) {
		printk("smb_file_write: inode = NULL\n");
		goto out;
	}

	result = smb_revalidate_inode(inode);
	if (result < 0)
		goto out;

	result = smb_open(file->f_dentry, O_WRONLY);
	if (result < 0)
		goto out;

	result = -EINVAL;
	if (!S_ISREG(inode->i_mode)) {
		printk("smb_file_write: write to non-file, mode %07o\n",
			inode->i_mode);
		goto out;
	}

	result = 0;
	if (count > 0)
	{
		result = generic_file_write(inode, file, buf, count);
	}
out:
	return result;
}

static struct file_operations smb_file_operations =
{
	NULL,			/* lseek - default */
	smb_file_read,		/* read */
	smb_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl */
	smb_file_mmap,		/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	smb_fsync,		/* fsync */
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
