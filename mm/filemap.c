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
#include <asm/pgtable.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 */

static inline void multi_bmap(struct inode * inode, unsigned int block, unsigned int * nr, int shift)
{
	int i = PAGE_SIZE >> shift;
	block >>= shift;
	do {
		*nr = bmap(inode, block);
		i--;
		block++;
		nr++;
	} while (i > 0);
}

static unsigned long filemap_nopage(struct vm_area_struct * area, unsigned long address,
	unsigned long page, int no_share)
{
	struct inode * inode = area->vm_inode;
	int nr[PAGE_SIZE/512];

	multi_bmap(inode, (address & PAGE_MASK) - area->vm_start + area->vm_offset, nr,
		inode->i_sb->s_blocksize_bits);
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
static void filemap_sync_page(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long page)
{
	struct inode * inode;
	int nr[PAGE_SIZE/512];
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
	inode = vma->vm_inode;
	offset += vma->vm_offset;
	multi_bmap(inode, offset, nr, inode->i_sb->s_blocksize_bits);
	bwrite_page(page, inode->i_dev, nr, inode->i_sb->s_blocksize);
}

/*
 * Swapping to a shared file: while we're busy writing out the page
 * (and the page still exists in memory), we save the page information
 * in the page table, so that "filemap_swapin()" can re-use the page
 * immediately if it is called while we're busy swapping it out..
 *
 * Once we've written it all out, we mark the page entry "empty", which
 * will result in a normal page-in (instead of a swap-in) from the now
 * up-to-date shared file mapping.
 */
void filemap_swapout(struct vm_area_struct * vma,
	unsigned long offset,
	pte_t *page_table)
{
	unsigned long page = pte_page(*page_table);
	unsigned long entry = SWP_ENTRY(SHM_SWP_TYPE, MAP_NR(page));

	pte_val(*page_table) = entry;
	invalidate();
	filemap_sync_page(vma, offset, page);
	if (pte_val(*page_table) == entry)
		pte_clear(page_table);
}

/*
 * filemap_swapin() is called only if we have something in the page
 * tables that is non-zero (but not present), which we know to be the
 * page index of a page that is busy being swapped out (see above).
 * So we just use it directly..
 */
static pte_t filemap_swapin(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long entry)
{
	unsigned long page = SWP_OFFSET(entry);

	mem_map[page]++;
	page = (page << PAGE_SHIFT) + PAGE_OFFSET;
	return pte_mkdirty(mk_pte(page,vma->vm_page_prot));
}

static inline void filemap_sync_pte(pte_t * pte, struct vm_area_struct *vma,
	unsigned long address, unsigned int flags)
{
	pte_t page = *pte;

	if (!pte_present(page))
		return;
	if (!pte_dirty(page))
		return;
	if (flags & MS_INVALIDATE) {
		pte_clear(pte);
	} else {
		mem_map[MAP_NR(pte_page(page))]++;
		*pte = pte_mkclean(page);
	}
	filemap_sync_page(vma, address - vma->vm_start, pte_page(page));
	free_page(pte_page(page));
}

static inline void filemap_sync_pte_range(pmd_t * pmd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned long offset, unsigned int flags)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("filemap_sync_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		filemap_sync_pte(pte, vma, address + offset, flags);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline void filemap_sync_pmd_range(pgd_t * pgd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned int flags)
{
	pmd_t * pmd;
	unsigned long offset, end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		printk("filemap_sync_pmd_range: bad pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, address);
	offset = address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		filemap_sync_pte_range(pmd, address, end - address, vma, offset, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void filemap_sync(struct vm_area_struct * vma, unsigned long address,
	size_t size, unsigned int flags)
{
	pgd_t * dir;
	unsigned long end = address + size;

	dir = pgd_offset(current, address);
	while (address < end) {
		filemap_sync_pmd_range(dir, address, end - address, vma, flags);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	invalidate();
	return;
}

/*
 * This handles area unmaps..
 */
static void filemap_unmap(struct vm_area_struct *vma, unsigned long start, size_t len)
{
	filemap_sync(vma, start, len, MS_ASYNC);
}

/*
 * This handles complete area closes..
 */
static void filemap_close(struct vm_area_struct * vma)
{
	filemap_sync(vma, vma->vm_start, vma->vm_end - vma->vm_start, MS_ASYNC);
}

/*
 * Shared mappings need to be able to do the right thing at
 * close/unmap/sync. They will also use the private file as
 * backing-store for swapping..
 */
static struct vm_operations_struct file_shared_mmap = {
	NULL,			/* open */
	filemap_close,		/* close */
	filemap_unmap,		/* unmap */
	NULL,			/* protect */
	filemap_sync,		/* sync */
	NULL,			/* advise */
	filemap_nopage,		/* nopage */
	NULL,			/* wppage */
	filemap_swapout,	/* swapout */
	filemap_swapin,		/* swapin */
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
	filemap_nopage,		/* nopage */
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
