/*
 * Dynamic DMA mapping support.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/errno.h>

#include <asm/io.h>
#include <asm/pgalloc.h>

/*
 * This allocates one page of cache-coherent memory space and returns
 * both the virtual and a "dma" address to that space.  It is not clear
 * whether this could be called from an interrupt context or not.  For
 * now, we expressly forbid it, especially as some of the stuff we do
 * here is not interrupt context safe.
 */
void *consistent_alloc(int gfp, size_t size, dma_addr_t *dma_handle)
{
	int order;
	unsigned long page;
	void *ret;

	if (in_interrupt())
		BUG();

	order = get_order(size);

	page = __get_free_pages(gfp, order);
	if (!page)
		goto no_page;

	memset((void *)page, 0, PAGE_SIZE << order);
	clean_cache_area(page, PAGE_SIZE << order);

	*dma_handle = virt_to_bus((void *)page);

	ret = __ioremap(virt_to_phys((void *)page), PAGE_SIZE << order, 0);
	if (ret)
		return ret;

	free_pages(page, order);
no_page:
	BUG();
	return NULL;
}

/*
 * free a page as defined by the above mapping.  We expressly forbid
 * calling this from interrupt context.
 */
void consistent_free(void *vaddr)
{
	if (in_interrupt())
		BUG();

	__iounmap(vaddr);
}

/*
 * make an area consistent.
 */
void consistent_sync(void *vaddr, size_t size, int rw)
{
	switch (rw) {
	case 0:
		BUG();
	case 1:	/* invalidate only */
		dma_cache_inv(vaddr, size);
		break;
	case 2:	/* writeback only */
		dma_cache_wback(vaddr, size);
		break;
	case 3:	/* writeback and invalidate */
		dma_cache_wback_inv(vaddr, size);
		break;
	}
}
