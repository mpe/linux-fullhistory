/*
 *	linux/mm/filemmap.c
 *
 * Copyright (C) 1994 Linus Torvalds
 */

/*
 * This file handles the generic file mmap semantics used by
 * most "normal" filesystems (but you don't /have/ to use this:
 * the NFS filesystem does this differently, for example)
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

#include <asm/segment.h>
#include <asm/system.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 */

static unsigned long file_mmap_nopage(struct vm_area_struct * area, unsigned long address,
	unsigned long page, int no_share)
{
	struct inode * inode = area->vm_inode;
	unsigned int block;
	int nr[8];
	int i, *p;

	address &= PAGE_MASK;
	block = address - area->vm_start + area->vm_offset;
	block >>= inode->i_sb->s_blocksize_bits;
	i = PAGE_SIZE >> inode->i_sb->s_blocksize_bits;
	p = nr;
	do {
		*p = bmap(inode,block);
		i--;
		block++;
		p++;
	} while (i > 0);
	return bread_page(page, inode->i_dev, nr, inode->i_sb->s_blocksize, no_share);
}

/*
 * NOTE! mmap sync doesn't really work yet. This is mainly a stub for it,
 * which only works if the buffers and the page were already sharing the
 * same physical page (that's actually pretty common, especially if the
 * file has been mmap'ed before being read the normal way).
 *
 * Todo:
 * - non-shared pages also need to be synced with the buffers.
 * - the "swapout()" function needs to swap out the page to
 *   the shared file instead of using the swap device.
 */
static inline void file_mmap_sync_page(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long page)
{
	struct buffer_head * bh;

	bh = buffer_pages[MAP_NR(page)];
	if (bh) {
		/* whee.. just mark the buffer heads dirty */
		struct buffer_head * tmp = bh;
		do {
			mark_buffer_dirty(tmp, 0);
			tmp = tmp->b_this_page;
		} while (tmp != bh);
		return;
	}
	/* we'll need to go fetch the buffer heads etc.. RSN */
	printk("msync: %ld: [%08lx]\n", offset, page);
	printk("Can't handle non-shared page yet\n");
	return;
}

static void file_mmap_sync(struct vm_area_struct * vma, unsigned long start,
	size_t size, unsigned int flags)
{
	unsigned long page_dir;
	unsigned long *page_table, *dir;
	unsigned long poff, pcnt, pc;

	size = size >> PAGE_SHIFT;
	dir = PAGE_DIR_OFFSET(current->tss.cr3,start);
	poff = (start >> PAGE_SHIFT) & (PTRS_PER_PAGE-1);
	start -= vma->vm_start;
	if ((pcnt = PTRS_PER_PAGE - poff) > size)
		pcnt = size;

	for ( ; size > 0; ++dir, size -= pcnt,
	     pcnt = (size > PTRS_PER_PAGE ? PTRS_PER_PAGE : size)) {
		if (!(PAGE_PRESENT & (page_dir = *dir))) {
			if (page_dir)
				printk("file_mmap_sync: bad page directory.\n");
			poff = 0;
			start += pcnt*PAGE_SIZE;
			continue;
		}
		page_table = (unsigned long *)(PAGE_MASK & page_dir);
		if (poff) {
			page_table += poff;
			poff = 0;
		}
		for (pc = pcnt; pc--; page_table++, start += PAGE_SIZE) {
			unsigned long page = *page_table;
			if (!(page & PAGE_PRESENT))
				continue;
			if (!(page & PAGE_DIRTY))
				continue;
			mem_map[MAP_NR(page)]++;
			if (flags & MS_INVALIDATE) {
				*page_table = 0;
				free_page(page);
			} else
				*page_table = page & ~PAGE_DIRTY;
			file_mmap_sync_page(vma, start, page);
			free_page(page);
		}
	}
	invalidate();
	return;
}

/*
 * This handles area unmaps..
 */
static void file_mmap_unmap(struct vm_area_struct *vma, unsigned long start, size_t len)
{
	if (vma->vm_page_prot & PAGE_RW)
		file_mmap_sync(vma, start, len, MS_ASYNC);
}

/*
 * This handles complete area closes..
 */
static void file_mmap_close(struct vm_area_struct * vma)
{
	if (vma->vm_page_prot & PAGE_RW)
		file_mmap_sync(vma, vma->vm_start, vma->vm_end - vma->vm_start, MS_ASYNC);
}

/*
 * This isn't implemented yet: you'll get a warning and incorrect behaviour.
 *
 * Note that the page is free'd by the higher-level after return,
 * so we have to either write it out or just forget it. We currently
 * forget it..
 */
void file_mmap_swapout(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long *pte)
{
	printk("swapout not implemented on shared files..\n");
	*pte = 0;
}

/*
 * Shared mappings need to be able to do the right thing at
 * close/unmap/sync. They will also use the private file as
 * backing-store for swapping..
 */
static struct vm_operations_struct file_shared_mmap = {
	NULL,			/* open */
	file_mmap_close,	/* close */
	file_mmap_unmap,	/* unmap */
	NULL,			/* protect */
	file_mmap_sync,		/* sync */
	NULL,			/* advise */
	file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	file_mmap_swapout,	/* swapout */
	NULL,			/* swapin */
};

/*
 * Private mappings just need to be able to load in the map
 *
 * (this is actually used for shared mappings as well, if we
 * know they can't ever get write permissions..)
 */
static struct vm_operations_struct file_private_mmap = {
	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	file_mmap_nopage,	/* nopage */
	NULL,			/* wppage */
	NULL,			/* swapout */
	NULL,			/* swapin */
};

/* This is used for a general mmap of a disk file */
int generic_mmap(struct inode * inode, struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;

	if (vma->vm_offset & (inode->i_sb->s_blocksize - 1))
		return -EINVAL;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!inode->i_op || !inode->i_op->bmap)
		return -ENOEXEC;
	ops = &file_private_mmap;
	if (vma->vm_flags & VM_SHARED) {
		if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
			ops = &file_shared_mmap;
	}
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	vma->vm_inode = inode;
	inode->i_count++;
	vma->vm_ops = ops;
	return 0;
}
