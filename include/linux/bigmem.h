#ifndef _LINUX_BIGMEM_H
#define _LINUX_BIGMEM_H

#include <linux/config.h>

#ifdef CONFIG_BIGMEM

#include <asm/bigmem.h>

/* declarations for linux/mm/bigmem.c */
extern unsigned long bigmem_mapnr;
extern int nr_free_bigpages;

extern struct page * prepare_bigmem_swapout(struct page *);
extern struct page * replace_with_bigmem(struct page *);

#else /* CONFIG_BIGMEM */

#define prepare_bigmem_swapout(page) page
#define replace_with_bigmem(page) page
#define kmap(kaddr, type) kaddr
#define kunmap(vaddr, type) do { } while (0)
#define nr_free_bigpages 0

#endif /* CONFIG_BIGMEM */

/* when CONFIG_BIGMEM is not set these will be plain clear/copy_page */
extern inline void clear_bigpage(unsigned long kaddr)
{
	unsigned long vaddr;

	vaddr = kmap(kaddr, KM_WRITE);
	clear_page(vaddr);
	kunmap(vaddr, KM_WRITE);
}

extern inline void copy_bigpage(unsigned long to, unsigned long from)
{
	unsigned long vfrom, vto;

	vfrom = kmap(from, KM_READ);
	vto = kmap(to, KM_WRITE);
	copy_page(vto, vfrom);
	kunmap(vfrom, KM_READ);
	kunmap(vto, KM_WRITE);
}

#endif /* _LINUX_BIGMEM_H */
