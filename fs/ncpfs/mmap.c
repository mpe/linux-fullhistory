/*
 *  mmap.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/ncp_fs.h>

#include "ncplib_kernel.h"
#include <asm/uaccess.h>
#include <asm/system.h>

static inline int min(int a, int b)
{
	return a < b ? a : b;
}

/*
 * Fill in the supplied page for mmap
 */
static unsigned long ncp_file_mmap_nopage(struct vm_area_struct *area,
				     unsigned long address, int no_share)
{
	struct file *file = area->vm_file;
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	unsigned long page;
	unsigned int already_read;
	unsigned int count;
	int bufsize;
	int pos;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return page;
	address &= PAGE_MASK;
	pos = address - area->vm_start + area->vm_offset;

	count = PAGE_SIZE;
	if (address + PAGE_SIZE > area->vm_end) {
		count = area->vm_end - address;
	}
	/* what we can read in one go */
	bufsize = NCP_SERVER(inode)->buffer_size;

	already_read = 0;
	if (ncp_make_open(inode, O_RDONLY) >= 0) {
		while (already_read < count) {
			int read_this_time;
			int to_read;

			to_read = bufsize - (pos % bufsize);

			to_read = min(to_read, count - already_read);

			if (ncp_read_kernel(NCP_SERVER(inode),
				     NCP_FINFO(inode)->file_handle,
				     pos, to_read,
				     (char *) (page + already_read),
				     &read_this_time) != 0) {
				read_this_time = 0;
			}
			pos += read_this_time;
			already_read += read_this_time;

			if (read_this_time < to_read) {
				break;
			}
		}

	}

	if (already_read < PAGE_SIZE)
		memset((char*)(page + already_read), 0, 
		       PAGE_SIZE - already_read);
	return page;
}

struct vm_operations_struct ncp_file_mmap =
{
	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	ncp_file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* swapout */
	NULL,			/* swapin */
};


/* This is used for a general mmap of a ncp file */
int ncp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_dentry->d_inode;
	
	DPRINTK(KERN_DEBUG "ncp_mmap: called\n");

	if (!ncp_conn_valid(NCP_SERVER(inode))) {
		return -EIO;
	}
	/* only PAGE_COW or read-only supported now */
	if (vma->vm_flags & VM_SHARED)
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
	}

	vma->vm_ops = &ncp_file_mmap;
	return 0;
}
