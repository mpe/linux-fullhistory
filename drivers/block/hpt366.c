/*
 * linux/drivers/block/hpt366.c		Version 0.15	Dec. 22, 1999
 *
 * Copyright (C) 1999			Andre Hedrick <andre@suse.com>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Thanks to HighPoint Technologies for their assistance, and hardware.
 * Special Thanks to Jon Burchmore in SanDiego for the deep pockets, his
 * donation of an ABit BP6 mainboard, processor, and memory acellerated
 * development and support.
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

const char *bad_ata66_4[] = {
	"WDC AC310200R",
	NULL
};

const char *bad_ata66_3[] = {
	"WDC AC310200R",
	NULL
};

const char *bad_ata33[] = {
	"Maxtor 92720U8", "Maxtor 92040U6", "Maxtor 91360U4", "Maxtor 91020U3", "Maxtor 90845U3", "Maxtor 90650U2",
	"Maxtor 91360D8", "Maxtor 91190D7", "Maxtor 91020D6", "Maxtor 90845D5", "Maxtor 90680D4", "Maxtor 90510D3", "Maxtor 90340D2",
	"Maxtor 91152D8", "Maxtor 91008D7", "Maxtor 90845D6", "Maxtor 90840D6", "Maxtor 90720D5", "Maxtor 90648D5", "Maxtor 90576D4",
	"Maxtor 90510D4",
	"Maxtor 90432D3", "Maxtor 90288D2", "Maxtor 90256D2",
	"Maxtor 91000D8", "Maxtor 90910D8", "Maxtor 90875D7", "Maxtor 90840D7", "Maxtor 90750D6", "Maxtor 90625D5", "Maxtor 90500D4",
	"Maxtor 91728D8", "Maxtor 91512D7", "Maxtor 91303D6", "Maxtor 91080D5", "Maxtor 90845D4", "Maxtor 90680D4", "Maxtor 90648D3", "Maxtor 90432D2",
	NULL
};

struct chipset_bus_clock_list_entry {
	byte		xfer_speed;
	unsigned int	chipset_settings;
};

struct chipset_bus_clock_list_entry forty_base [] = {

	{	XFER_UDMA_4	,	0x900fd943	},
	{	XFER_UDMA_3	,	0x900ad943	},
	{	XFER_UDMA_2	,	0x900bd943	},
	{	XFER_UDMA_1	,	0x9008d943	},
	{	XFER_UDMA_0	,	0x9008d943	},

	{	XFER_MW_DMA_2	,	0xa008d943	},
	{	XFER_MW_DMA_1	,	0xa010d955	},
	{	XFER_MW_DMA_0	,	0xa010d9fc	},

	{	XFER_PIO_4	,	0xc008d963	},
	{	XFER_PIO_3	,	0xc010d974	},
	{	XFER_PIO_2	,	0xc010d997	},
	{	XFER_PIO_1	,	0xc010d9c7	},
	{	XFER_PIO_0	,	0xc018d9d9	},
	{	0		,	0x0120d9d9	}
};

struct chipset_bus_clock_list_entry thirty_three_base [] = {

	{	XFER_UDMA_4	,	0x90c9a731	},
	{	XFER_UDMA_3	,	0x90cfa731	},
	{	XFER_UDMA_2	,	0x90caa731	},
	{	XFER_UDMA_1	,	0x90cba731	},
	{	XFER_UDMA_0	,	0x90c8a731	},

	{	XFER_MW_DMA_2	,	0xa0c8a731	},
	{	XFER_MW_DMA_1	,	0xa0c8a732	},	/* 0xa0c8a733 */
	{	XFER_MW_DMA_0	,	0xa0c8a797	},

	{	XFER_PIO_4	,	0xc0c8a731	},
	{	XFER_PIO_3	,	0xc0c8a742	},
	{	XFER_PIO_2	,	0xc0d0a753	},
	{	XFER_PIO_1	,	0xc0d0a7a3	},	/* 0xc0d0a793 */
	{	XFER_PIO_0	,	0xc0d0a7aa	},	/* 0xc0d0a7a7 */
	{	0		,	0x0120a7a7	}
};

