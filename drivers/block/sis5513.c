/*
 * linux/drivers/block/sis5513.c	Version 0.06	July 11, 1999
 *
 * Copyright (C) 1999	Andre Hedrick
 *
 * drive_number
 *	= ((HWIF(drive)->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
 *	= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
 */

#include <linux/config.h>
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

static struct pci_dev *host_dev;

#define SIS5513_DEBUG_DRIVE_INFO	0

extern char *ide_xfer_verbose (byte xfer_rate);

/*
 * ((id->word93 & 0x2000) && (HWIF(drive)->udma_four))
 */
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	byte			drive_pci, test1, test2, mask;
	int			err;

	byte speed		= 0x00;
	byte unmask		= 0xE0;
	byte four_two		= 0x00;
	int drive_number	= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
	byte udma_66		= ((id->word93 & 0x2000) && (hwif->udma_four)) ? 1 : 0;

	if (host_dev) {
		switch(host_dev->device) {
			case PCI_DEVICE_ID_SI_530:
			case PCI_DEVICE_ID_SI_620:
				unmask   = 0xF0;
				four_two = 0x01;
			default:
				break;
		}
	}

	switch(drive_number) {
		case 0:		drive_pci = 0x40;break;
		case 1:		drive_pci = 0x42;break;
		case 2:		drive_pci = 0x44;break;
		case 3:		drive_pci = 0x46;break;
		default:	return ide_dma_off;
	}

	pci_read_config_byte(dev, drive_pci, &test1);
	pci_read_config_byte(dev, drive_pci|0x01, &test2);

	if ((!ultra) && (test2 & 0x80)) {
		pci_write_config_byte(dev, drive_pci|0x01, test2 & ~0x80);
		pci_read_config_byte(dev, drive_pci|0x01, &test2);
	}

	if ((id->dma_ultra & 0x0010) && (ultra) && (udma_66) && (four_two)) {
		if (!((id->dma_ultra >> 8) & 16)) {
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_ultra |= 0x1010;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		if (!(test2 & 0x90)) {
			pci_write_config_byte(dev, drive_pci|0x01, test2 & ~unmask);
			pci_write_config_byte(dev, drive_pci|0x01, test2|0x90);
		}
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (ultra) && (udma_66) && (four_two)) {
		if (!((id->dma_ultra >> 8) & 8)) {
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_ultra |= 0x0808;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		if (!(test2 & 0xA0)) {
			pci_write_config_byte(dev, drive_pci|0x01, test2 & ~unmask);
			pci_write_config_byte(dev, drive_pci|0x01, test2|0xA0);
		}
		speed = XFER_UDMA_3;
	} else if ((id->dma_ultra & 0x0004) && (ultra)) {
		if (!((id->dma_ultra >> 8) & 4)) {
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_ultra |= 0x0404;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		mask = (four_two) ? 0xB0 : 0xA0;
		if (!(test2 & mask)) {
			pci_write_config_byte(dev, drive_pci|0x01, test2 & ~unmask);
			pci_write_config_byte(dev, drive_pci|0x01, test2|mask);
		}
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		if (!((id->dma_ultra >> 8) & 2)) {
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_ultra |= 0x0202;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		mask = (four_two) ? 0xD0 : 0xC0;
		if (!(test2 & mask)) {
			pci_write_config_byte(dev, drive_pci|0x01, test2 & ~unmask);
			pci_write_config_byte(dev, drive_pci|0x01, test2|mask);
		}
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
		if (!((id->dma_ultra >> 8) & 1)) {
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_ultra |= 0x0101;
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_1word &= ~0x0F00;
		}
		if (!(test2 & unmask)) {
			pci_write_config_byte(dev, drive_pci|0x01, test2 & ~unmask);
			pci_write_config_byte(dev, drive_pci|0x01, test2|unmask);
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

	err = ide_config_drive_speed(drive, speed);

#if SIS5513_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d\n",
		drive->name,
		ide_xfer_verbose(speed),
		drive_number);
#endif /* SIS5513_DEBUG_DRIVE_INFO */

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static void config_drive_art_rwp (ide_drive_t *drive)
{
	ide_hwif_t *hwif		= HWIF(drive);
	struct pci_dev *dev		= hwif->pci_dev;

	byte				timing, pio, drive_pci, test1, test2;

	unsigned short eide_pio_timing[6] = {600, 390, 240, 180, 120, 90};
	unsigned short xfer_pio		= drive->id->eide_pio_modes;
	int drive_number		= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));

	if (drive->media == ide_disk) {
		struct pci_dev *dev	= hwif->pci_dev;
		byte reg4bh		= 0;
                byte rw_prefetch	= (0x11 << drive_number);

		pci_read_config_byte(dev, 0x4b, &reg4bh);
		if ((reg4bh & rw_prefetch) != rw_prefetch)
			pci_write_config_byte(dev, 0x4b, reg4bh|rw_prefetch);
	}

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

/*
 *               Mode 0       Mode 1     Mode 2     Mode 3     Mode 4
 * Active time    8T (240ns)  6T (180ns) 4T (120ns) 3T  (90ns) 3T  (90ns)
 * 0x41 2:0 bits  000          110        100        011        011
 * Recovery time 12T (360ns)  7T (210ns) 4T (120ns) 3T  (90ns) 1T  (30ns)
 * 0x40 3:0 bits 0000         0111       0100       0011       0001
 * Cycle time    20T (600ns) 13T (390ns) 8T (240ns) 6T (180ns) 4T (120ns)
 */

	switch(drive_number) {
		case 0:		drive_pci = 0x40;break;
		case 1:		drive_pci = 0x42;break;
		case 2:		drive_pci = 0x44;break;
		case 3:		drive_pci = 0x46;break;
		default:	return;
	}

	pci_read_config_byte(dev, drive_pci, &test1);
	pci_read_config_byte(dev, drive_pci|0x01, &test2);

	/*
	 * Do a blanket clear of active and recovery timings.
	 */

	test1 &= ~0x07;
	test2 &= ~0x0F;

	switch(timing) {
		case 4:		test1 |= 0x01;test2 |= 0x03;break;
		case 3:		test1 |= 0x03;test2 |= 0x03;break;
		case 2:		test1 |= 0x04;test2 |= 0x04;break;
		case 1:		test1 |= 0x07;test2 |= 0x06;break;
		default:	break;
	}

	pci_write_config_byte(dev, drive_pci, test1);
	pci_write_config_byte(dev, drive_pci|0x01, test2);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id		= drive->id;
	ide_dma_action_t dma_func	= ide_dma_off_quietly;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			return HWIF(drive)->dmaproc(ide_dma_off, drive);
		}

		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x001F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive, 1);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive, 0);
			}
		} else if ((ide_dmaproc(ide_dma_good_drive, drive)) &&
			   (id->eide_dma_time > 150)) {
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive, 0);
		}
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * sis5513_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */
int sis5513_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			config_drive_art_rwp(drive);
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

