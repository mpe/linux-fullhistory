#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/config.h>
#include <asm/pgalloc.h>

#ifdef CONFIG_HIGHMEM

extern struct page *highmem_start_page;

#include <asm/highmem.h>

/* declarations for linux/mm/highmem.c */
FASTCALL(unsigned int nr_free_highpages(void));

extern struct page * prepare_highmem_swapout(struct page *);
extern struct page * replace_with_highmem(struct page *);
extern struct buffer_head * create_bounce(int rw, struct buffer_head * bh_orig);

#else /* CONFIG_HIGHMEM */

static inline unsigned int nr_free_highpages(void) { return 0; }
#define prepare_highmem_swapout(page) page
#define replace_with_highmem(page) page

static __inline__ unsigned long kmap(struct page * page) {
	return (unsigned long) page_address(page);
}

#define kunmap(page) do { } while (0)

#define kmap_atomic(page,idx)		kmap(page)
#define kunmap_atomic(page,idx)		kunmap(page)

#endif /* CONFIG_HIGHMEM */

/* when CONFIG_HIGHMEM is not set these will be plain clear/copy_page */
static inline void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	unsigned long kaddr;

	kaddr = kmap(page);
	clear_user_page((void *)kaddr, vaddr);
	kunmap(page);
}

static inline void clear_highpage(struct page *page)
{
	unsigned long kaddr;

	kaddr = kmap(page);
	clear_page((void *)kaddr);
	kunmap(page);
}

static inline void memclear_highpage(struct page *page, unsigned int offset, unsigned int size)
{
	unsigned long kaddr;

	if (offset + size > PAGE_SIZE)
		BUG();
	kaddr = kmap(page);
	memset((void *)(kaddr + offset), 0, size);
	kunmap(page);
}

/*
 * Same but also flushes aliased cache contents to RAM.
 */
static inline void memclear_highpage_flush(struct page *page, unsigned int offset, unsigned int size)
{
	unsigned long kaddr;

	if (offset + size > PAGE_SIZE)
		BUG();
	kaddr = kmap(page);
	memset((void *)(kaddr + offset), 0, size);
	flush_page_to_ram(page);
	kunmap(page);
}

static inline void copy_user_highpage(struct page *to, struct page *from, unsigned long vaddr)
{
	unsigned long vfrom, vto;

	vfrom = kmap(from);
	vto = kmap(to);
	copy_user_page((void *)vto, (void *)vfrom, vaddr);
	kunmap(from);
	kunmap(to);
}

static inline void copy_highpage(struct page *to, struct page *from)
{
	unsigned long vfrom, vto;

	vfrom = kmap(from);
	vto = kmap(to);
	copy_page((void *)vto, (void *)vfrom);
	kunmap(from);
	kunmap(to);
}

#endif /* _LINUX_HIGHMEM_H */
