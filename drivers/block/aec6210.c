/*
 * linux/drivers/block/aec6210.c		Version 0.01	Nov 17, 1998
 *
 * Copyright (C) 1998-99	Andre Hedrick
 *
 *  pio 0 ::       40: 00 07 00 00 00 00 00 00 02 07 a6 04 00 02 00 02
 *  pio 1 ::       40: 0a 07 00 00 00 00 00 00 02 07 a6 05 00 02 00 02
 *  pio 2 ::       40: 08 07 00 00 00 00 00 00 02 07 a6 05 00 02 00 02
 *  pio 3 ::       40: 03 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *  pio 4 ::       40: 01 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *  dma 0 ::       40: 0a 07 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *  dma 1 ::       40: 02 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *  dma 2 ::       40: 01 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *                 50: ff ff ff ff 00 06 04 00 00 00 00 00 00 00 00 00
 *
 * udma 0 ::       40: 01 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *                 50: ff ff ff ff 01 06 04 00 00 00 00 00 00 00 00 00
 *
 * udma 1 ::       40: 01 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *                 50: ff ff ff ff 01 06 04 00 00 00 00 00 00 00 00 00
 *
 * udma 2 ::       40: 01 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *                 50: ff ff ff ff 02 06 04 00 00 00 00 00 00 00 00 00
 *
 * auto   ::       40: 01 04 00 00 00 00 00 00 02 05 a6 05 00 02 00 02
 *                 50: ff ff ff ff 02 06 04 00 00 00 00 00 00 00 00 00
 *
 * auto   ::       40: 01 04 01 04 01 04 01 04 02 05 a6 cf 00 02 00 02
 *                 50: ff ff ff ff aa 06 04 00 00 00 00 00 00 00 00 00
 *
 *                 NO-Devices
 *                 40: 00 00 00 00 00 00 00 00 02 05 a6 00 00 02 00 02
 *                 50: ff ff ff ff 00 06 00 00 00 00 00 00 00 00 00 00
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

unsigned int __init pci_init_aec6210 (struct pci_dev *dev, const char *name)
{
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", name, dev->resource[PCI_ROM_RESOURCE].start);
	}
	return dev->irq;
}
