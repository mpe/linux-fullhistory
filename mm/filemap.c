/*
 *	linux/mm/filemap.c
 *
 * Copyright (C) 1994, 1995  Linus Torvalds
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
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 *
 * Shared mappings now work. 15.8.1995  Bruno.
 */

unsigned long page_cache_size = 0;
struct page * page_hash_table[PAGE_HASH_SIZE];

/*
 * Simple routines for both non-shared and shared mappings.
 */

void invalidate_inode_pages(struct inode * inode, unsigned long start)
{
	struct page ** p;
	struct page * page;

repeat:
	p = &inode->i_pages;
	while ((page = *p) != NULL) {
		unsigned long offset = page->offset;

		/* page wholly truncated - free it */
		if (offset >= start) {
			if (page->locked) {
				wait_on_page(page);
				goto repeat;
			}
			inode->i_nrpages--;
			if ((*p = page->next) != NULL)
				(*p)->prev = page->prev;
			page->dirty = 0;
			page->next = NULL;
			page->prev = NULL;
			remove_page_from_hash_queue(page);
			page->inode = NULL;
			free_page(page_address(page));
			continue;
		}
		p = &page->next;
		offset = start - offset;
		/* partial truncate, clear end of page */
		if (offset < PAGE_SIZE)
			memset((void *) (offset + page_address(page)), 0, PAGE_SIZE - offset);
	}
}

int shrink_mmap(int priority, unsigned long limit)
{
	static int clock = 0;
	struct page * page;
	struct buffer_head *tmp, *bh;

	if (limit > high_memory)
		limit = high_memory;
	limit = MAP_NR(limit);
	if (clock >= limit)
		clock = 0;
	priority = (limit<<2) >> priority;
	page = mem_map + clock;
	while (priority-- > 0) {
		/* First of all, regenerate the page's referenced bit
                   from any buffers in the page */
		bh = buffer_pages[MAP_NR(page_address(page))];
		if (bh) {
			tmp = bh;
			do {
				if (buffer_touched(tmp)) {
					clear_bit(BH_Touched, &tmp->b_state);
					page->referenced = 1;
				}
				tmp = tmp->b_this_page;
			} while (tmp != bh);
		}

		/* We can't throw away shared pages, but we do mark
		   them as referenced.  This relies on the fact that
		   no page is currently in both the page cache and the
		   buffer cache; we'd have to modify the following
		   test to allow for that case. */
		if (page->count > 1)
			page->referenced = 1;
		else if (page->referenced)
			page->referenced = 0;
		else if (page->count && !page->locked) {
			/* The page is an old, unshared page --- try
                           to discard it. */
			if (page->inode) {
				remove_page_from_hash_queue(page);
				remove_page_from_inode_queue(page);
				free_page(page_address(page));
				return 1;
			}
			if (bh && try_to_free_buffer(bh, &bh, 6))
				return 1;
		}
		page++;
		clock++;
		if (clock >= limit) {
			clock = 0;
			page = mem_map;
		}
	}
	return 0;
}

/*
 * This is called from try_to_swap_out() when we try to egt rid of some
 * pages..  If we're unmapping the last occurrence of this page, we also
 * free it from the page hash-queues etc, as we don't want to keep it
 * in-core unnecessarily.
 */
unsigned long page_unuse(unsigned long page)
{
	struct page * p = mem_map + MAP_NR(page);
	int count = p->count;

	if (count != 2)
		return count;
	if (!p->inode)
		return count;
	remove_page_from_hash_queue(p);
	remove_page_from_inode_queue(p);
	free_page(page);
	return 1;
}

/*
 * Update a page cache copy, when we're doing a "write()" system call
 * See also "update_vm_cache()".
 */
void update_vm_cache(struct inode * inode, unsigned long pos, const char * buf, int count)
{
	struct page * page;

	page = find_page(inode, pos & PAGE_MASK);
	if (page) {
		unsigned long addr;

		wait_on_page(page);
		addr = page_address(page);
		memcpy((void *) ((pos & ~PAGE_MASK) + addr), buf, count);
		free_page(addr);
	}
}

/*
 * Try to read ahead in the file. "page_cache" is a potentially free page
 * that we could use for the cache (if it is 0 we can try to create one,
 * this is all overlapped with the IO on the previous page finishing anyway)
 */
