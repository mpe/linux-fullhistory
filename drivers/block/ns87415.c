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
	unsigned int bit, other, new, *old = (unsigned int *) hwif->select_data;
	struct pci_dev *dev = hwif->pci_dev;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	new = *old;

	/* Adjust IRQ enable bit */
	bit = 1 << (8 + hwif->channel);
	new = drive->present ? (new & ~bit) : (new | bit);

	/* Select PIO or DMA, DMA may only be selected for one drive/channel. */
	bit   = 1 << (20 + drive->select.b.unit       + (hwif->channel << 1));
	other = 1 << (20 + (1 - drive->select.b.unit) + (hwif->channel << 1));
	new = use_dma ? ((new & ~other) | bit) : (new & ~bit);

	if (new != *old) {
		*old = new;
		(void) pci_write_config_dword(dev, 0x40, new);
	}

	__restore_flags(flags);	/* local CPU only */
}

static void ns87415_selectproc (ide_drive_t *drive)
{
	ns87415_prepare_drive (drive, drive->using_dma);
}

static int ns87415_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t	*hwif = HWIF(drive);
	byte		dma_stat;

	switch (func) {
		case ide_dma_end: /* returns 1 on error, 0 otherwise */
			drive->waiting_for_dma = 0;
			dma_stat = inb(hwif->dma_base+2);
			outb(inb(hwif->dma_base)&~1, hwif->dma_base);	/* stop DMA */
			outb(inb(hwif->dma_base)|6, hwif->dma_base);	/* from ERRATA: clear the INTR & ERROR bits */
			return (dma_stat & 7) != 4;		/* verify good DMA status */
		case ide_dma_write:
		case ide_dma_read:
			ns87415_prepare_drive(drive, 1);	/* select DMA xfer */
			if (!ide_dmaproc(func, drive))		/* use standard DMA stuff */
				return 0;
			ns87415_prepare_drive(drive, 0);	/* DMA failed: select PIO xfer */
			return 1;
		default:
			return ide_dmaproc(func, drive);	/* use standard DMA stuff */
	}
}

__initfunc(void ide_init_ns87415 (ide_hwif_t *hwif))
{
	struct pci_dev *dev = hwif->pci_dev;
	unsigned int ctrl, using_inta;
	byte progif;
#ifdef __sparc_v9__
	int timeout;
	byte stat;
#endif

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

	outb(0x60, hwif->dma_base + 2);

	if (!using_inta)
		hwif->irq = hwif->channel ? 15 : 14;	/* legacy mode */
	else if (!hwif->irq && hwif->mate && hwif->mate->irq)
		hwif->irq = hwif->mate->irq;	/* share IRQ with mate */

	hwif->dmaproc = &ns87415_dmaproc;
	hwif->selectproc = &ns87415_selectproc;
}
