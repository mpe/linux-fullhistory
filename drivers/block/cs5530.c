/*
 * linux/drivers/block/cs5530.c		Version 0.2	Jan 30, 2000
 *
 * Copyright (C) 2000			Mark Lord <mlord@pobox.com>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Development of this chipset driver was funded
 * by the nice folks at National Semiconductor.
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
#include "ide_modes.h"

/*
 * Return the mode name for a drive transfer mode value:
 */
static const char *strmode (byte mode)
{
	switch (mode) {
		case XFER_UDMA_4:	return("UDMA4");
		case XFER_UDMA_3:	return("UDMA3");
		case XFER_UDMA_2:	return("UDMA2");
		case XFER_UDMA_1:	return("UDMA1");
		case XFER_UDMA_0:	return("UDMA0");
		case XFER_MW_DMA_2:	return("MDMA2");
		case XFER_MW_DMA_1:	return("MDMA1");
		case XFER_MW_DMA_0:	return("MDMA0");
		case XFER_SW_DMA_2:	return("SDMA2");
		case XFER_SW_DMA_1:	return("SDMA1");
		case XFER_SW_DMA_0:	return("SDMA0");
		case XFER_PIO_4:	return("PIO4");
		case XFER_PIO_3:	return("PIO3");
		case XFER_PIO_2:	return("PIO2");
		case XFER_PIO_1:	return("PIO1");
		case XFER_PIO_0:	return("PIO0");
		default:		return("???");
	}
}

/*
 * Set a new transfer mode at the drive
 */
int cs5530_set_xfer_mode (ide_drive_t *drive, byte mode)
{
	int		i, error = 1;
	byte		stat;
	ide_hwif_t	*hwif = HWIF(drive);

	printk("%s: cs5530_set_xfer_mode(%s)\n", drive->name, strmode(mode));
	/*
	 * If this is a DMA mode setting, then turn off all DMA bits.
	 * We will set one of them back on afterwards, if all goes well.
	 *
	 * Not sure why this is needed (it looks very silly),
	 * but other IDE chipset drivers also do this fiddling.  ???? -ml
 	 */
	switch (mode) {
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
	}

	/*
	 * Select the drive, and issue the SETFEATURES command
	 */
	disable_irq(hwif->irq);
	udelay(1);
	SELECT_DRIVE(HWIF(drive), drive);
	udelay(1);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl | 2, IDE_CONTROL_REG);
	OUT_BYTE(mode, IDE_NSECTOR_REG);
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);
	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */

	/*
	 * Wait for drive to become non-BUSY
	 */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		unsigned long flags, timeout;
		__save_flags(flags);	/* local CPU only */
		ide__sti();		/* local CPU only -- for jiffies */
		timeout = jiffies + WAIT_CMD;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (0 < (signed long)(jiffies - timeout))
				break;
		}
		__restore_flags(flags); /* local CPU only */
	}

	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), DRIVE_READY, BUSY_STAT|DRQ_STAT|ERR_STAT)) {
			error = 0;
			break;
		}
	}
	enable_irq(hwif->irq);

	/*
	 * Turn dma bit on if all is okay
	 */
	if (error) {
		(void) ide_dump_status(drive, "cs5530_set_xfer_mode", stat);
	} else {
		switch (mode) {
			case XFER_UDMA_4:   drive->id->dma_ultra |= 0x1010; break;
			case XFER_UDMA_3:   drive->id->dma_ultra |= 0x0808; break;
			case XFER_UDMA_2:   drive->id->dma_ultra |= 0x0404; break;
			case XFER_UDMA_1:   drive->id->dma_ultra |= 0x0202; break;
			case XFER_UDMA_0:   drive->id->dma_ultra |= 0x0101; break;
			case XFER_MW_DMA_2: drive->id->dma_mword |= 0x0404; break;
			case XFER_MW_DMA_1: drive->id->dma_mword |= 0x0202; break;
			case XFER_MW_DMA_0: drive->id->dma_mword |= 0x0101; break;
			case XFER_SW_DMA_2: drive->id->dma_1word |= 0x0404; break;
			case XFER_SW_DMA_1: drive->id->dma_1word |= 0x0202; break;
			case XFER_SW_DMA_0: drive->id->dma_1word |= 0x0101; break;
		}
	}
	return error;
}

/*
 * Here are the standard PIO mode 0-4 timings for each "format".
 * Format-0 uses fast data reg timings, with slower command reg timings.
 * Format-1 uses fast timings for all registers, but won't work with all drives.
 */
static unsigned int cs5530_pio_timings[2][5] =
	{{0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},
	 {0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010}};

/*
 * After chip reset, the PIO timings are set to 0x0000e132, which is not valid.
 */
#define CS5530_BAD_PIO(timings) (((timings)&~0x80000000)==0x0000e132)
#define CS5530_BASEREG(hwif)	(((hwif)->dma_base & ~0xf) + ((hwif)->channel ? 0x30 : 0x20))

