#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Page-mapping primitive inline functions
 *
 * Copyright 1995 Linus Torvalds
 */

static inline unsigned long page_address(struct page * page)
{
	return PAGE_OFFSET + PAGE_SIZE*(page - mem_map);
}

#define PAGE_HASH_SIZE 257

extern struct page * page_hash_table[PAGE_HASH_SIZE];

static inline unsigned long _page_hashfn(struct inode * inode, unsigned long offset)
{
	offset ^= (unsigned long) inode;
	return offset % PAGE_HASH_SIZE;
}

#define page_hash(inode,offset) page_hash_table[_page_hashfn(inode,offset)]

static inline struct page * find_page(struct inode * inode, unsigned long offset)
{
	struct page *page;

	for (page = page_hash(inode, offset); page ; page = page->next_hash) {
		if (page->inode != inode)
			continue;
		if (page->offset != offset)
			continue;
		break;
	}
	return page;
}

static inline void remove_page_from_hash_queue(struct page * page)
{
	struct page **p = &page_hash(page->inode,page->offset);

	if (page->next_hash)
		page->next_hash->prev_hash = page->prev_hash;
	if (page->prev_hash)
		page->prev_hash->next_hash = page->next_hash;
	if (*p == page)
		*p = page->next_hash;
	page->next_hash = page->prev_hash = NULL;
}

static inline void add_page_to_hash_queue(struct inode * inode, struct page * page)
{
	struct page **p = &page_hash(inode,page->offset);

	page->prev_hash = NULL;
	if ((page->next_hash = *p) != NULL)
		page->next_hash->prev_hash = page;
	*p = page;
}

static inline void remove_page_from_inode_queue(struct page * page)
{
	struct inode * inode = page->inode;

	page->inode = NULL;
	inode->i_nrpages--;
	if (inode->i_pages == page)
		inode->i_pages = page->next;
	if (page->next)
		page->next->prev = page->prev;
	if (page->prev)
		page->prev->next = page->next;
	page->next = NULL;
	page->prev = NULL;
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

#endif
