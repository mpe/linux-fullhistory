/*
 *	fs/msdos/mmap.c
 *
 *	Written by Jacques Gelinas (jacques@solucorp.qc.ca)
 *	Inspired by fs/nfs/mmap.c (Jaon Tombs 15 Aug 1993)
 *
 *	msdos mmap handling
 */
#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/msdos_fs.h>

/*
 * Fill in the supplied page for mmap
 */
static unsigned long msdos_file_mmap_nopage(
	struct vm_area_struct * area,
	unsigned long address,
	unsigned long page,
	int error_code)
{
	struct inode * inode = area->vm_inode;
	unsigned int clear;
	int pos;
	long gap;	/* distance from eof to pos */

	address &= PAGE_MASK;
	pos = address - area->vm_start + area->vm_offset;

	clear = 0;
	gap = inode->i_size - pos;
	if (gap <= 0){
		/* mmaping beyond end of file */
		clear = PAGE_SIZE;
	}else{
		int cur_read;
		int need_read;
		struct file filp;
		if (gap < PAGE_SIZE){
			clear = PAGE_SIZE - gap;
		}
		filp.f_reada = 0;
		filp.f_pos = pos;
		need_read = PAGE_SIZE - clear;
		{
			unsigned long cur_fs = get_fs();
			set_fs (KERNEL_DS);
			cur_read = msdos_file_read (inode,&filp,(char*)page
				,need_read);
			set_fs (cur_fs);
		}
		if (cur_read != need_read){
			printk ("MSDOS: Error while reading an mmap file %d <> %d\n"
				,cur_read,need_read);
		}
	}
	if (clear > 0){
		memset ((char*)page+PAGE_SIZE-clear,0,clear);
	}
	return page;
}

struct vm_operations_struct msdos_file_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	msdos_file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* swapout */
	NULL,			/* swapin */
};

/*
 * This is used for a general mmap of an msdos file
 * Returns 0 if ok, or a negative error code if not.
 */
int msdos_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
	if (vma->vm_flags & VM_SHARED)	/* only PAGE_COW or read-only supported now */
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
	vma->vm_ops = &msdos_file_mmap;
	return 0;
}


