/*
 * linux/drivers/ide/aec6210.c		Version 0.06	Mar. 18, 2000
 *
 * Copyright (C) 1998-2000	Andre Hedrick (andre@suse.com)
 * May be copied or modified under the terms of the GNU General Public License
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

#define ACARD_DEBUG_DRIVE_INFO		0

#undef DISPLAY_AEC6210_TIMINGS

#if defined(DISPLAY_AEC6210_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int aec6210_get_info(char *, char **, off_t, int);
extern int (*aec6210_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

static int aec6210_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;

	u32 bibma = bmide_dev->resource[4].start;
	u8 c0 = 0, c1 = 0;
	
	p += sprintf(p, "\n                                AEC6210 Chipset.\n");

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ", (c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ", (c1&0x40) ? "yes" : "no " );

	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");
	return p-buffer;/* => must be less than 4k! */
}
#endif	/* defined(DISPLAY_AEC6210_TIMINGS) && defined(CONFIG_PROC_FS) */

byte aec6210_proc = 0;

#ifdef CONFIG_AEC6210_TUNING

struct chipset_bus_clock_list_entry {
	byte		xfer_speed;
	unsigned short	chipset_settings;
	byte		ultra_settings;
};

struct chipset_bus_clock_list_entry aec6210_base [] = {
	{	XFER_UDMA_2,	0x0401,	0x02	},
	{	XFER_UDMA_1,	0x0401,	0x01	},
	{	XFER_UDMA_0,	0x0401,	0x01	},

	{	XFER_MW_DMA_2,	0x0401,	0x00	},
	{	XFER_MW_DMA_1,	0x0402,	0x00	},
	{	XFER_MW_DMA_0,	0x070a,	0x00	},

	{	XFER_PIO_4,	0x0401,	0x00	},
	{	XFER_PIO_3,	0x0403,	0x00	},
	{	XFER_PIO_2,	0x0708,	0x00	},
	{	XFER_PIO_1,	0x070a,	0x00	},
	{	XFER_PIO_0,	0x0700,	0x00	},
	{	0,		0x0000,	0x00	}
};

extern char *ide_xfer_verbose (byte xfer_rate);

/*
 * TO DO: active tuning and correction of cards without a bios.
 */

static unsigned short pci_bus_clock_list (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return chipset_table->chipset_settings;
		}
	return 0x0000;
}

static byte pci_bus_clock_list_ultra (byte speed, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			return chipset_table->ultra_settings;
		}
	return 0x00;
}

static int aec6210_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);

	int			err;
	byte			drive_pci;
	unsigned short		drive_conf = 0x0000;
	byte			ultra = 0x00, ultra_conf = 0x00;
	byte			tmp1 = 0x00, tmp2 = 0x00;

	int drive_number	= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));

	switch(drive_number) {
		case 0:		drive_pci = 0x40; break;
		case 1:		drive_pci = 0x42; break;
		case 2:		drive_pci = 0x44; break;
		case 3:		drive_pci = 0x46; break;
		default:	return -1;
	}

	pci_read_config_word(HWIF(drive)->pci_dev, drive_pci, &drive_conf);
	drive_conf = pci_bus_clock_list(speed, aec6210_base);
	pci_write_config_word(HWIF(drive)->pci_dev, drive_pci, drive_conf);

	pci_read_config_byte(HWIF(drive)->pci_dev, 0x54, &ultra);
	tmp1 = ((0x00 << (2*drive_number)) | (ultra & ~(3 << (2*drive_number))));
	ultra_conf = pci_bus_clock_list_ultra(speed, aec6210_base);
	tmp2 = ((ultra_conf << (2*drive_number)) | (tmp1 & ~(3 << (2*drive_number))));
	pci_write_config_byte(HWIF(drive)->pci_dev, 0x54, tmp2);

	err = ide_config_drive_speed(drive, speed);

#if ACARD_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d 0x04%x 0x02%x 0x02%x 0x02%x 0x02%x\n",
		drive->name, ide_xfer_verbose(speed), drive_number,
		drive_conf, ultra, tmp1, ultra_conf, tmp2);
#endif /* ACARD_DEBUG_DRIVE_INFO */

	return(err);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id = drive->id;
	byte speed = -1;

	if (drive->media != ide_disk)
		return ((int) ide_dma_off_quietly);

	if (((id->dma_ultra & 0x0010) ||
	     (id->dma_ultra & 0x0008) ||
	     (id->dma_ultra & 0x0004)) && (ultra)) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
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
	(void) aec6210_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_off :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

static void aec6210_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;

	switch(pio) {
		case 5:
			speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);
		case 4:
			speed = XFER_PIO_4; break;
		case 3:
			speed = XFER_PIO_3; break;
		case 2:
			speed = XFER_PIO_2; break;
		case 1:
			speed = XFER_PIO_1; break;
		default:
			speed = XFER_PIO_0; break;
	}
	(void) aec6210_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
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
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive, 0);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		aec6210_tune_drive(drive, 5);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

/*
 * aec6210_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */
int aec6210_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */
#endif /* CONFIG_AEC6210_TUNING */

unsigned int __init pci_init_aec6210 (struct pci_dev *dev, const char *name)
{
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", name, dev->resource[PCI_ROM_RESOURCE].start);
	}

#if defined(DISPLAY_AEC6210_TIMINGS) && defined(CONFIG_PROC_FS)
	aec6210_proc = 1;
	bmide_dev = dev;
	aec6210_display_info = &aec6210_get_info;
#endif /* DISPLAY_AEC6210_TIMINGS && CONFIG_PROC_FS */

	return dev->irq;
}

void __init ide_init_aec6210 (ide_hwif_t *hwif)
{
#ifdef CONFIG_AEC6210_TUNING
	hwif->tuneproc = &aec6210_tune_drive;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base)
		hwif->dmaproc = &aec6210_dmaproc;
#endif /* CONFIG_BLK_DEV_IDEDMA */
#endif /* CONFIG_AEC6210_TUNING */
}

void __init ide_dmacapable_aec6210 (ide_hwif_t *hwif, unsigned long dmabase)
{
	byte dma_new	= 0;
	byte dma_old	= inb(dmabase+2);
	byte reg54h	= 0;
	byte masterdma	= hwif->channel ? 0x30 : 0x03;
	byte slavedma	= hwif->channel ? 0xc0 : 0x0c;
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	dma_new = dma_old;

	pci_read_config_byte(hwif->pci_dev, 0x54, &reg54h);

	if (reg54h & masterdma)	dma_new |= 0x20;
	if (reg54h & slavedma)	dma_new |= 0x40;
	if (dma_new != dma_old)	outb(dma_new, dmabase+2);

	__restore_flags(flags);	/* local CPU only */

	ide_setup_dma(hwif, dmabase, 8);
}
