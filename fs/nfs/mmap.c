/*
 *	fs/nfs/mmap.c	by Jon Tombs 15 Aug 1993
 *
 * This code is from
 *	linux/mm/mmap.c which was written by obz, Linus and Eric
 * and
 *	linux/mm/memory.c  by Linus Torvalds and others
 *
 *	Copyright (C) 1993
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
#include <linux/nfs_fs.h>

#include <asm/segment.h>
#include <asm/system.h>

/*
 * Fill in the supplied page for mmap
 */
static unsigned long nfs_file_mmap_nopage(struct vm_area_struct * area,
	unsigned long address, unsigned long page, int no_share)
{
	struct inode * inode = area->vm_inode;
	unsigned int clear;
	unsigned long tmp;
	int n;
	int i;
	int pos;
	struct nfs_fattr fattr;

	address &= PAGE_MASK;
	pos = address - area->vm_start + area->vm_offset;

	clear = 0;
	if (address + PAGE_SIZE > area->vm_end) {
		clear = address + PAGE_SIZE - area->vm_end;
	}

	n = NFS_SERVER(inode)->rsize; /* what we can read in one go */

	for (i = 0; i < (PAGE_SIZE - clear); i += n) {
		int hunk, result;

		hunk = PAGE_SIZE - i;
		if (hunk > n)
			hunk = n;
		result = nfs_proc_read(NFS_SERVER(inode), NFS_FH(inode),
			pos, hunk, (char *) (page + i), &fattr);
		if (result < 0)
			break;
		pos += result;
		if (result < n) {
			i += result;
			break;
		}
	}

#ifdef doweneedthishere
	nfs_refresh_inode(inode, &fattr);
#endif

	tmp = page + PAGE_SIZE;
	while (clear--) {
		*(char *)--tmp = 0;
	}
	return page;
}
struct vm_operations_struct nfs_file_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	nfs_file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* share */
	NULL,			/* unmap */
};


/* This is used for a general mmap of a nfs file */
int nfs_mmap(struct inode * inode, struct file * file,
	unsigned long addr, size_t len, int prot, unsigned long off)
{
	struct vm_area_struct * mpnt;

	if (prot & PAGE_RW)	/* only PAGE_COW or read-only supported now */
		return -EINVAL;
	if (off & (inode->i_sb->s_blocksize - 1))
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}

	mpnt = (struct vm_area_struct * ) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!mpnt)
		return -ENOMEM;

	unmap_page_range(addr, len);
	mpnt->vm_task = current;
	mpnt->vm_start = addr;
	mpnt->vm_end = addr + len;
	mpnt->vm_page_prot = prot;
	mpnt->vm_flags = 0;
	mpnt->vm_share = NULL;
	mpnt->vm_inode = inode;
	inode->i_count++;
	mpnt->vm_offset = off;
	mpnt->vm_ops = &nfs_file_mmap;
	insert_vm_struct(current, mpnt);
	merge_segments(current->mm->mmap, NULL, NULL);
	return 0;
}
