#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/config.h>
#include <asm/pgalloc.h>

#ifdef CONFIG_HIGHMEM

extern struct page *highmem_start_page;

#include <asm/highmem.h>

/* declarations for linux/mm/highmem.c */
extern unsigned long highmem_mapnr;
FASTCALL(unsigned int nr_free_highpages(void));

extern struct page * prepare_highmem_swapout(struct page *);
extern struct page * replace_with_highmem(struct page *);
extern struct buffer_head * create_bounce(int rw, struct buffer_head * bh_orig);

#else /* CONFIG_HIGHMEM */

extern inline unsigned int nr_free_highpages(void) { return 0; }
#define prepare_highmem_swapout(page) page
#define replace_with_highmem(page) page
#define kmap(page) page_address(page)
#define kunmap(page) do { } while (0)

#endif /* CONFIG_HIGHMEM */

/* when CONFIG_HIGHMEM is not set these will be plain clear/copy_page */
extern inline void clear_highpage(struct page *page)
{
	unsigned long kaddr;

	kaddr = kmap(page);
	clear_page((void *)kaddr);
	kunmap(page);
}

extern inline void memclear_highpage(struct page *page, unsigned int offset, unsigned int size)
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
extern inline void memclear_highpage_flush(struct page *page, unsigned int offset, unsigned int size)
{
	unsigned long kaddr;

	if (offset + size > PAGE_SIZE)
		BUG();
	kaddr = kmap(page);
	memset((void *)(kaddr + offset), 0, size);
	flush_page_to_ram(page);
	kunmap(page);
}

extern inline void copy_highpage(struct page *to, struct page *from)
{
	unsigned long vfrom, vto;

	vfrom = kmap(from);
	vto = kmap(to);
	copy_page((void *)vto, (void *)vfrom);
	kunmap(from);
	kunmap(to);
}

#endif /* _LINUX_HIGHMEM_H */
