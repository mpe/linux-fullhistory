/*
 *  linux/drivers/block/rz1000.c	Version 0.03  Mar 20, 1996
 *
 *  Copyright (C) 1995-1996  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author/Maintainer:  mlord@pobox.com (Mark Lord)
 *
 *  This file provides support for disabling the buggy read-ahead
 *  mode of the RZ1000 IDE chipset, commonly used on Intel motherboards.
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

static void init_rz1000 (byte bus, byte fn, const char *name)
{
	unsigned short reg, h;

	printk("%s: buggy IDE controller: ", name);
	if (!pcibios_read_config_word (bus, fn, PCI_COMMAND, &reg) && !(reg & 1)) {
		printk("disabled (BIOS)\n");
		return;
	}
	if (!pcibios_read_config_word (bus, fn, 0x40, &reg)
	 && !pcibios_write_config_word(bus, fn, 0x40, reg & 0xdfff))
	{
		printk("disabled read-ahead\n");
	} else {
		printk("\n");
		for (h = 0; h < MAX_HWIFS; ++h) {
			ide_hwif_t *hwif = &ide_hwifs[h];
			if ((hwif->io_ports[IDE_DATA_OFFSET] == 0x1f0 || hwif->io_ports[IDE_DATA_OFFSET] == 0x170)
			 && (hwif->chipset == ide_unknown || hwif->chipset == ide_generic))
			{
				hwif->chipset = ide_rz1000;
				hwif->serialized = 1;
				hwif->drives[0].no_unmask = 1;
				hwif->drives[1].no_unmask = 1;
				printk("  %s: serialized, disabled unmasking\n", hwif->name);
			}
		}
	}
}

void ide_probe_for_rz100x (void)
{
	byte index, bus, fn;

	for (index = 0; !pcibios_find_device (PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000, index, &bus, &fn); ++index)
		init_rz1000 (bus, fn, "RZ1000");
	for (index = 0; !pcibios_find_device (PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001, index, &bus, &fn); ++index)
		init_rz1000 (bus, fn, "RZ1001");
}
