/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2002-2005 Silicon Graphics, Inc. All rights reserved.
 *
 * Routines for PCI DMA mapping.  See Documentation/DMA-API.txt for
 * a description of how these routines should be used.
 */

#include <linux/module.h>
#include <asm/dma.h>
#include <asm/sn/sn_sal.h>
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/pcibr_provider.h"

#define SG_ENT_VIRT_ADDRESS(sg)	(page_address((sg)->page) + (sg)->offset)
#define SG_ENT_PHYS_ADDRESS(SG)	virt_to_phys(SG_ENT_VIRT_ADDRESS(SG))

/**
 * sn_dma_supported - test a DMA mask
 * @dev: device to test
 * @mask: DMA mask to test
 *
 * Return whether the given PCI device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during PCI bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.  Of course, SN only supports devices that have 32 or more
 * address bits when using the PMU.
 */
int sn_dma_supported(struct device *dev, u64 mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	if (mask < 0x7fffffff)
		return 0;
	return 1;
}
EXPORT_SYMBOL(sn_dma_supported);

/**
 * sn_dma_set_mask - set the DMA mask
 * @dev: device to set
 * @dma_mask: new mask
 *
 * Set @dev's DMA mask if the hw supports it.
 */
int sn_dma_set_mask(struct device *dev, u64 dma_mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	if (!sn_dma_supported(dev, dma_mask))
		return 0;

	*dev->dma_mask = dma_mask;
	return 1;
}
EXPORT_SYMBOL(sn_dma_set_mask);

/**
 * sn_dma_alloc_coherent - allocate memory for coherent DMA
 * @dev: device to allocate for
 * @size: size of the region
 * @dma_handle: DMA (bus) address
 * @flags: memory allocation flags
 *
 * dma_alloc_coherent() returns a pointer to a memory region suitable for
 * coherent DMA traffic to/from a PCI device.  On SN platforms, this means
 * that @dma_handle will have the %PCIIO_DMA_CMD flag set.
 *
 * This interface is usually used for "command" streams (e.g. the command
 * queue for a SCSI controller).  See Documentation/DMA-API.txt for
 * more information.
 */
void *sn_dma_alloc_coherent(struct device *dev, size_t size,
			    dma_addr_t * dma_handle, int flags)
{
	void *cpuaddr;
	unsigned long phys_addr;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(to_pci_dev(dev));

	BUG_ON(dev->bus != &pci_bus_type);

	/*
	 * Allocate the memory.
	 * FIXME: We should be doing alloc_pages_node for the node closest
	 *        to the PCI device.
	 */
	if (!(cpuaddr = (void *)__get_free_pages(GFP_ATOMIC, get_order(size))))
		return NULL;

	memset(cpuaddr, 0x0, size);

	/* physical addr. of the memory we just got */
	phys_addr = __pa(cpuaddr);

	/*
	 * 64 bit address translations should never fail.
	 * 32 bit translations can fail if there are insufficient mapping
	 * resources.
	 */

	*dma_handle = pcibr_dma_map(pcidev_info, phys_addr, size,
				    SN_PCIDMA_CONSISTENT);
	if (!*dma_handle) {
		printk(KERN_ERR "%s: out of ATEs\n", __FUNCTION__);
		free_pages((unsigned long)cpuaddr, get_order(size));
		return NULL;
	}

	return cpuaddr;
}
EXPORT_SYMBOL(sn_dma_alloc_coherent);

/**
 * sn_pci_free_coherent - free memory associated with coherent DMAable region
 * @dev: device to free for
 * @size: size to free
 * @cpu_addr: kernel virtual address to free
 * @dma_handle: DMA address associated with this region
 *
 * Frees the memory allocated by dma_alloc_coherent(), potentially unmapping
 * any associated IOMMU mappings.
 */
void sn_dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
			  dma_addr_t dma_handle)
{
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(to_pci_dev(dev));

	BUG_ON(dev->bus != &pci_bus_type);

	pcibr_dma_unmap(pcidev_info, dma_handle, 0);
	free_pages((unsigned long)cpu_addr, get_order(size));
}
EXPORT_SYMBOL(sn_dma_free_coherent);

/**
 * sn_dma_map_single - map a single page for DMA
 * @dev: device to map for
 * @cpu_addr: kernel virtual address of the region to map
 * @size: size of the region
 * @direction: DMA direction
 *
 * Map the region pointed to by @cpu_addr for DMA and return the
 * DMA address.
 *
 * We map this to the one step pcibr_dmamap_trans interface rather than
 * the two step pcibr_dmamap_alloc/pcibr_dmamap_addr because we have
 * no way of saving the dmamap handle from the alloc to later free
 * (which is pretty much unacceptable).
 *
 * TODO: simplify our interface;
 *       figure out how to save dmamap handle so can use two step.
 */
