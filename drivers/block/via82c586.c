/*
 * linux/drivers/block/via82c586.c	Version 0.01	Aug 16, 1998
 *
 *  Copyright (C) 1998 Michel Aubry
 *  Copyright (C) 1998 Andre Hedrick
 *
 *  The VIA MVP-3 is reported OK with UDMA.
 *
 *  VIA chips also have a single FIFO, with the same 64 bytes deep buffer (16 levels 
 *  of 4 bytes each).
 *  However, VIA chips can have the buffer split either 8:8 levels, 16:0 levels or 
 *  0:16 levels between both channels. One could think of using this feature, as even
 *  if no level of FIFO is given to a given channel, one can always reach ATAPI drives 
 *  through it, or, if one channel is unused, configuration defaults to an even split 
 *  FIFO levels.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "ide.h"

/*
 *  Set VIA Chipset Timings for (U)DMA modes enabled.
 *
 *  VIA Apollo chipset has complete support for
 *  setting up the timing parameters.
 */
static void set_via_timings (ide_hwif_t *hwif)
{
	struct pci_dev  *dev = hwif->pci_dev;
	byte post  = hwif->channel ? 0xc0 : 0x30;
	byte flush = hwif->channel ? 0xa0 : 0x50;
	byte via_config = 0;
	int rc = 0, errors = 0;

	printk("%s: VIA Bus-Master ", hwif->name);

	if (!hwif->dma_base) {
		printk(" ERROR, NO DMA_BASE\n");
		return;
	}

	/* setting IDE read prefetch buffer and IDE post write buffer.
	 * (This feature allows prefetched reads and post writes).
	 */
	if ((rc = pci_read_config_byte(dev, 0x41, &via_config))) {
		errors++;
		goto via_error;
	}
	if ((rc = pci_write_config_byte(dev, 0x41, via_config | post))) {
		errors++;
		goto via_error;
	}

	/* setting Channel read and End-of-sector FIFO flush.
	 * (This feature ensures that FIFO flush is enabled:
         *  - for read DMA when interrupt asserts the given channel.
         *  - at the end of each sector for the given channel.)
	 */
	if ((rc = pci_read_config_byte(dev, 0x46, &via_config))) {
		errors++;
		goto via_error;
	}
        if ((rc = pci_write_config_byte(dev, 0x46, via_config | flush))) {
		errors++;
		goto via_error;
	}

via_error:
	printk("(U)DMA Timing Config %s\n", errors ? "ERROR" : "Success");
}

void ide_init_via82c586 (ide_hwif_t *hwif)
{
	set_via_timings(hwif);
}