/*
 * cs5530_tuneproc() handles selection/setting of PIO modes
 * for both the chipset and drive.
 *
 * The ide_init_cs5530() routine guarantees that all drives
 * will have valid default PIO timings set up before we get here.
 */
static void cs5530_tuneproc (ide_drive_t *drive, byte pio)	/* pio=255 means "autotune" */
{
	ide_hwif_t	*hwif = HWIF(drive);
	unsigned int	format, basereg = CS5530_BASEREG(hwif);
	static byte	modes[5] = {XFER_PIO_0, XFER_PIO_1, XFER_PIO_2, XFER_PIO_3, XFER_PIO_4};

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	if (!cs5530_set_xfer_mode(drive, modes[pio])) {
		format = (inl(basereg+4) >> 31) & 1;
		outl(cs5530_pio_timings[format][pio], basereg+(drive->select.b.unit<<3));
	}
}

/*
 * cs5530_config_dma() handles selection/setting of DMA/UDMA modes
 * for both the chipset and drive.
 */
static int cs5530_config_dma (ide_drive_t *drive)
{
	int			udma_ok = 1, mode = 0;
	ide_hwif_t		*hwif = HWIF(drive);
	int			unit = drive->select.b.unit;
	ide_drive_t		*mate = &hwif->drives[unit^1];
	struct hd_driveid	*id = drive->id;
	unsigned int		basereg, reg, timings;


	/*
	 * Default to DMA-off in case we run into trouble here.
	 */
	(void)hwif->dmaproc(ide_dma_off_quietly, drive);	/* turn off DMA while we fiddle */
	outb(inb(hwif->dma_base+2)&~(unit?0x40:0x20), hwif->dma_base+2); /* clear DMA_capable bit */

	/*
	 * The CS5530 specifies that two drives sharing a cable cannot
	 * mix UDMA/MDMA.  It has to be one or the other, for the pair,
	 * though different timings can still be chosen for each drive.
	 * We could set the appropriate timing bits on the fly,
	 * but that might be a bit confusing.  So, for now we statically
	 * handle this requirement by looking at our mate drive to see
	 * what it is capable of, before choosing a mode for our own drive.
	 */
	if (mate->present) {
		struct hd_driveid *mateid = mate->id;
		if (mateid && (mateid->capability & 1) && !hwif->dmaproc(ide_dma_bad_drive, mate)) {
			if ((mateid->field_valid & 4) && (mateid->dma_ultra & 7))
				udma_ok = 1;
			else if ((mateid->field_valid & 2) && (mateid->dma_mword & 7))
				udma_ok = 0;
			else
				udma_ok = 1;
		}
	}

	/*
	 * Now see what the current drive is capable of,
	 * selecting UDMA only if the mate said it was ok.
	 */
	if (id && (id->capability & 1) && hwif->autodma && !hwif->dmaproc(ide_dma_bad_drive, drive)) {
		if (udma_ok && (id->field_valid & 4) && (id->dma_ultra & 7)) {
			if      (id->dma_ultra & 4)
				mode = XFER_UDMA_2;
			else if (id->dma_ultra & 2)
				mode = XFER_UDMA_1;
			else if (id->dma_ultra & 1)
				mode = XFER_UDMA_0;
		}
		if (!mode && (id->field_valid & 2) && (id->dma_mword & 7)) {
			if      (id->dma_mword & 4)
				mode = XFER_MW_DMA_2;
			else if (id->dma_mword & 2)
				mode = XFER_MW_DMA_1;
			else if (id->dma_mword & 1)
				mode = XFER_MW_DMA_0;
		}
	}

	/*
	 * Tell the drive to switch to the new mode; abort on failure.
	 */
	if (!mode || cs5530_set_xfer_mode(drive, mode))
		return 1;	/* failure */

	/*
	 * Now tune the chipset to match the drive:
	 */
	switch (mode) {
		case XFER_UDMA_0:	timings = 0x00921250; break;
		case XFER_UDMA_1:	timings = 0x00911140; break;
		case XFER_UDMA_2:	timings = 0x00911030; break;
		case XFER_MW_DMA_0:	timings = 0x00077771; break;
		case XFER_MW_DMA_1:	timings = 0x00012121; break;
		case XFER_MW_DMA_2:	timings = 0x00002020; break;
		default:
			printk("%s: cs5530_config_dma: huh? mode=%02x\n", drive->name, mode);
			return 1;	/* failure */
	}
	basereg = CS5530_BASEREG(hwif);
	reg = inl(basereg+4);			/* get drive0 config register */
	timings |= reg & 0x80000000;		/* preserve PIO format bit */
	if (unit == 0) {			/* are we configuring drive0? */
		outl(timings, basereg+4);	/* write drive0 config register */
	} else {
		if (timings & 0x00100000)
			reg |=  0x00100000;	/* enable UDMA timings for both drives */
		else
			reg &= ~0x00100000;	/* disable UDMA timings for both drives */
		outl(reg,     basereg+4);	/* write drive0 config register */
		outl(timings, basereg+12);	/* write drive1 config register */
	}
	outb(inb(hwif->dma_base+2)|(unit?0x40:0x20), hwif->dma_base+2);	/* set DMA_capable bit */

	if (!strcmp(drive->name, "hdc"))	/* FIXME */
		return 0;
	/*
	 * Finally, turn DMA on in software, and exit.
	 */
	return hwif->dmaproc(ide_dma_on, drive);	/* success */
}