struct chipset_bus_clock_list_entry twenty_five_base [] = {

	{	XFER_UDMA_4	,	0x90c98521	},
	{	XFER_UDMA_3	,	0x90cf8521	},
	{	XFER_UDMA_2	,	0x90cf8521	},
	{	XFER_UDMA_1	,	0x90cb8521	},
	{	XFER_UDMA_0	,	0x90cb8521	},

	{	XFER_MW_DMA_2	,	0xa0ca8521	},
	{	XFER_MW_DMA_1	,	0xa0ca8532	},
	{	XFER_MW_DMA_0	,	0xa0ca8575	},

	{	XFER_PIO_4	,	0xc0ca8521	},
	{	XFER_PIO_3	,	0xc0ca8532	},
	{	XFER_PIO_2	,	0xc0ca8542	},
	{	XFER_PIO_1	,	0xc0d08572	},
	{	XFER_PIO_0	,	0xc0d08585	},
	{	0		,	0x01208585	}
};

#define HPT366_DEBUG_DRIVE_INFO		0
#define HPT366_ALLOW_ATA66_4		1
#define HPT366_ALLOW_ATA66_3		1

extern char *ide_xfer_verbose (byte xfer_rate);
byte hpt363_shared_irq = 0;
byte hpt363_shared_pin = 0;

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;
#if HPT366_DEBUG_DRIVE_INFO
	printk("check_in_drive_lists(%s, %p)\n", drive->name, list);
#endif /* HPT366_DEBUG_DRIVE_INFO */

	while (*list) {
		if (!strcmp(*list++,id->model)) {
#ifdef DEBUG
			printk("%s: Broken ASIC, BackSpeeding (U)DMA for %s\n", drive->name, id->model);
#endif /* DEBUG */
			return 1;
		}
	}
	return 0;
}

static unsigned int pci_bus_clock_list (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
#if HPT366_DEBUG_DRIVE_INFO
	printk("pci_bus_clock_list(speed=0x%02x, table=%p)\n", speed, chipset_table);
#endif /* HPT366_DEBUG_DRIVE_INFO */
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
#if HPT366_DEBUG_DRIVE_INFO
			printk("pci_bus_clock_list: found match: 0x%08x\n", chipset_table->chipset_settings);
#endif /* HPT366_DEBUG_DRIVE_INFO */
			return chipset_table->chipset_settings;
		}
#if HPT366_DEBUG_DRIVE_INFO
	printk("pci_bus_clock_list: using default: 0x%08x\n", 0x01208585);
#endif /* HPT366_DEBUG_DRIVE_INFO */
	return 0x01208585;
}