static unsigned long try_to_read_ahead(struct inode * inode, unsigned long offset, unsigned long page_cache)
{
	struct page * page;

	offset &= PAGE_MASK;
	if (!page_cache) {
		page_cache = __get_free_page(GFP_KERNEL);
		if (!page_cache)
			return 0;
	}
	if (offset >= inode->i_size)
		return page_cache;
#if 1
	page = find_page(inode, offset);
	if (page) {
		page->count--;
		return page_cache;
	}
	/*
	 * Ok, add the new page to the hash-queues...
	 */
	page = mem_map + MAP_NR(page_cache);
	page->count++;
	page->uptodate = 0;
	page->error = 0;
	page->offset = offset;
	add_page_to_inode_queue(inode, page);
	add_page_to_hash_queue(inode, page);

	inode->i_op->readpage(inode, page);

	free_page(page_cache);
	return 0;
#else
	return page_cache;
#endif
}

/* 
 * Wait for IO to complete on a locked page.
 */
void __wait_on_page(struct page *page)
{
	struct wait_queue wait = { current, NULL };

	page->count++;
	add_wait_queue(&page->wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (page->locked) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&page->wait, &wait);
	page->count--;
	current->state = TASK_RUNNING;
}


/*
 * This is a generic file read routine, and uses the
 * inode->i_op->readpage() function for the actual low-level
 * stuff.
 */
#define READAHEAD_PAGES 3
#define MAX_IO_PAGES 4
int generic_file_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int read = 0, newpage = 0;
	unsigned long pos;
	unsigned long page_cache = 0;
	int pre_read = 0;
	
	if (count <= 0)
		return 0;

	pos = filp->f_pos;
	do {
		struct page *page;
		unsigned long offset, addr, nr;
		int i;
		off_t p;

		if (pos >= inode->i_size)
			break;
		offset = pos & ~PAGE_MASK;
		nr = PAGE_SIZE - offset;
		/*
		 * Try to find the data in the page cache..
		 */
		page = find_page(inode, pos & PAGE_MASK);
		if (page)
			goto found_page;

		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 */
		if (!page_cache) {
			page_cache = __get_free_page(GFP_KERNEL);
			if (!page_cache) {
				if (!read)
					read = -ENOMEM;
				break;
			}
		}

		/*
		 * That could have slept, so we need to check again..
		 */
		if (pos >= inode->i_size)
			break;
		page = find_page(inode, pos & PAGE_MASK);
		if (page)
			goto found_page;

		/*
		 * Ok, add the new page to the hash-queues...
		 */
		page = mem_map + MAP_NR(page_cache);
		page_cache = 0;
		page->count++;
		page->uptodate = 0;
		page->error = 0;
		page->offset = pos & PAGE_MASK;
		add_page_to_inode_queue(inode, page);
		add_page_to_hash_queue(inode, page);

		inode->i_op->readpage(inode, page);
		/* We only set "newpage" when we encounter a
                   completely uncached page.  This way, we do no
                   readahead if we are still just reading data out of
                   the cache (even if the cached page is not yet
                   uptodate --- it may be currently being read as a
                   result of previous readahead).  -- sct */
		newpage = 1;

found_page:
		addr = page_address(page);
		if (nr > count)
			nr = count;
		/* We have two readahead cases.  First, do data
                   pre-read if the current read request is for more
                   than one page, so we can merge the adjacent
                   requests. */
		if (newpage && nr < count) {
			if (pre_read > 0)
				pre_read -= PAGE_SIZE;
			else {
				pre_read = (MAX_IO_PAGES-1) * PAGE_SIZE;
				if (pre_read > (count - nr))
					pre_read = count - nr;
				for (i=0, p=pos; i<pre_read; i+=PAGE_SIZE) {
					p += PAGE_SIZE;
					page_cache = try_to_read_ahead(inode, p, page_cache);
				}
			}
		}
		else
		/* Second, do readahead at the end of the read, if we
                   are still waiting on the current IO to complete, if
                   readahead is flagged for the file, and if we have
                   finished with the current block. */
		if (newpage && nr == count && filp->f_reada
		    && !((pos + nr) & ~PAGE_MASK)) {
			for (i=0, p=pos; i<READAHEAD_PAGES; i++) {
				p += PAGE_SIZE;
				page_cache = try_to_read_ahead(inode, p, page_cache);
			}
		}
		wait_on_page(page);
		if (nr > inode->i_size - pos)
			nr = inode->i_size - pos;
		memcpy_tofs(buf, (void *) (addr + offset), nr);
		free_page(addr);
		buf += nr;
		pos += nr;
		read += nr;
		count -= nr;
	} while (count);

	filp->f_pos = pos;
	filp->f_reada = 1;
	if (page_cache)
		free_page(page_cache);
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	return read;
}

/*
 * Find a cached page and wait for it to become up-to-date, return
 * the page address.  Increments the page count.
 */
