/*
 * linux/drivers/block/hpt343.c		Version 0.23	May 12, 1999
 *
 * Copyright (C) 1998-99	Andre Hedrick
 *					(hedrick@astro.dyer.vanderbilt.edu)
 *
 * 00:12.0 Unknown mass storage controller:
 * Triones Technologies, Inc.
 * Unknown device 0003 (rev 01)
 *
 * hde: UDMA 2 (0x0000 0x0002) (0x0000 0x0010)
 * hdf: UDMA 2 (0x0002 0x0012) (0x0010 0x0030)
 * hde: DMA 2  (0x0000 0x0002) (0x0000 0x0010)
 * hdf: DMA 2  (0x0002 0x0012) (0x0010 0x0030)
 * hdg: DMA 1  (0x0012 0x0052) (0x0030 0x0070)
 * hdh: DMA 1  (0x0052 0x0252) (0x0070 0x00f0)
 *
 * drive_number
 *	= ((HWIF(drive)->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
 *	= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
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

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

#define HPT343_DEBUG_DRIVE_INFO		0
#define HPT343_DISABLE_ALL_DMAING	0
#define HPT343_DMA_DISK_ONLY		0

extern char *ide_xfer_verbose (byte xfer_rate);

static void hpt34x_clear_chipset (ide_drive_t *drive)
{
	int drive_number	= ((HWIF(drive)->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
	unsigned int reg1	= 0, tmp1 = 0;
	unsigned int reg2	= 0, tmp2 = 0;

	pci_read_config_dword(HWIF(drive)->pci_dev, 0x44, &reg1);
	pci_read_config_dword(HWIF(drive)->pci_dev, 0x48, &reg2);
	tmp1 = ((0x00 << (3*drive_number)) | (reg1 & ~(7 << (3*drive_number))));
	tmp2 = ((0x00 << drive_number) | reg2);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x44, tmp1);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x48, tmp2);
}

static int hpt34x_tune_chipset (ide_drive_t *drive, byte speed)
{
	int			err;
	byte			hi_speed, lo_speed;
	int drive_number	= ((HWIF(drive)->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
	unsigned int reg1	= 0, tmp1 = 0;
	unsigned int reg2	= 0, tmp2 = 0;

	SPLIT_BYTE(speed, hi_speed, lo_speed);

	if (hi_speed & 7) {
		hi_speed = (hi_speed & 4) ? 0x01 : 0x10;
	} else {
		lo_speed <<= 5;
		lo_speed >>= 5;
	}

	pci_read_config_dword(HWIF(drive)->pci_dev, 0x44, &reg1);
	pci_read_config_dword(HWIF(drive)->pci_dev, 0x48, &reg2);
	tmp1 = ((lo_speed << (3*drive_number)) | (reg1 & ~(7 << (3*drive_number))));
	tmp2 = ((hi_speed << drive_number) | reg2);
	err = ide_config_drive_speed(drive, speed);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x44, tmp1);
	pci_write_config_dword(HWIF(drive)->pci_dev, 0x48, tmp2);

#if HPT343_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d (0x%04x 0x%04x) (0x%04x 0x%04x)" \
		" (0x%02x 0x%02x) 0x%04x\n",
		drive->name, ide_xfer_verbose(speed),
		drive_number, reg1, tmp1, reg2, tmp2,
		hi_speed, lo_speed, err);
#endif /* HPT343_DEBUG_DRIVE_INFO */

	return(err);
}

/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initally for designed for
 * HPT343 UDMA chipset by HighPoint|Triones Technologies, Inc.
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;

#if HPT343_DISABLE_ALL_DMAING
	return ((int) ide_dma_off);
#elif HPT343_DMA_DISK_ONLY
	if (drive->media != ide_disk)
		return ((int) ide_dma_off_quietly);
