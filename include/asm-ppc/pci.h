#ifndef __PPC_PCI_H
#define __PPC_PCI_H

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

/* Dynamic DMA Mapping stuff
 * 	++ajoshi
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

struct pci_dev;

extern void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
				  dma_addr_t *dma_handle);
extern void pci_free_consistent(struct pci_dev *hwdev, size_t size,
				void *vaddr, dma_addr_t dma_handle);
extern inline dma_addr_t pci_map_single(struct pci_dev *hwdev, void *ptr,
					size_t size)
{
	return virt_to_bus(ptr);
}
extern inline void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
				    size_t size)
{
	/* nothing to do */
}
extern inline int pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg,
			     int nents)
{
	return nents;
}
extern inline void pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg,
				int nents)
{
	/* nothing to do */
}
extern inline void pci_dma_sync_single(struct pci_dev *hwdev,
				       dma_addr_t dma_handle,
				       size_t size)
{
	/* nothing to do */
}
extern inline void pci_dma_syng_sg(struct pci_dev *hwdev,
				   struct scatterlist *sg,
				   int nelems)
{
	/* nothing to do */
}

#define sg_dma_address(sg)	(virt_to_bus((sg)->address))
#define sg_dma_len(sg)		((sg)->length)

#endif /* __PPC_PCI_H */