static inline unsigned long fill_page(struct inode * inode, unsigned long offset)
{
	struct page * page;
	unsigned long new_page;

	page = find_page(inode, offset);
	if (page)
		goto found_page;
	new_page = __get_free_page(GFP_KERNEL);
	page = find_page(inode, offset);
	if (page) {
		if (new_page)
			free_page(new_page);
		goto found_page;
	}
	if (!new_page)
		return 0;
	page = mem_map + MAP_NR(new_page);
	new_page = 0;
	page->count++;
	page->uptodate = 0;
	page->error = 0;
	page->offset = offset;
	add_page_to_inode_queue(inode, page);
	add_page_to_hash_queue(inode, page);
	inode->i_op->readpage(inode, page);
found_page:
	wait_on_page(page);
	return page_address(page);
}

/*
 * Semantics for shared and private memory areas are different past the end
 * of the file. A shared mapping past the last page of the file is an error
 * and results in a SIBGUS, while a private mapping just maps in a zero page.
 */
static unsigned long filemap_nopage(struct vm_area_struct * area, unsigned long address, int no_share)
{
	unsigned long offset;
	struct inode * inode = area->vm_inode;
	unsigned long page;

	offset = (address & PAGE_MASK) - area->vm_start + area->vm_offset;
	if (offset >= inode->i_size && (area->vm_flags & VM_SHARED) && area->vm_mm == current->mm)
		return 0;

	page = fill_page(inode, offset);
	if (page && no_share) {
		unsigned long new_page = __get_free_page(GFP_KERNEL);
		if (new_page)
			memcpy((void *) new_page, (void *) page, PAGE_SIZE);
		free_page(page);
		return new_page;
	}
	return page;
}

/*
 * Tries to write a shared mapped page to its backing store. May return -EIO
 * if the disk is full.
 */
static int filemap_write_page(struct vm_area_struct * vma,
	unsigned long offset,
	unsigned long page)
{
	int old_fs;
	unsigned long size, result;
	struct file file;
	struct inode * inode;
	struct buffer_head * bh;

	bh = buffer_pages[MAP_NR(page)];
	if (bh) {
		/* whee.. just mark the buffer heads dirty */
		struct buffer_head * tmp = bh;
		do {
			mark_buffer_dirty(tmp, 0);
			tmp = tmp->b_this_page;
		} while (tmp != bh);
		return 0;
	}

	inode = vma->vm_inode;
	file.f_op = inode->i_op->default_file_ops;
	if (!file.f_op->write)
		return -EIO;
	size = offset + PAGE_SIZE;
	/* refuse to extend file size.. */
	if (S_ISREG(inode->i_mode)) {
		if (size > inode->i_size)
			size = inode->i_size;
		/* Ho humm.. We should have tested for this earlier */
		if (size < offset)
			return -EIO;
	}
	size -= offset;
	file.f_mode = 3;
	file.f_flags = 0;
	file.f_count = 1;
	file.f_inode = inode;
	file.f_pos = offset;
	file.f_reada = 0;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	result = file.f_op->write(inode, &file, (const char *) page, size);
	set_fs(old_fs);
	if (result != size)
		return -EIO;
	return 0;
}


/*
 * Swapping to a shared file: while we're busy writing out the page
 * (and the page still exists in memory), we save the page information
 * in the page table, so that "filemap_swapin()" can re-use the page
 * immediately if it is called while we're busy swapping it out..
 *
 * Once we've written it all out, we mark the page entry "empty", which
 * will result in a normal page-in (instead of a swap-in) from the now
 * up-to-date disk file.
 */
int filemap_swapout(struct vm_area_struct * vma,
	unsigned long offset,
	pte_t *page_table)
{
	int error;
	unsigned long page = pte_page(*page_table);
	unsigned long entry = SWP_ENTRY(SHM_SWP_TYPE, MAP_NR(page));

	set_pte(page_table, __pte(entry));
	/* Yuck, perhaps a slightly modified swapout parameter set? */
	invalidate_page(vma, (offset + vma->vm_start - vma->vm_offset));
	error = filemap_write_page(vma, offset, page);
	if (pte_val(*page_table) == entry)
		pte_clear(page_table);
	return error;
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

	mem_map[page].count++;
	page = (page << PAGE_SHIFT) + PAGE_OFFSET;
	return mk_pte(page,vma->vm_page_prot);
}


