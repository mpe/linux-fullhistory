#ifndef __SPARC64_PCI_H
#define __SPARC64_PCI_H

#include <asm/scatterlist.h>

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0

/* Map kernel buffer using consistant mode DMA for PCI device.
 * Returns a 32-bit PCI DMA address.
 */
extern u32 pci_map_consistant(struct pci_dev *, void *, int);

/* Unmap a consistant DMA translation. */
extern void pci_unmap_consistant(struct pci_dev *, u32, int);

/* Map a single buffer for PCI DMA in streaming mode. */
extern u32 pci_map_single(struct pci_dev *, void *, int);

/* Unmap a single streaming mode DMA translation. */
extern void pci_unmap_single(struct pci_dev *, u32, int);

/* Map a set of buffers described by scatterlist in streaming
 * mode for PCI DMA.
 */
extern void pci_map_sg(struct pci_dev *, struct scatterlist *, int);

/* Unmap a set of streaming mode DMA translations. */
extern void pci_unmap_sg(struct pci_dev *, struct scatterlist *, int);

/* Make physical memory consistant for a single
 * streaming mode DMA translation after a transfer.
 */
extern void pci_dma_sync_single(struct pci_dev *, u32, int);

/* Make physical memory consistant for a set of streaming
 * mode DMA translations after a transfer.
 */
extern void pci_dma_sync_sg(struct pci_dev *, struct scatterlist *, int);

#endif /* __SPARC64_PCI_H */
