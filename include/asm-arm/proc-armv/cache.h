/*
 *  linux/include/asm-arm/proc-armv/cache.h
 *
 *  Copyright (C) 1999-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/mman.h>
#include <asm/glue.h>

/*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty PG_arch_1

/*
 * Cache handling for 32-bit ARM processors.
 *
 * Note that on ARM, we have a more accurate specification than that
 * Linux's "flush".  We therefore do not use "flush" here, but instead
 * use:
 *
 * clean:      the act of pushing dirty cache entries out to memory.
 * invalidate: the act of discarding data held within the cache,
 *             whether it is dirty or not.
 */

/*
 * Generic I + D cache
 */
#define flush_cache_all()						\
	do {								\
		cpu_cache_clean_invalidate_all();			\
	} while (0)

/* This is always called for current->mm */
#define flush_cache_mm(_mm)						\
	do {								\
		if ((_mm) == current->active_mm)			\
			cpu_cache_clean_invalidate_all();		\
	} while (0)

#define flush_cache_range(_vma,_start,_end)				\
	do {								\
		if ((_vma)->vm_mm == current->active_mm)		\
			cpu_cache_clean_invalidate_range((_start), (_end), 1); \
	} while (0)

#define flush_cache_page(_vma,_vmaddr)					\
	do {								\
		if ((_vma)->vm_mm == current->active_mm) {		\
			cpu_cache_clean_invalidate_range((_vmaddr),	\
				(_vmaddr) + PAGE_SIZE,			\
				((_vma)->vm_flags & VM_EXEC));		\
		} \
	} while (0)

/*
 * D cache only
 */

#define invalidate_dcache_range(_s,_e)	cpu_dcache_invalidate_range((_s),(_e))
#define clean_dcache_range(_s,_e)	cpu_dcache_clean_range((_s),(_e))
#define flush_dcache_range(_s,_e)	cpu_cache_clean_invalidate_range((_s),(_e),0)

#define clean_dcache_area(start,size) \
	cpu_cache_clean_invalidate_range((unsigned long)start, \
					 ((unsigned long)start) + size, 0);

/*
 * This is an obsolete interface; the functionality that was provided by this
 * function is now merged into our flush_dcache_page, flush_icache_page,
 * copy_user_page and clear_user_page functions.
 */
#define flush_page_to_ram(page)	do { } while (0)

/*
 * flush_dcache_page is used when the kernel has written to the page
 * cache page at virtual address page->virtual.
 *
 * If this page isn't mapped (ie, page->mapping = NULL), or it has
 * userspace mappings (page->mapping->i_mmap or page->mapping->i_mmap_shared)
 * then we _must_ always clean + invalidate the dcache entries associated
 * with the kernel mapping.
 *
 * Otherwise we can defer the operation, and clean the cache when we are
 * about to change to user space.  This is the same method as used on SPARC64.
 * See update_mmu_cache for the user space part.
 */
#define mapping_mapped(map)	(!list_empty(&(map)->i_mmap) || \
				 !list_empty(&(map)->i_mmap_shared))

static inline void __flush_dcache_page(struct page *page)
{
	unsigned long virt = (unsigned long)page_address(page);
	cpu_cache_clean_invalidate_range(virt, virt + PAGE_SIZE, 0);
}

static inline void flush_dcache_page(struct page *page)
{
	if (page->mapping && !mapping_mapped(page->mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else
		__flush_dcache_page(page);
}

#define flush_icache_user_range(vma,page,addr,len) \
	flush_dcache_page(page)

/*
 * We don't appear to need to do anything here.  In fact, if we did, we'd
 * duplicate cache flushing elsewhere performed by flush_dcache_page().
 */
#define flush_icache_page(vma,page)	do { } while (0)

#define clean_dcache_entry(_s)		cpu_dcache_clean_entry((unsigned long)(_s))

/*
 * I cache coherency stuff.
 *
 * This *is not* just icache.  It is to make data written to memory
 * consistent such that instructions fetched from the region are what
 * we expect.
 *
 * This generally means that we have to clean out the Dcache and write
 * buffers, and maybe flush the Icache in the specified range.
 */
#define flush_icache_range(_s,_e)					\
	do {								\
		cpu_icache_invalidate_range((_s), (_e));		\
	} while (0)
