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

static inline int
min(int a, int b)
{
	return a < b ? a : b;
}

static int
smb_fsync(struct inode *inode, struct file *file)
{
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
	int rsize = SMB_SERVER(inode)->opt.max_xmit - (SMB_HEADER_LEN+15);
	int result, refresh = 0;
	int count = PAGE_SIZE;

	pr_debug("SMB: smb_readpage_sync(%p)\n", page);
	clear_bit(PG_error, &page->flags);

	result = smb_open(inode, O_RDONLY);
	if (result < 0)
		goto io_error;

	do {
		if (count < rsize)
			rsize = count;

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
	unsigned long	address;
	int		error = -1;

	pr_debug("SMB: smb_readpage %08lx\n", page_address(page));
	set_bit(PG_locked, &page->flags);
	address = page_address(page);
	atomic_inc(&page->count);
	error = smb_readpage_sync(inode, page);
	free_page(address);
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
	return written ? written : result;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 */
static int
smb_writepage(struct inode *inode, struct page *page)
{
	return smb_writepage_sync(inode, page, 0, PAGE_SIZE);
}

static int
smb_updatepage(struct inode *inode, struct page *page, const char *buffer,
	       unsigned long offset, unsigned int count, int sync)
{
	u8		*page_addr;

	pr_debug("SMB:      smb_updatepage(%x/%ld %d@%ld, sync=%d)\n",
		 inode->i_dev, inode->i_ino,
		 count, page->offset+offset, sync);

	page_addr = (u8 *) page_address(page);

	copy_from_user(page_addr + offset, buffer, count);
	return smb_writepage_sync(inode, page, offset, count);
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
	if (status < 0)
		return status;

	return generic_file_read(inode, file, buf, count);
}

static int
smb_file_mmap(struct inode * inode, struct file * file,
				struct vm_area_struct * vma)
{
	int	status;

	status = smb_revalidate_inode(inode);
	if (status < 0)
		return status;

	return generic_file_mmap(inode, file, vma);
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

	if (!inode) {
		printk("smb_file_write: inode = NULL\n");
		return -EINVAL;
	}

	result = smb_revalidate_inode(inode);
	if (result < 0)
		return result;

	result = smb_open(inode, O_WRONLY);
	if (result < 0)
		return result;

	if (!S_ISREG(inode->i_mode)) {
		printk("smb_file_write: write to non-file, mode %07o\n",
			inode->i_mode);
		return -EINVAL;
	}
	if (count <= 0)
		return 0;

	return generic_file_write(inode, file, buf, count);
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