static inline int filemap_sync_pte(pte_t * ptep, struct vm_area_struct *vma,
	unsigned long address, unsigned int flags)
{
	pte_t pte = *ptep;
	unsigned long page;
	int error;

	if (!(flags & MS_INVALIDATE)) {
		if (!pte_present(pte))
			return 0;
		if (!pte_dirty(pte))
			return 0;
		set_pte(ptep, pte_mkclean(pte));
		invalidate_page(vma, address);
		page = pte_page(pte);
		mem_map[MAP_NR(page)].count++;
	} else {
		if (pte_none(pte))
			return 0;
		pte_clear(ptep);
		invalidate_page(vma, address);
		if (!pte_present(pte)) {
			swap_free(pte_val(pte));
			return 0;
		}
		page = pte_page(pte);
		if (!pte_dirty(pte) || flags == MS_INVALIDATE) {
			free_page(page);
			return 0;
		}
	}
	error = filemap_write_page(vma, address - vma->vm_start + vma->vm_offset, page);
	free_page(page);
	return error;
}

static inline int filemap_sync_pte_range(pmd_t * pmd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned long offset, unsigned int flags)
{
	pte_t * pte;
	unsigned long end;
	int error;

	if (pmd_none(*pmd))
		return 0;
	if (pmd_bad(*pmd)) {
		printk("filemap_sync_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return 0;
	}
	pte = pte_offset(pmd, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	error = 0;
	do {
		error |= filemap_sync_pte(pte, vma, address + offset, flags);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return error;
}

static inline int filemap_sync_pmd_range(pgd_t * pgd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned int flags)
{
	pmd_t * pmd;
	unsigned long offset, end;
	int error;

	if (pgd_none(*pgd))
		return 0;
	if (pgd_bad(*pgd)) {
		printk("filemap_sync_pmd_range: bad pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		return 0;
	}
	pmd = pmd_offset(pgd, address);
	offset = address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	error = 0;
	do {
		error |= filemap_sync_pte_range(pmd, address, end - address, vma, offset, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return error;
}

static int filemap_sync(struct vm_area_struct * vma, unsigned long address,
	size_t size, unsigned int flags)
{
	pgd_t * dir;
	unsigned long end = address + size;
	int error = 0;

	dir = pgd_offset(current->mm, address);
	while (address < end) {
		error |= filemap_sync_pmd_range(dir, address, end - address, vma, flags);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	invalidate_range(vma->vm_mm, end - size, end);
	return error;
}

/*
 * This handles (potentially partial) area unmaps..
 */
static void filemap_unmap(struct vm_area_struct *vma, unsigned long start, size_t len)
{
	filemap_sync(vma, start, len, MS_ASYNC);
}

/*
 * Shared mappings need to be able to do the right thing at
 * close/unmap/sync. They will also use the private file as
 * backing-store for swapping..
 */
static struct vm_operations_struct file_shared_mmap = {
	NULL,			/* no special open */
	NULL,			/* no special close */
	filemap_unmap,		/* unmap - we need to sync the pages */
	NULL,			/* no special protect */
	filemap_sync,		/* sync */
	NULL,			/* advise */
	filemap_nopage,		/* nopage */
	NULL,			/* wppage */
	filemap_swapout,	/* swapout */
	filemap_swapin,		/* swapin */
};

/*
 * Private mappings just need to be able to load in the map.
 *
 * (This is actually used for shared mappings as well, if we
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

	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE)) {
		ops = &file_shared_mmap;
		/* share_page() can only guarantee proper page sharing if
		 * the offsets are all page aligned. */
		if (vma->vm_offset & (PAGE_SIZE - 1))
			return -EINVAL;
	} else {
		ops = &file_private_mmap;
		if (vma->vm_offset & (inode->i_sb->s_blocksize - 1))
			return -EINVAL;
	}
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	if (!inode->i_op || !inode->i_op->readpage)
		return -ENOEXEC;
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	vma->vm_inode = inode;
	inode->i_count++;
	vma->vm_ops = ops;
	return 0;
}


/*
 * The msync() system call.
 */

static int msync_interval(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int flags)
{
	if (!vma->vm_inode)
		return 0;
	if (vma->vm_ops->sync) {
		int error;
		error = vma->vm_ops->sync(vma, start, end-start, flags);
		if (error)
			return error;
		if (flags & MS_SYNC)
			return file_fsync(vma->vm_inode, NULL);
		return 0;
	}
	return 0;
}

asmlinkage int sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error, error;

	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
		return -EINVAL;
	if (end == start)
		return 0;
	/*
	 * If the interval [start,end) covers some unmapped address ranges,
	 * just ignore them, but return -EFAULT at the end.
	 */
	vma = find_vma(current, start);
	unmapped_error = 0;
	for (;;) {
		/* Still start < end. */
		if (!vma)
			return -EFAULT;
		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -EFAULT;
			start = vma->vm_start;
		}
		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = msync_interval(vma, start, end, flags);
				if (error)
					return error;
			}
			return unmapped_error;
		}
		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = msync_interval(vma, start, vma->vm_end, flags);
		if (error)
			return error;
		start = vma->vm_end;
		vma = vma->vm_next;
	}
}
