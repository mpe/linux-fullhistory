#ifndef __SPARC64_PCI_H
#define __SPARC64_PCI_H

#include <asm/scatterlist.h>

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0

#define PCIBIOS_MIN_IO		0UL
#define PCIBIOS_MIN_MEM		0UL

struct pci_dev;

/* Allocate and map kernel buffer using consistant mode DMA for PCI device.
 * Returns non-NULL cpu-view pointer to the buffer if successful and
 * sets *dma_addrp to the pci side dma address as well, else *dma_addrp
 * is undefined.
 */
extern void *pci_alloc_consistant(struct pci_dev *pdev, long size, u32 *dma_addrp);

/* Free and unmap a consistant DMA buffer.
 * cpu_addr is what was returned from pci_alloc_consistant,
 * size must be the same as what as passed into pci_alloc_consistant,
 * and likewise dma_addr must be the same as what *dma_addrp was set to.
 *
 * References to the memory and mappings assosciated with cpu_addr/dma_addr
 * past this call are illegal.
 */
extern void pci_free_consistant(struct pci_dev *pdev, long size, void *cpu_addr, u32 dma_addr);

/* Map a single buffer of the indicate size for PCI DMA in streaming mode.
 * The 32-bit PCI bus mastering address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory
 * until either pci_unmap_single or pci_sync_single is performed.
 */
extern u32 pci_map_single(struct pci_dev *pdev, void *buffer, long size);

/* Unmap a single streaming mode DMA translation.  The dma_addr and size
 * must match what was provided for in a previous pci_map_single call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guarenteed to see
 * whatever the device wrote there.
 */
extern void pci_unmap_single(struct pci_dev *pdev, u32 dma_addr, long size);

/* Map a set of buffers described by scatterlist in streaming
 * mode for PCI DMA.  This is the scather-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate PCI dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *	 The routine returns the number of addr/length pairs actually
 *	 used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
extern int pci_map_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents);

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
extern void pci_unmap_sg(struct pci_dev *pdev, struct scatterlist *sg, int nents);

/* Make physical memory consistant for a single
 * streaming mode DMA translation after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, the
 * device again owns the buffer.
 */
extern void pci_dma_sync_single(struct pci_dev *, u32, long);

/* Make physical memory consistant for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as pci_dma_sync_single but for a scatter-gather list,
 * same rules and usage.
 */
extern void pci_dma_sync_sg(struct pci_dev *, struct scatterlist *, int);

#endif /* __SPARC64_PCI_H */
