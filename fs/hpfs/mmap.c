/*
 *  linux/fs/hpfs/mmap.c
 *
 *  taken from fat filesystem
 *
 *  Written by Jacques Gelinas (jacques@solucorp.qc.ca)
 *  Inspired by fs/nfs/mmap.c (Jon Tombs 15 Aug 1993)
 *
 *  Modified for HPFS by Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz)
 *
 *  mmap handling for hpfs filesystem
 *	(generic_file_mmap may be used only on filesystems that keep zeros
 *	 in last file sector beyond end)
 */

/*
 * generic_file_mmap doesn't erase the space beyond file end in last sector. :-(
 * Hpfs doesn't keep zeros in last sector. This causes problems with kernel
 * mkdep.c and probably other programs. Additionally this could be a security
 * hole - some interesting data, like pieces of /etc/shadow could be found
 * beyond file end.
 *
 * So, I can't use generic mmap. mmap from fat filesystem looks good, so I used
 * it.
 *
 * BTW. fat uses generic mmap on normal disks. Doesn't it also have above bugs?
 * I don't think Msdos erases space in last sector.
 *
 * If you fix generic_file_mmap, you can remove this file and use it.
 */

#include "hpfs_fn.h"

/*
 * Fill in the supplied page for mmap
 */

static unsigned long hpfs_file_mmap_nopage(
	struct vm_area_struct * area,
	unsigned long address,
	int error_code)
{
	/*struct inode * inode = area->vm_inode;*/
	struct inode * inode = area->vm_file->f_dentry->d_inode;
	unsigned long page;
	unsigned int clear;
	loff_t pos;
	long gap;	/* distance from eof to pos */

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return page;
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
		/*struct file *filp = area->vm_file;*/
		struct file filp;
		if (gap < PAGE_SIZE){
			clear = PAGE_SIZE - gap;
		}
		filp.f_reada = 0;
		filp.f_pos = pos;
		filp.f_dentry=area->vm_file->f_dentry;
		need_read = PAGE_SIZE - clear;
		{
			mm_segment_t cur_fs = get_fs();
			set_fs (KERNEL_DS);
			cur_read = generic_file_read (&filp,(char*)page
				,need_read,&pos);
			set_fs (cur_fs);
		}
		if (cur_read != need_read){
			hpfs_error(inode->i_sb, "Error while reading an mmap file %08x", inode->i_ino);
		}
	}
	if (clear > 0){
		memset ((char*)page+PAGE_SIZE-clear,0,clear);
	}
	return page;
}

struct vm_operations_struct hpfs_file_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	hpfs_file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* swapout */
	NULL,			/* swapin */
};

/*
 * This is used for a general mmap of an msdos file
 * Returns 0 if ok, or a negative error code if not.
 */
int hpfs_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct inode *inode = file->f_dentry->d_inode;
	/*printk("start mmap\n");*/
	if (vma->vm_flags & VM_SHARED)	/* only PAGE_COW or read-only supported now */
		return -EINVAL;
	if (vma->vm_offset & (inode->i_sb->s_blocksize - 1))
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	/*if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}*/

	vma->vm_file = file;
	/*inode->i_count++;*/
	file->f_count++;
	vma->vm_ops = &hpfs_file_mmap;
	/*printk("end mmap\n");*/
	return 0;
}
