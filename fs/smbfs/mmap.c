/*
 *  mmap.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
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
#include <linux/smb_fs.h>
#include <linux/fcntl.h>

#include <asm/segment.h>
#include <asm/system.h>

/*
 * Fill in the supplied page for mmap
 */
static unsigned long 
smb_file_mmap_nopage(struct vm_area_struct * area,
		     unsigned long address, int no_share)
{
	struct inode * inode = area->vm_inode;
	unsigned long page;
	unsigned int clear;
	unsigned long tmp;
	int n;
	int i;
	int pos;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return 0;
	address &= PAGE_MASK;
	pos = address - area->vm_start + area->vm_offset;

	clear = 0;
	if (address + PAGE_SIZE > area->vm_end) {
		clear = address + PAGE_SIZE - area->vm_end;
	}

        /* what we can read in one go */
	n = SMB_SERVER(inode)->max_xmit - SMB_HEADER_LEN - 5 * 2 - 3 - 10;

        if (smb_make_open(inode, O_RDONLY) < 0) {
                clear = PAGE_SIZE;
        }
        else
        {
        
                for (i = 0; i < (PAGE_SIZE - clear); i += n) {
                        int hunk, result;

                        hunk = PAGE_SIZE - i;
                        if (hunk > n)
                                hunk = n;

                        DDPRINTK("smb_file_mmap_nopage: reading\n");
                        DDPRINTK("smb_file_mmap_nopage: pos = %d\n", pos);
                        result = smb_proc_read(SMB_SERVER(inode),
                                               SMB_FINFO(inode), pos, hunk,
                                               (char *) (page + i), 0);
                        DDPRINTK("smb_file_mmap_nopage: result= %d\n", result);
                        if (result < 0)
                                break;
                        pos += result;
                        if (result < n) {
                                i += result;
                                break;
                        }
                }
        }

	tmp = page + PAGE_SIZE;
	while (clear--) {
		*(char *)--tmp = 0;
	}
	return page;
}

struct vm_operations_struct smb_file_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	smb_file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* swapout */
	NULL,			/* swapin */
};


/* This is used for a general mmap of a smb file */
int
smb_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
        DPRINTK("smb_mmap: called\n");

        /* only PAGE_COW or read-only supported now */
	if (vma->vm_flags & VM_SHARED)	
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}

	vma->vm_inode = inode;
	inode->i_count++;
	vma->vm_ops = &smb_file_mmap;
	return 0;
}
