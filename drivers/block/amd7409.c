/*
 * linux/drivers/block/amd7409.c	Version 0.01	Jan. 10, 2000
 *
 * Copyright (C) 2000			Andre Hedrick <andre@suse.com>
 * May be copied or modified under the terms of the GNU General Public License
 *
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
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"

/*
 * Here is where all the hard work goes to program the chipset.
 *
 */
static int amd7409_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	int err			= 0;
	int drive_number	= ((HWIF(drive)->channel ? 2 : 0) +
				   (drive->select.b.unit & 0x01));
	byte drive_pci		= 0x00;
	byte drive_pci2		= 0x00;
	byte drive_timing	= 0x00;
	byte pio_timing		= 0x00;

        switch (drive_number) {
		case 0: drive_pci = 0x53; drive_pci2 = 0x4b; break;
		case 1: drive_pci = 0x52; drive_pci2 = 0x4a; break;
		case 2: drive_pci = 0x51; drive_pci2 = 0x49; break;
		case 3: drive_pci = 0x50; drive_pci2 = 0x48; break;
		default:
                        return ((int) ide_dma_off_quietly);
        }

	pci_read_config_byte(dev, drive_pci, &drive_timing);
	pci_read_config_byte(dev, drive_pci2, &pio_timing);

	printk("%s: UDMA 0x%02x PIO 0x%02x ",
		drive->name, drive_timing, pio_timing);

	switch(speed) {
		case XFER_UDMA_4:
			drive_timing &= ~0xC7;
			drive_timing |= 0x45;
			pci_write_config_byte(dev, drive_pci, drive_timing);
			break;
		case XFER_UDMA_3:
			drive_timing &= ~0xC7;
			drive_timing |= 0x44;
			pci_write_config_byte(dev, drive_pci, drive_timing);
			break;
		case XFER_UDMA_2:
			drive_timing &= ~0xC7;
			drive_timing |= 0x40;
			pci_write_config_byte(dev, drive_pci, drive_timing);
			break;
		case XFER_UDMA_1:
			drive_timing &= ~0xC7;
			drive_timing |= 0x41;
			pci_write_config_byte(dev, drive_pci, drive_timing);
			break;
		case XFER_UDMA_0:
			drive_timing &= ~0xC7;
			drive_timing |= 0x42;
			pci_write_config_byte(dev, drive_pci, drive_timing);
			break;
		case XFER_MW_DMA_2:break;
		case XFER_MW_DMA_1:break;
		case XFER_MW_DMA_0:break;
		case XFER_SW_DMA_2:break;
		case XFER_SW_DMA_1:break;
		case XFER_SW_DMA_0:break;
		case XFER_PIO_4:break;
		case XFER_PIO_3:break;
		case XFER_PIO_2:break;
		case XFER_PIO_1:break;
		case XFER_PIO_0:break;
		default: break;
        }

	printk(":: UDMA 0x%02x PIO 0x%02x\n", drive_timing, pio_timing);

	err = ide_config_drive_speed(drive, speed);
	return (err);
}

/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;
	int  rval;

	if ((id->dma_ultra & 0x0010) && (HWIF(drive)->udma_four)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (HWIF(drive)->udma_four)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra & 0x0004) {
		speed = XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0002) {
		speed = XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0001) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else if (id->dma_1word & 0x0002) {
		speed = XFER_SW_DMA_1;
	} else if (id->dma_1word & 0x0001) {
		speed = XFER_SW_DMA_0;
        } else {
		return ((int) ide_dma_off_quietly);
	}

	(void) amd7409_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);

	return rval;
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
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;
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
	(void) amd7409_tune_chipset(drive, speed);
}

static void amd7409_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) amd7409_tune_chipset(drive, speed);
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
			if (id->dma_ultra & 0x001F) {
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
 * amd7409_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */

int amd7409_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

unsigned int __init ata66_amd7409 (ide_hwif_t *hwif)
{
#if 0
	byte ata66 = 0;

	pci_read_config_byte(hwif->pci_dev, 0x48, &ata66);
	return ((ata66 & 0x02) ? 0 : 1);
#else
	return 0;
#endif
}

void __init ide_init_amd7409 (ide_hwif_t *hwif)
{
	hwif->tuneproc = &amd7409_tune_drive;
	if (hwif->dma_base) {
		hwif->dmaproc = &amd7409_dmaproc;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
}