#endif /* HPT343_DISABLE_ALL_DMAING */

	if (id->dma_ultra & 0x0004) {
		if (!((id->dma_ultra >> 8) & 4)) {
			drive->id->dma_ultra &= ~0x0F00;
			drive->id->dma_ultra |= 0x0404;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		speed = XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0002) {
		if (!((id->dma_ultra >> 8) & 2)) {
			drive->id->dma_ultra &= ~0x0F00;
			drive->id->dma_ultra |= 0x0202;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		speed = XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0001) {
		if (!((id->dma_ultra >> 8) & 1)) {
			drive->id->dma_ultra &= ~0x0F00;
			drive->id->dma_ultra |= 0x0101;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		if (!((id->dma_mword >> 8) & 4)) {
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_mword |= 0x0404;
			drive->id->dma_1word &= ~0x0F00;
		}
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		if (!((id->dma_mword >> 8) & 2)) {
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_mword |= 0x0202;
			drive->id->dma_1word &= ~0x0F00;
		}
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		if (!((id->dma_mword >> 8) & 1)) {
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_mword |= 0x0101;
			drive->id->dma_1word &= ~0x0F00;
		}
		speed = XFER_MW_DMA_0;
	} else if (id->dma_1word & 0x0004) {
		if (!((id->dma_1word >> 8) & 4)) {
			drive->id->dma_1word &= ~0x0F00;
			drive->id->dma_1word |= 0x0404;
			drive->id->dma_mword &= ~0x0F00;
		}
		speed = XFER_SW_DMA_2;
	} else if (id->dma_1word & 0x0002) {
		if (!((id->dma_1word >> 8) & 2)) {
			drive->id->dma_1word &= ~0x0F00;
			drive->id->dma_1word |= 0x0202;
			drive->id->dma_mword &= ~0x0F00;
		}
		speed = XFER_SW_DMA_1;
	} else if (id->dma_1word & 0x0001) {
		if (!((id->dma_1word >> 8) & 1)) {
			drive->id->dma_1word &= ~0x0F00;
			drive->id->dma_1word |= 0x0101;
			drive->id->dma_mword &= ~0x0F00;
		}
		speed = XFER_SW_DMA_0;
        } else {
		return ((int) ide_dma_off_quietly);
	}

	(void) hpt34x_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio	= drive->id->eide_pio_modes;

	byte			timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) hpt34x_tune_chipset(drive, speed);
}

static void hpt34x_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;

	hpt34x_clear_chipset(drive);
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) hpt34x_tune_chipset(drive, speed);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x0007) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:

		config_chipset_for_pio(drive);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * hpt34x_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 *
 * This is specific to the HPT343 UDMA bios-less chipset
 * and HPT345 UDMA bios chipset (stamped HPT363)
 * by HighPoint|Triones Technologies, Inc.
 */

int hpt34x_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			hpt34x_clear_chipset(drive);
			return config_drive_xfer_rate(drive);
#if 0
		case ide_dma_off:
		case ide_dma_off_quietly:
		case ide_dma_on:
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_read:
		case ide_dma_write:
		case ide_dma_begin:
		case ide_dma_end:
		case ide_dma_test_irq:
#endif
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

/*
 * If the BIOS does not set the IO base addaress to XX00, 343 will fail.
 */
#define	HPT34X_PCI_INIT_REG		0x80

__initfunc(unsigned int pci_init_hpt34x (struct pci_dev *dev, const char *name))
{
	int i;
	unsigned short cmd;
	unsigned long hpt34xIoBase = dev->base_address[4] & PCI_BASE_ADDRESS_IO_MASK;
#if 0
	unsigned char misc10 = inb(hpt34xIoBase + 0x0010);
	unsigned char misc11 = inb(hpt34xIoBase + 0x0011);
#endif

	pci_write_config_byte(dev, HPT34X_PCI_INIT_REG, 0x00);
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	pci_write_config_word(dev, PCI_COMMAND, cmd & ~PCI_COMMAND_IO);

	dev->base_address[0] = (hpt34xIoBase + 0x20);
	dev->base_address[1] = (hpt34xIoBase + 0x34);
	dev->base_address[2] = (hpt34xIoBase + 0x28);
	dev->base_address[3] = (hpt34xIoBase + 0x3c);

	for(i=0; i<4; i++)
		dev->base_address[i] |= PCI_BASE_ADDRESS_SPACE_IO;

	/*
	 * Since 20-23 can be assigned and are R/W, we correct them.
	 */
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, dev->base_address[0]);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_1, dev->base_address[1]);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_2, dev->base_address[2]);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_3, dev->base_address[3]);
	pci_write_config_word(dev, PCI_COMMAND, cmd);

#if 0
	outb(misc10|0x78, (hpt34xIoBase + 0x0010));
	outb(misc11, (hpt34xIoBase + 0x0011));
#endif

#ifdef DEBUG
	printk("%s: 0x%02x 0x%02x\n",
		(pcicmd & PCI_COMMAND_MEMORY) ? "HPT345" : name,
		inb(hpt34xIoBase + 0x0010),
		inb(hpt34xIoBase + 0x0011));
#endif

	if (cmd & PCI_COMMAND_MEMORY) {
		if (dev->rom_address) {
			pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->rom_address | PCI_ROM_ADDRESS_ENABLE);
			printk(KERN_INFO "HPT345: ROM enabled at 0x%08lx\n", dev->rom_address);
		}
	} else {
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);
	}
	return dev->irq;
}

__initfunc(void ide_init_hpt34x (ide_hwif_t *hwif))
{
	hwif->tuneproc = &hpt34x_tune_drive;
	if (hwif->dma_base) {
		unsigned short pcicmd = 0;

		pci_read_config_word(hwif->pci_dev, PCI_COMMAND, &pcicmd);
#ifdef CONFIG_BLK_DEV_HPT34X_DMA
		hwif->autodma = (pcicmd & PCI_COMMAND_MEMORY) ? 1 : 0;
#endif /* CONFIG_BLK_DEV_HPT34X_DMA */
		hwif->dmaproc = &hpt34x_dmaproc;
	} else {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
}