static int hpt366_tune_chipset (ide_drive_t *drive, byte speed)
{
	int			err;
#if HPT366_DEBUG_DRIVE_INFO
	int drive_number	= ((HWIF(drive)->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
#endif /* HPT366_DEBUG_DRIVE_INFO */
	byte regtime		= (drive->select.b.unit & 0x01) ? 0x44 : 0x40;
	unsigned int reg1	= 0;
	unsigned int reg2	= 0;

#if HPT366_DEBUG_DRIVE_INFO
	printk("hpt366_tune_chipset(%s, speed=0x%02x)\n", drive->name, speed);
#endif /* HPT366_DEBUG_DRIVE_INFO */

	pci_read_config_dword(HWIF(drive)->pci_dev, regtime, &reg1);
	/* detect bus speed by looking at control reg timing: */
	switch((reg1 >> 8) & 7) {
		case 5:
			reg2 = pci_bus_clock_list(speed, forty_base);
			break;
		case 9:
			reg2 = pci_bus_clock_list(speed, twenty_five_base);
			break;
		default:
			printk("hpt366: assuming 33Mhz PCI bus\n");
		case 7:
			reg2 = pci_bus_clock_list(speed, thirty_three_base);
			break;
	}
	/*
	 * Disable on-chip PIO FIFO/buffer (to avoid problems handling I/O errors later)
	 */
	reg2 &= ~0x80000000;

	pci_write_config_dword(HWIF(drive)->pci_dev, regtime, reg2);
	err = ide_config_drive_speed(drive, speed);

#if HPT366_DEBUG_DRIVE_INFO
	printk("%s: speed=0x%02x(%s), drive%d, old=0x%08x, new=0x%08x, err=0x%04x\n",
		drive->name, speed, ide_xfer_verbose(speed),
		drive_number, reg1, reg2, err);
#endif /* HPT366_DEBUG_DRIVE_INFO */
	return(err);
}

/*
 * This allows the configuration of ide_pci chipset registers
 * for cards that learn about the drive's UDMA, DMA, PIO capabilities
 * after the drive is reported by the OS.  Initally for designed for
 * HPT366 UDMA chipset by HighPoint|Triones Technologies, Inc.
 *
 * check_in_drive_lists(drive, bad_ata66_4)
 * check_in_drive_lists(drive, bad_ata66_3)
 * check_in_drive_lists(drive, bad_ata33)
 *
 */
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;
	byte reg51h		= 0;
	unsigned int reg40	= 0;
	int  rval;

	if ((id->dma_ultra & 0x0010) &&
	    (!check_in_drive_lists(drive, bad_ata66_4)) &&
	    (HPT366_ALLOW_ATA66_4) &&
	    (HWIF(drive)->udma_four)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) &&
		   (!check_in_drive_lists(drive, bad_ata66_3)) &&
		   (HPT366_ALLOW_ATA66_3) &&
		   (HWIF(drive)->udma_four)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra && (!check_in_drive_lists(drive, bad_ata33))) {
		if (id->dma_ultra & 0x0004) {
			speed = XFER_UDMA_2;
		} else if (id->dma_ultra & 0x0002) {
			speed = XFER_UDMA_1;
		} else if (id->dma_ultra & 0x0001) {
			speed = XFER_UDMA_0;
		}
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
#if HPT366_DEBUG_DRIVE_INFO
		printk("%s: config_chipset_for_dma: returning 'ide_dma_off_quietly'\n", drive->name);
#endif /* HPT366_DEBUG_DRIVE_INFO */
		return ((int) ide_dma_off_quietly);
	}

	pci_read_config_byte(HWIF(drive)->pci_dev, 0x51, &reg51h);

#ifdef CONFIG_HPT366_FAST_IRQ_PREDICTION
	/*
	 * Some drives prefer/allow for the method of handling interrupts.
	 */
	if (!(reg51h & 0x80))
		pci_write_config_byte(HWIF(drive)->pci_dev, 0x51, reg51h|0x80);
#else /* ! CONFIG_HPT366_FAST_IRQ_PREDICTION */
	/*
	 * Disable the "fast interrupt" prediction.
	 * Instead, always wait for the real interrupt from the drive!
	 */
	if (reg51h & 0x80)
		pci_write_config_byte(HWIF(drive)->pci_dev, 0x51, reg51h & ~0x80);
#endif /* CONFIG_HPT366_FAST_IRQ_PREDICTION */

	/*
	 * Preserve existing PIO settings:
	 */
	pci_read_config_dword(HWIF(drive)->pci_dev, 0x40, &reg40);
	speed = (speed & ~0xc0000000) | (reg40 & 0xc0000000);

#if HPT366_DEBUG_DRIVE_INFO
	printk("%s: config_chipset_for_dma:  speed=0x%04x\n", drive->name, speed);
#endif /* HPT366_DEBUG_DRIVE_INFO */
	(void) hpt366_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);

#if HPT366_DEBUG_DRIVE_INFO
	printk("%s: config_chipset_for_dma: returning %d (%s)\n", drive->name, rval, rval == ide_dma_on ? "dma_on" : "dma_off");
