#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

#include <asm/system.h>

/*
 * Page-mapping primitive inline functions
 *
 * Copyright 1995 Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/fs.h>

static inline unsigned long page_address(struct page * page)
{
	return PAGE_OFFSET + ((page - mem_map) << PAGE_SHIFT);
}

/*
 * The page cache can done in larger chunks than
 * one page, because it allows for more efficient
 * throughput (it can then be mapped into user
 * space in smaller chunks for same flexibility).
 *
 * Or rather, it _will_ be done in larger chunks.
 */
#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_SIZE		PAGE_SIZE
#define PAGE_CACHE_MASK		PAGE_MASK
#define PAGE_CACHE_ALIGN(addr)	(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK)

#define page_cache_alloc()	__get_free_page(GFP_USER)
#define page_cache_free(x)	free_page(x)
#define page_cache_release(x)	__free_page(x)

/*
 * From a kernel address, get the "struct page *"
 */
#define page_cache_entry(x)	(mem_map + MAP_NR(x))

#define PAGE_HASH_BITS 16
#define PAGE_HASH_SIZE (1 << PAGE_HASH_BITS)

extern atomic_t page_cache_size; /* # of pages currently in the hash table */
extern struct page * page_hash_table[PAGE_HASH_SIZE];

/*
 * We use a power-of-two hash table to avoid a modulus,
 * and get a reasonable hash by knowing roughly how the
 * inode pointer and offsets are distributed (ie, we
 * roughly know which bits are "significant")
 */
static inline unsigned long _page_hashfn(struct inode * inode, unsigned long offset)
{
#define i (((unsigned long) inode)/(sizeof(struct inode) & ~ (sizeof(struct inode) - 1)))
#define o (offset >> PAGE_SHIFT)
#define s(x) ((x)+((x)>>PAGE_HASH_BITS))
	return s(i+o) & (PAGE_HASH_SIZE-1);
#undef i
#undef o
#undef s
}

#define page_hash(inode,offset) (page_hash_table+_page_hashfn(inode,offset))

extern struct page * __find_get_page (struct inode * inode,
				unsigned long offset, struct page **hash);
#define find_get_page(inode, offset) \
		__find_get_page(inode, offset, page_hash(inode, offset))
extern struct page * __find_lock_page (struct inode * inode,
				unsigned long offset, struct page **hash);
extern void lock_page(struct page *page);
#define find_lock_page(inode, offset) \
		__find_lock_page(inode, offset, page_hash(inode, offset))

extern void __add_page_to_hash_queue(struct page * page, struct page **p);

extern int add_to_page_cache_unique(struct page * page, struct inode * inode, unsigned long offset, struct page **hash);

static inline void add_page_to_hash_queue(struct page * page, struct inode * inode, unsigned long offset)
{
	__add_page_to_hash_queue(page, page_hash(inode,offset));
}

static inline void add_page_to_inode_queue(struct inode * inode, struct page * page)
{
	struct page **p = &inode->i_pages;

	inode->i_nrpages++;
	page->inode = inode;
	page->prev = NULL;
	if ((page->next = *p) != NULL)
		page->next->prev = page;
	*p = page;
}

extern void ___wait_on_page(struct page *);

static inline void wait_on_page(struct page * page)
{

	if (PageLocked(page))
		___wait_on_page(page);
}

extern void update_vm_cache(struct inode *, unsigned long, const char *, int);

#endif
