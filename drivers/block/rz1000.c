/*
 *  linux/drivers/block/rz1000.c	Version 0.05  December 8, 1997
 *
 *  Copyright (C) 1995-1998  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author/Maintainer:  mlord@pobox.com (Mark Lord)
 *
 *  This file provides support for disabling the buggy read-ahead
 *  mode of the RZ1000 IDE chipset, commonly used on Intel motherboards.
 *
 *  Dunno if this fixes both ports, or only the primary port (?).
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <asm/io.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include "ide.h"

#ifdef CONFIG_BLK_DEV_IDEPCI

__initfunc(void ide_init_rz1000 (ide_hwif_t *hwif))	/* called from ide-pci.c */
{
	unsigned short reg;

	hwif->chipset = ide_rz1000;
	if (!pcibios_read_config_word (hwif->pci_bus, hwif->pci_fn, 0x40, &reg)
	 && !pcibios_write_config_word(hwif->pci_bus, hwif->pci_fn, 0x40, reg & 0xdfff))
	{
		printk("%s: disabled chipset read-ahead (buggy RZ1000/RZ1001)\n", hwif->name);
	} else {
		hwif->serialized = 1;
		hwif->drives[0].no_unmask = 1;
		hwif->drives[1].no_unmask = 1;
		printk("%s: serialized, disabled unmasking (buggy RZ1000/RZ1001)\n", hwif->name);
	}
}

#else

__initfunc(static void init_rz1000 (byte bus, byte fn, const char *name))
{
	unsigned short reg, h;

	if (!pcibios_read_config_word (bus, fn, PCI_COMMAND, &reg) && !(reg & 1)) {
		printk("%s: buggy IDE controller disabled (BIOS)\n", name);
		return;
	}
	if (!pcibios_read_config_word (bus, fn, 0x40, &reg)
	 && !pcibios_write_config_word(bus, fn, 0x40, reg & 0xdfff))
	{
		printk("IDE: disabled chipset read-ahead (buggy %s)\n", name);
	} else {
		for (h = 0; h < MAX_HWIFS; ++h) {
			ide_hwif_t *hwif = &ide_hwifs[h];
			if ((hwif->io_ports[IDE_DATA_OFFSET] == 0x1f0 || hwif->io_ports[IDE_DATA_OFFSET] == 0x170)
			 && (hwif->chipset == ide_unknown || hwif->chipset == ide_generic))
			{
				hwif->chipset = ide_rz1000;
				hwif->serialized = 1;
				hwif->drives[0].no_unmask = 1;
				hwif->drives[1].no_unmask = 1;
				if (hwif->io_ports[IDE_DATA_OFFSET] == 0x170)
					hwif->channel = 1;
				printk("%s: serialized, disabled unmasking (buggy %s)\n", hwif->name, name);
			}
		}
	}
}

__initfunc(void ide_probe_for_rz100x (void))	/* called from ide.c */
{
	byte index, bus, fn;

	for (index = 0; !pcibios_find_device (PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000, index, &bus, &fn); ++index)
		init_rz1000 (bus, fn, "RZ1000");
	for (index = 0; !pcibios_find_device (PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001, index, &bus, &fn); ++index)
		init_rz1000 (bus, fn, "RZ1001");
}

#endif CONFIG_BLK_DEV_IDEPCI
