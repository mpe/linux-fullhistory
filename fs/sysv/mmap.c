/*
 *  linux/fs/sysv/mmap.c
 *
 *  mm/memory.c, mm/mmap.c
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 *
 *  nfs/mmap.c
 *  Copyright (C) 1993  Jon Tombs
 *
 *  fs/msdos/mmap.c
 *  Copyright (C) 1994  Jacques Gelinas
 *
 *  fs/sysv/mmap.c
 *  Copyright (C) 1994  Bruno Haible
 *
 *  SystemV/Coherent mmap handling
 */

#include <asm/segment.h>

#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/malloc.h>

/*
 * Fill in the supplied page for mmap
 */
static unsigned long sysv_file_mmap_nopage (struct vm_area_struct * area,
	unsigned long address, unsigned long page, int no_share)
{
	int remaining, count, old_fs;
	struct file filp;

	address &= PAGE_MASK;
	/* prepare a file pointer */
	filp.f_pos = address - area->vm_start + area->vm_offset;
	filp.f_reada = 0;
	remaining = area->vm_end - address;
	if (remaining > PAGE_SIZE)
		remaining = PAGE_SIZE;
	/* read from the file. page is in kernel space, not user space. */
	old_fs = get_fs(); set_fs(get_ds());
	count = sysv_file_read (area->vm_inode, &filp, (char *)page, remaining);
	set_fs(old_fs);
	if (count < 0)
		count = 0;	/* do nothing on I/O error ?? */
	else
		remaining -= count;
	if (remaining > 0)
		memset((char *)page + count, 0, remaining);
	return page;
}

static struct vm_operations_struct sysv_file_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	sysv_file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* share */
	NULL,			/* unmap */
};

int sysv_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
	if (vma->vm_page_prot & PAGE_RW)	/* only PAGE_COW or read-only supported right now */
		return -EINVAL;
	if (vma->vm_offset & (inode->i_sb->s_blocksize - 1))
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}

	vma->vm_inode = inode;
	inode->i_count++;
	vma->vm_ops = &sysv_file_mmap;
	return 0;
}