__initfunc(unsigned int pci_init_sis5513 (struct pci_dev *dev, const char *name))
{
	struct pci_dev *host;
	byte latency = 0, reg48h = 0;

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &latency);
	pci_read_config_byte(dev, 0x48, &reg48h);

	for (host = pci_devices; host; host=host->next) {
		if (host->vendor == PCI_VENDOR_ID_SI &&
		    host->device == PCI_DEVICE_ID_SI_620) {
			if (latency != 0x10)
				pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x10);
			host_dev = host;
			printk("%s: Chipset Core ATA-66, SiS620\n", name);
			printk("%s: Primary ATA-%s, Secondary ATA-%s Cable Detect\n",
				name,
				(reg48h & 0x10) ? "33" : "66",
				(reg48h & 0x20) ? "33" : "66");
			break;
		} else if (host->vendor == PCI_VENDOR_ID_SI &&
			   host->device == PCI_DEVICE_ID_SI_530) {
			host_dev = host;
			printk("%s: Chipset Core ATA-66, SiS530\n", name);
			printk("%s: Primary ATA-%s, Secondary ATA-%s Cable Detect\n",
				name,
				(reg48h & 0x10) ? "33" : "66",
				(reg48h & 0x20) ? "33" : "66");
			break;
		} else if (host->vendor == PCI_VENDOR_ID_SI &&
			   host->device == PCI_DEVICE_ID_SI_5600) {
			host_dev = host;
			printk("SIS5600:%s Chipset Core ATA-33\n", name);
			break;
		} else if (host->vendor == PCI_VENDOR_ID_SI &&
			   host->device == PCI_DEVICE_ID_SI_5597) {
			host_dev = host;
			printk("SIS5597:%s Chipset Core ATA-33\n", name);
			break;
		}
	}

	if (host_dev) {
		byte reg52h = 0;

		pci_read_config_byte(dev, 0x52, &reg52h);
		if (!(reg52h & 0x04))
			pci_write_config_byte(dev, 0x52, reg52h|0x04);
	}

	return 0;
}

__initfunc(void ide_init_sis5513 (ide_hwif_t *hwif))
{
	byte reg48h = 0;
	byte mask = hwif->channel ? 0x20 : 0x10;

	pci_read_config_byte(hwif->pci_dev, 0x48, &reg48h);
	hwif->irq = hwif->channel ? 15 : 14;

	if (!(hwif->dma_base))
		return;

	if (host_dev) {
		switch(host_dev->device) {
			case PCI_DEVICE_ID_SI_530:
			case PCI_DEVICE_ID_SI_620:
				hwif->autodma = 1;
				hwif->udma_four = (reg48h & mask) ? 0 : 1;
				hwif->dmaproc = &sis5513_dmaproc;
				return;
			case PCI_DEVICE_ID_SI_5600:
			case PCI_DEVICE_ID_SI_5597:
				hwif->autodma = 1;
				hwif->udma_four = 0;
				hwif->dmaproc = &sis5513_dmaproc;
				return;
			default:
				hwif->autodma = 0;
				hwif->udma_four = 0;
				return;
		}
	}
}
