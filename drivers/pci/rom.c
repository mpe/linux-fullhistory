/*
 * drivers/pci/rom.c
 *
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 *
 * PCI ROM access routines
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "pci.h"

/**
 * pci_enable_rom - enable ROM decoding for a PCI device
 * @dev: PCI device to enable
 *
 * Enable ROM decoding on @dev.  This involves simply turning on the last
 * bit of the PCI ROM BAR.  Note that some cards may share address decoders
 * between the ROM and other resources, so enabling it may disable access
 * to MMIO registers or other card memory.
 */
static void pci_enable_rom(struct pci_dev *pdev)
{
	u32 rom_addr;

	pci_read_config_dword(pdev, pdev->rom_base_reg, &rom_addr);
	rom_addr |= PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, pdev->rom_base_reg, rom_addr);
}

/**
 * pci_disable_rom - disable ROM decoding for a PCI device
 * @dev: PCI device to disable
 *
 * Disable ROM decoding on a PCI device by turning off the last bit in the
 * ROM BAR.
 */
static void pci_disable_rom(struct pci_dev *pdev)
{
	u32 rom_addr;
	pci_read_config_dword(pdev, pdev->rom_base_reg, &rom_addr);
	rom_addr &= ~PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, pdev->rom_base_reg, rom_addr);
}

/**
 * pci_map_rom - map a PCI ROM to kernel space
 * @dev: pointer to pci device struct
 * @size: pointer to receive size of pci window over ROM
 * @return: kernel virtual pointer to image of ROM
 *
 * Map a PCI ROM into kernel space. If ROM is boot video ROM,
 * the shadow BIOS copy will be returned instead of the
 * actual ROM.
 */
void __iomem *pci_map_rom(struct pci_dev *pdev, size_t *size)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	loff_t start;
	void __iomem *rom;
	void __iomem *image;
	int last_image;

	/* IORESOURCE_ROM_SHADOW only set on x86 */
	if (res->flags & IORESOURCE_ROM_SHADOW) {
		/* primary video rom always starts here */
		start = (loff_t)0xC0000;
		*size = 0x20000; /* cover C000:0 through E000:0 */
	} else {
		if (res->flags & IORESOURCE_ROM_COPY) {
			*size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
			return (void __iomem *)pci_resource_start(pdev, PCI_ROM_RESOURCE);
		} else {
			/* assign the ROM an address if it doesn't have one */
			if (res->parent == NULL)
				pci_assign_resource(pdev, PCI_ROM_RESOURCE);

			start = pci_resource_start(pdev, PCI_ROM_RESOURCE);
			*size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
			if (*size == 0)
				return NULL;

			/* Enable ROM space decodes */
			pci_enable_rom(pdev);
		}
	}

	rom = ioremap(start, *size);
	if (!rom) {
		/* restore enable if ioremap fails */
		if (!(res->flags & (IORESOURCE_ROM_ENABLE |
				    IORESOURCE_ROM_SHADOW |
				    IORESOURCE_ROM_COPY)))
			pci_disable_rom(pdev);
		return NULL;
	}

	/*
	 * Try to find the true size of the ROM since sometimes the PCI window
	 * size is much larger than the actual size of the ROM.
	 * True size is important if the ROM is going to be copied.
	 */
	image = rom;
	do {
		void __iomem *pds;
		/* Standard PCI ROMs start out with these bytes 55 AA */
		if (readb(image) != 0x55)
			break;
		if (readb(image + 1) != 0xAA)
			break;
		/* get the PCI data structure and check its signature */
		pds = image + readw(image + 24);
		if (readb(pds) != 'P')
			break;
		if (readb(pds + 1) != 'C')
			break;
		if (readb(pds + 2) != 'I')
			break;
		if (readb(pds + 3) != 'R')
			break;
		last_image = readb(pds + 21) & 0x80;
		/* this length is reliable */
		image += readw(pds + 16) * 512;
	} while (!last_image);

	*size = image - rom;

	return rom;
}

/**
 * pci_map_rom_copy - map a PCI ROM to kernel space, create a copy
 * @dev: pointer to pci device struct
 * @size: pointer to receive size of pci window over ROM
 * @return: kernel virtual pointer to image of ROM
 *
 * Map a PCI ROM into kernel space. If ROM is boot video ROM,
 * the shadow BIOS copy will be returned instead of the
 * actual ROM.
 */
void __iomem *pci_map_rom_copy(struct pci_dev *pdev, size_t *size)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	void __iomem *rom;

	rom = pci_map_rom(pdev, size);
	if (!rom)
		return NULL;

	if (res->flags & (IORESOURCE_ROM_COPY | IORESOURCE_ROM_SHADOW))
		return rom;

	res->start = (unsigned long)kmalloc(*size, GFP_KERNEL);
	if (!res->start)
		return rom;

	res->end = res->start + *size;
	memcpy_fromio((void*)res->start, rom, *size);
	pci_unmap_rom(pdev, rom);
	res->flags |= IORESOURCE_ROM_COPY;

	return (void __iomem *)res->start;
}

/**
 * pci_unmap_rom - unmap the ROM from kernel space
 * @dev: pointer to pci device struct
 * @rom: virtual address of the previous mapping
 *
 * Remove a mapping of a previously mapped ROM
 */
void pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];

	if (res->flags & IORESOURCE_ROM_COPY)
		return;

	iounmap(rom);

	/* Disable again before continuing, leave enabled if pci=rom */
	if (!(res->flags & (IORESOURCE_ROM_ENABLE | IORESOURCE_ROM_SHADOW)))
		pci_disable_rom(pdev);
}

/**
 * pci_remove_rom - disable the ROM and remove its sysfs attribute
 * @dev: pointer to pci device struct
 *
 * Remove the rom file in sysfs and disable ROM decoding.
 */
void pci_remove_rom(struct pci_dev *pdev)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];

	if (pci_resource_len(pdev, PCI_ROM_RESOURCE))
		sysfs_remove_bin_file(&pdev->dev.kobj, pdev->rom_attr);
	if (!(res->flags & (IORESOURCE_ROM_ENABLE |
			    IORESOURCE_ROM_SHADOW |
			    IORESOURCE_ROM_COPY)))
		pci_disable_rom(pdev);
}

/**
 * pci_cleanup_rom - internal routine for freeing the ROM copy created
 * by pci_map_rom_copy called from remove.c
 * @dev: pointer to pci device struct
 *
 * Free the copied ROM if we allocated one.
 */
void pci_cleanup_rom(struct pci_dev *pdev)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	if (res->flags & IORESOURCE_ROM_COPY) {
		kfree((void*)res->start);
		res->flags &= ~IORESOURCE_ROM_COPY;
		res->start = 0;
		res->end = 0;
	}
}

EXPORT_SYMBOL(pci_map_rom);
EXPORT_SYMBOL(pci_map_rom_copy);
EXPORT_SYMBOL(pci_unmap_rom);
EXPORT_SYMBOL(pci_remove_rom);
