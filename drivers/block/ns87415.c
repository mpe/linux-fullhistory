/*
 * linux/drivers/block/ns87415.c	Version 1.00  December 7, 1997
 *
 * Copyright (C) 1997-1998  Mark Lord
 * Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 *
 * Inspired by an earlier effort from David S. Miller (davem@caipfs.rutgers.edu)
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


static unsigned int ns87415_count = 0, ns87415_control[MAX_HWIFS] = { 0 };

/*
 * This routine either enables/disables (according to drive->present)
 * the IRQ associated with the port (HWIF(drive)),
 * and selects either PIO or DMA handshaking for the next I/O operation.
 */
static void ns87415_prepare_drive (ide_drive_t *drive, unsigned int use_dma)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int bit, new, *old = (unsigned int *) hwif->select_data;
	struct pci_dev *dev = hwif->pci_dev;
	unsigned long flags;

	save_flags(flags); cli();
	new = *old;

	/* adjust IRQ enable bit */
	bit = 1 << (8 + hwif->channel);
	new = drive->present ? (new & ~bit) : (new | bit);

	/* select PIO or DMA */
	bit = 1 << (20 + drive->select.b.unit + (hwif->channel << 1));
	new = use_dma ? (new | bit) : (new & ~bit);

	if (new != *old) {
		if (use_dma) {
			bit = (1 << (5 + drive->select.b.unit));
			outb((inb(hwif->dma_base+2) & 0x60) | bit,
			     hwif->dma_base+2);
		}

		*old = new;
		(void) pci_write_config_dword(dev, 0x40, new);
	}
	restore_flags(flags);
}

static void ns87415_selectproc (ide_drive_t *drive)
{
	ns87415_prepare_drive (drive, drive->using_dma);
}

static int ns87415_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	switch (func) {
		case ide_dma_end: /* returns 1 on error, 0 otherwise */
		{
			byte dma_stat = inb(hwif->dma_base+2);
			int rc = (dma_stat & 7) != 4;
			/* from errata: stop DMA, clear INTR & ERROR */
			outb(7, hwif->dma_base);
			/* clear the INTR & ERROR bits */
			outb(dma_stat|6, hwif->dma_base+2);
			/* verify good DMA status */
			return rc;
		}
		case ide_dma_write:
		case ide_dma_read:
			/* select DMA xfer */
			ns87415_prepare_drive(drive, 1);
			/* use standard DMA stuff */
			if (!ide_dmaproc(func, drive))
				return 0;
			/* DMA failed: select PIO xfer */
			ns87415_prepare_drive(drive, 0);
			return 1;
		default:
			/* use standard DMA stuff */
			return ide_dmaproc(func, drive);
	}
}

__initfunc(void ide_init_ns87415 (ide_hwif_t *hwif))
{
	struct pci_dev *dev = hwif->pci_dev;
	unsigned int ctrl, using_inta;
	byte progif, stat;
	int timeout;

	/*
	 * We cannot probe for IRQ: both ports share common IRQ on INTA.
	 * Also, leave IRQ masked during drive probing, to prevent infinite
	 * interrupts from a potentially floating INTA..
	 *
	 * IRQs get unmasked in selectproc when drive is first used.
	 */
	(void) pci_read_config_dword(dev, 0x40, &ctrl);
	(void) pci_read_config_byte(dev, 0x09, &progif);
	/* is irq in "native" mode? */
	using_inta = progif & (1 << (hwif->channel << 1));
	if (!using_inta)
		using_inta = ctrl & (1 << (4 + hwif->channel));
	if (hwif->mate) {
		hwif->select_data = hwif->mate->select_data;
	} else {
		hwif->select_data = (unsigned long)
					&ns87415_control[ns87415_count++];
		ctrl |= (1 << 8) | (1 << 9);	/* mask both IRQs */
		if (using_inta)
			ctrl &= ~(1 << 6);	/* unmask INTA */
		*((unsigned int *)hwif->select_data) = ctrl;
		(void) pci_write_config_dword(dev, 0x40, ctrl);

		/*
		 * Set prefetch size to 512 bytes for both ports,
		 * but don't turn on/off prefetching here.
		 */
		pci_write_config_byte(dev, 0x55, 0xee);

#ifdef __sparc_v9__
		/*
		 * XXX: Reset the device, if we don't it will not respond
		 *      to SELECT_DRIVE() properly during first probe_hwif().
		 */
		timeout = 10000;
		outb(12, hwif->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		outb(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
		do {
			udelay(50);
			stat = inb(hwif->io_ports[IDE_STATUS_OFFSET]);
                	if (stat == 0xff)
                        	break;
        	} while ((stat & BUSY_STAT) && --timeout);
#endif
	}
	if (!using_inta)
		hwif->irq = hwif->channel ? 15 : 14;	/* legacy mode */
	else if (!hwif->irq && hwif->mate && hwif->mate->irq)
		hwif->irq = hwif->mate->irq;	/* share IRQ with mate */

	hwif->dmaproc = &ns87415_dmaproc;
	hwif->selectproc = &ns87415_selectproc;
}