#endif /* HPT366_DEBUG_DRIVE_INFO */
	return rval;
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio	= drive->id->eide_pio_modes;
	byte			timing, speed, pio;
	unsigned int reg40 = 0;

#if HPT366_DEBUG_DRIVE_INFO
	printk("%s: config_chipset_for_pio\n", drive->name);
#endif /* HPT366_DEBUG_DRIVE_INFO */
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
	/*
	 * Preserve existing DMA settings:
	 */
	pci_read_config_dword(HWIF(drive)->pci_dev, 0x40, &reg40);
	speed = (speed & ~0x30070000) | (reg40 & 0x30070000);
#if HPT366_DEBUG_DRIVE_INFO
	printk("%s: config_chipset_for_pio:  speed=0x%04x\n", drive->name, speed);
#endif /* HPT366_DEBUG_DRIVE_INFO */
	(void) hpt366_tune_chipset(drive, speed);
}

static void hpt366_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) hpt366_tune_chipset(drive, speed);
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
 * hpt366_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 *
 * This is specific to the HPT366 UDMA bios chipset
 * by HighPoint|Triones Technologies, Inc.
 */

int hpt366_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	byte reg50h = 0;

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_lostirq:
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x50, &reg50h);
			pci_write_config_byte(HWIF(drive)->pci_dev, 0x50, reg50h|0x03);
			pci_read_config_byte(HWIF(drive)->pci_dev, 0x50, &reg50h);
			/* ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL); */
		case ide_dma_timeout:
			break;
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

unsigned int __init pci_init_hpt366 (struct pci_dev *dev, const char *name)
{
	byte test = 0;

	if (dev->resource[PCI_ROM_RESOURCE].start)
		pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);

	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x08);

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &test);
	if (test != 0x78)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x78);

	pci_read_config_byte(dev, PCI_MIN_GNT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MIN_GNT, 0x08);

	pci_read_config_byte(dev, PCI_MAX_LAT, &test);
	if (test != 0x08)
		pci_write_config_byte(dev, PCI_MAX_LAT, 0x08);

	return dev->irq;
}

unsigned int __init ata66_hpt366 (ide_hwif_t *hwif)
{
	byte ata66 = 0;

	pci_read_config_byte(hwif->pci_dev, 0x5a, &ata66);
#ifdef DEBUG
	printk("HPT366: reg5ah=0x%02x ATA-%s Cable Port%d\n",
		ata66, (ata66 & 0x02) ? "33" : "66",
		PCI_FUNC(hwif->pci_dev->devfn));
#endif /* DEBUG */
	return ((ata66 & 0x02) ? 0 : 1);
}

void __init ide_init_hpt366 (ide_hwif_t *hwif)
{
#if 0
	if ((PCI_FUNC(hwif->pci_dev->devfn) & 1) && (hpt363_shared_irq)) {
		hwif->mate = &ide_hwifs[hwif->index-1];
		hwif->mate->mate = hwif;
		hwif->serialized = hwif->mate->serialized = 1;
	}

	if ((PCI_FUNC(hwif->pci_dev->devfn) & 1) && (hpt363_shared_pin)) {

	}
#endif

	hwif->tuneproc = &hpt366_tune_drive;
	if (hwif->dma_base) {
		hwif->dmaproc = &hpt366_dmaproc;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
}

void ide_dmacapable_hpt366 (ide_hwif_t *hwif, unsigned long dmabase)
{
	byte masterdma = 0, slavedma = 0;
	byte dma_new = 0, dma_old = inb(dmabase+2);
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	dma_new = dma_old;
	pci_read_config_byte(hwif->pci_dev, 0x43, &masterdma);
	pci_read_config_byte(hwif->pci_dev, 0x47, &slavedma);

	if (masterdma & 0x30)	dma_new |= 0x20;
	if (slavedma & 0x30)	dma_new |= 0x40;
	if (dma_new != dma_old) outb(dma_new, dmabase+2);

	__restore_flags(flags);	/* local CPU only */

	ide_setup_dma(hwif, dmabase, 8);
}
