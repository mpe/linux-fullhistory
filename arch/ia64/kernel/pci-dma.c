/*
 * Dynamic DMA mapping support.
 *
 * This implementation is for IA-64 platforms that do not support
 * I/O TLBs (aka DMA address translation hardware).
 *
 * XXX This doesn't do the right thing yet.  It appears we would have
 * to add additional zones so we can implement the various address
 * mask constraints that we might encounter.  A zone for memory < 32
 * bits is obviously necessary...
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>

#include <asm/io.h>

/* Pure 2^n version of get_order */
extern __inline__ unsigned long
get_order (unsigned long size)
{
	unsigned long order = ia64_fls(size - 1) + 1;

	printk ("get_order: size=%lu, order=%lu\n", size, order);

	if (order > PAGE_SHIFT)
		order -= PAGE_SHIFT;
	else
		order = 0;
	return order;
}

void *
pci_alloc_consistent (struct pci_dev *hwdev, size_t size, dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;

	if (!hwdev || hwdev->dma_mask != 0xffffffff)
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}

void
pci_free_consistent (struct pci_dev *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long) vaddr, get_order(size));
}
