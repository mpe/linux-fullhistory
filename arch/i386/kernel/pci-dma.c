/*
 * Dynamic DMA mapping support.
 *
 * On i386 there is no hardware dynamic DMA address translation,
 * so consistent alloc/free are merely page allocation/freeing.
 * The rest of the dynamic DMA mapping interface is implemented
 * in asm/pci.h.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/io.h>

/* Pure 2^n version of get_order */
extern __inline__ int __get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;

	if (hwdev == NULL || hwdev->dma_mask != 0xffffffff)
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, __get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}

void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, __get_order(size));
}