dma_addr_t sn_dma_map_single(struct device *dev, void *cpu_addr, size_t size,
			     int direction)
{
	dma_addr_t dma_addr;
	unsigned long phys_addr;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(to_pci_dev(dev));

	BUG_ON(dev->bus != &pci_bus_type);

	phys_addr = __pa(cpu_addr);
	dma_addr = pcibr_dma_map(pcidev_info, phys_addr, size, 0);
	if (!dma_addr) {
		printk(KERN_ERR "%s: out of ATEs\n", __FUNCTION__);
		return 0;
	}
	return dma_addr;
}
EXPORT_SYMBOL(sn_dma_map_single);

/**
 * sn_dma_unmap_single - unamp a DMA mapped page
 * @dev: device to sync
 * @dma_addr: DMA address to sync
 * @size: size of region
 * @direction: DMA direction
 *
 * This routine is supposed to sync the DMA region specified
 * by @dma_handle into the coherence domain.  On SN, we're always cache
 * coherent, so we just need to free any ATEs associated with this mapping.
 */
void sn_dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
			 int direction)
{
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(to_pci_dev(dev));

	BUG_ON(dev->bus != &pci_bus_type);
	pcibr_dma_unmap(pcidev_info, dma_addr, direction);
}
EXPORT_SYMBOL(sn_dma_unmap_single);

/**
 * sn_dma_unmap_sg - unmap a DMA scatterlist
 * @dev: device to unmap
 * @sg: scatterlist to unmap
 * @nhwentries: number of scatterlist entries
 * @direction: DMA direction
 *
 * Unmap a set of streaming mode DMA translations.
 */
void sn_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		     int nhwentries, int direction)
{
	int i;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(to_pci_dev(dev));

	BUG_ON(dev->bus != &pci_bus_type);

	for (i = 0; i < nhwentries; i++, sg++) {
		pcibr_dma_unmap(pcidev_info, sg->dma_address, direction);
		sg->dma_address = (dma_addr_t) NULL;
		sg->dma_length = 0;
	}
}
EXPORT_SYMBOL(sn_dma_unmap_sg);

/**
 * sn_dma_map_sg - map a scatterlist for DMA
 * @dev: device to map for
 * @sg: scatterlist to map
 * @nhwentries: number of entries
 * @direction: direction of the DMA transaction
 *
 * Maps each entry of @sg for DMA.
 */
int sn_dma_map_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		  int direction)
{
	unsigned long phys_addr;
	struct scatterlist *saved_sg = sg;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(to_pci_dev(dev));
	int i;

	BUG_ON(dev->bus != &pci_bus_type);

	/*
	 * Setup a DMA address for each entry in the scatterlist.
	 */
	for (i = 0; i < nhwentries; i++, sg++) {
		phys_addr = SG_ENT_PHYS_ADDRESS(sg);
		sg->dma_address = pcibr_dma_map(pcidev_info, phys_addr,
						sg->length, 0);

		if (!sg->dma_address) {
			printk(KERN_ERR "%s: out of ATEs\n", __FUNCTION__);

			/*
			 * Free any successfully allocated entries.
			 */
			if (i > 0)
				sn_dma_unmap_sg(dev, saved_sg, i, direction);
			return 0;
		}

		sg->dma_length = sg->length;
	}

	return nhwentries;
}
EXPORT_SYMBOL(sn_dma_map_sg);

void sn_dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
				size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_single_for_cpu);

void sn_dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				   size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_single_for_device);

void sn_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			    int nelems, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_sg_for_cpu);

void sn_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			       int nelems, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_sg_for_device);

int sn_dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}
EXPORT_SYMBOL(sn_dma_mapping_error);

char *sn_pci_get_legacy_mem(struct pci_bus *bus)
{
	if (!SN_PCIBUS_BUSSOFT(bus))
		return ERR_PTR(-ENODEV);

	return (char *)(SN_PCIBUS_BUSSOFT(bus)->bs_legacy_mem | __IA64_UNCACHED_OFFSET);
}

int sn_pci_legacy_read(struct pci_bus *bus, u16 port, u32 *val, u8 size)
{
	unsigned long addr;
	int ret;

	if (!SN_PCIBUS_BUSSOFT(bus))
		return -ENODEV;

	addr = SN_PCIBUS_BUSSOFT(bus)->bs_legacy_io | __IA64_UNCACHED_OFFSET;
	addr += port;

	ret = ia64_sn_probe_mem(addr, (long)size, (void *)val);

	if (ret == 2)
		return -EINVAL;

	if (ret == 1)
		*val = -1;

	return size;
}

int sn_pci_legacy_write(struct pci_bus *bus, u16 port, u32 val, u8 size)
{
	int ret = size;
	unsigned long paddr;
	unsigned long *addr;

	if (!SN_PCIBUS_BUSSOFT(bus)) {
		ret = -ENODEV;
		goto out;
	}

	/* Put the phys addr in uncached space */
	paddr = SN_PCIBUS_BUSSOFT(bus)->bs_legacy_io | __IA64_UNCACHED_OFFSET;
	paddr += port;
	addr = (unsigned long *)paddr;

	switch (size) {
	case 1:
		*(volatile u8 *)(addr) = (u8)(val);
		break;
	case 2:
		*(volatile u16 *)(addr) = (u16)(val);
		break;
	case 4:
		*(volatile u32 *)(addr) = (u32)(val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
 out:
	return ret;
}