/*
 * This is a CS5530-specific wrapper for the standard ide_dmaproc().
 * We need it for our custom "ide_dma_check" function.
 * All other requests are forwarded to the standard ide_dmaproc().
 */
int cs5530_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return cs5530_config_dma(drive);
		default:
			return ide_dmaproc(func, drive);
	}
}

/*
 * Initialize the cs5530 bridge for reliable IDE DMA operation.
 */
unsigned int __init pci_init_cs5530 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *master_0 = NULL, *cs5530_0 = NULL;
	unsigned short pcicmd = 0;
	unsigned long flags;

	pci_for_each_dev (dev) {
		if (dev->vendor == PCI_VENDOR_ID_CYRIX) {
			switch (dev->device) {
				case PCI_DEVICE_ID_CYRIX_PCI_MASTER:
					master_0 = dev;
					break;
				case PCI_DEVICE_ID_CYRIX_5530_LEGACY:
					cs5530_0 = dev;
					break;
			}
		}
	}
	if (!master_0) {
		printk("%s: unable to locate PCI MASTER function\n", name);
		return 0;
	}
	if (!cs5530_0) {
		printk("%s: unable to locate CS5530 LEGACY function\n", name);
		return 0;
	}

	save_flags(flags);
	cli();	/* all CPUs (there should only be one CPU with this chipset) */

	/*
	 * Enable BusMaster and MemoryWriteAndInvalidate for the cs5530:
	 * -->  OR 0x14 into 16-bit PCI COMMAND reg of function 0 of the cs5530
	 */
	pci_read_config_word (cs5530_0, PCI_COMMAND, &pcicmd);
	pci_write_config_word(cs5530_0, PCI_COMMAND, pcicmd | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE);

	/*
	 * Set PCI CacheLineSize to 16-bytes:
	 * --> Write 0x04 into 8-bit PCI CACHELINESIZE reg of function 0 of the cs5530
	 */
	pci_write_config_byte(cs5530_0, PCI_CACHE_LINE_SIZE, 0x04);

	/*
	 * Disable trapping of UDMA register accesses (Win98 hack):
	 * --> Write 0x5006 into 16-bit reg at offset 0xd0 of function 0 of the cs5530
	 */
	pci_write_config_word(cs5530_0, 0xd0, 0x5006);

	/*
	 * Bit-1 at 0x40 enables MemoryWriteAndInvalidate on internal X-bus:
	 * The other settings are what is necessary to get the register
	 * into a sane state for IDE DMA operation.
	 */
	pci_write_config_byte(master_0, 0x40, 0x1e);

	/* 
	 * Set max PCI burst size (16-bytes seems to work best):
	 *	   16bytes: set bit-1 at 0x41 (reg value of 0x16)
	 *	all others: clear bit-1 at 0x41, and do:
	 *	  128bytes: OR 0x00 at 0x41
	 *	  256bytes: OR 0x04 at 0x41
	 *	  512bytes: OR 0x08 at 0x41
	 *	 1024bytes: OR 0x0c at 0x41
	 */
	pci_write_config_byte(master_0, 0x41, 0x14);

	/*
	 * These settings are necessary to get the chip
	 * into a sane state for IDE DMA operation.
	 */
	pci_write_config_byte(master_0, 0x42, 0x00);
	pci_write_config_byte(master_0, 0x43, 0xc1);

	restore_flags(flags);
	return 0;
}

/*
 * This gets invoked by the IDE driver once for each channel,
 * and performs channel-specific pre-initialization before drive probing.
 */
void __init ide_init_cs5530 (ide_hwif_t *hwif)
{
	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;
	if (!hwif->dma_base) {
		hwif->autodma = 0;
	} else {
		unsigned int basereg, d0_timings;

		hwif->dmaproc  = &cs5530_dmaproc;
		hwif->tuneproc = &cs5530_tuneproc;
		basereg = CS5530_BASEREG(hwif);
		d0_timings = inl(basereg+0);
		if (CS5530_BAD_PIO(d0_timings)) {	/* PIO timings not initialized? */
			outl(cs5530_pio_timings[(d0_timings>>31)&1][0], basereg+0);
			if (!hwif->drives[0].autotune)
				hwif->drives[0].autotune = 1;	/* needs autotuning later */
		}
		if (CS5530_BAD_PIO(inl(basereg+8))) {	/* PIO timings not initialized? */
			outl(cs5530_pio_timings[(d0_timings>>31)&1][0], basereg+8);
			if (!hwif->drives[1].autotune)
				hwif->drives[1].autotune = 1;	/* needs autotuning later */
		}
	}
}
