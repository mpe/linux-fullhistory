/*
 * linux/drivers/block/piix.c	Version 0.28	Dec. 13, 1999
 *
 *  Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
 *  Copyright (C) 1998-1999 Andre Hedrick (andre@suse.com)
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  PIO mode setting function for Intel chipsets.  
 *  For use instead of BIOS settings.
 *
 * 40-41
 * 42-43
 * 
 *                 41
 *                 43
 *
 * | PIO 0       | c0 | 80 | 0 | 	piix_tune_drive(drive, 0);
 * | PIO 2 | SW2 | d0 | 90 | 4 | 	piix_tune_drive(drive, 2);
 * | PIO 3 | MW1 | e1 | a1 | 9 | 	piix_tune_drive(drive, 3);
 * | PIO 4 | MW2 | e3 | a3 | b | 	piix_tune_drive(drive, 4);
 * 
 * sitre = word40 & 0x4000; primary
 * sitre = word42 & 0x4000; secondary
 *
 * 44 8421|8421    hdd|hdb
 * 
 * 48 8421         hdd|hdc|hdb|hda udma enabled
 *
 *    0001         hda
 *    0010         hdb
 *    0100         hdc
 *    1000         hdd
 *
 * 4a 84|21        hdb|hda
 * 4b 84|21        hdd|hdc
 * 
 *    00|00 udma 0
 *    01|01 udma 1
 *    10|10 udma 2
 *    11|11 reserved
 *
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x40, &reg40);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x42, &reg42);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x44, &reg44);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x48, &reg48);
 * pci_read_config_word(HWIF(drive)->pci_dev, 0x4a, &reg4a);
 *
 * #if 0
 * int err;
 * err = ide_config_drive_speed(drive, speed);
 * (void) ide_config_drive_speed(drive, speed);
 * #else
 * #endif
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/delay.h>
#include <asm/io.h>

#include "ide_modes.h"

#define PIIX_DEBUG_DRIVE_INFO		0

#define DISPLAY_PIIX_TIMINGS

#if defined(DISPLAY_PIIX_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int piix_get_info(char *, char **, off_t, int);
extern int (*piix_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

static int piix_get_info (char *buffer, char **addr, off_t offset, int count)
{
	/* int rc; */
	int piix_who = ((bmide_dev->device == PCI_DEVICE_ID_INTEL_82801AA_1) ||
			(bmide_dev->device == PCI_DEVICE_ID_INTEL_82801AB_1) ||
			(bmide_dev->device == PCI_DEVICE_ID_INTEL_82371AB)) ? 4 : 3;
	char *p = buffer;
	p += sprintf(p, "\n                                Intel PIIX%d Chipset.\n", piix_who);
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n\n");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "\n");
	p += sprintf(p, "\n");

/*
 *	FIXME.... Add configuration junk data....blah blah......
 */

	return p-buffer;	 /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_PIIX_TIMINGS) && defined(CONFIG_PROC_FS) */

/*
 *  Used to set Fifo configuration via kernel command line:
 */

byte piix_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

#ifdef CONFIG_BLK_DEV_PIIX_TUNING
/*
 *
 */
static byte piix_dma_2_pio (byte xfer_rate) {
	switch(xfer_rate) {
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
		case XFER_MW_DMA_2:
		case XFER_PIO_4:
			return 4;
		case XFER_MW_DMA_1:
		case XFER_PIO_3:
			return 3;
		case XFER_SW_DMA_2:
		case XFER_PIO_2:
			return 2;
		case XFER_MW_DMA_0:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
		case XFER_PIO_1:
		case XFER_PIO_0:
		case XFER_PIO_SLOW:
		default:
			return 0;
	}
}
#endif /* CONFIG_BLK_DEV_PIIX_TUNING */

/*
 *  Based on settings done by AMI BIOS
 *  (might be usefull if drive is not registered in CMOS for any reason).
 */
static void piix_tune_drive (ide_drive_t *drive, byte pio)
{
	unsigned long flags;
	u16 master_data;
	byte slave_data;
	int is_slave		= (&HWIF(drive)->drives[1] == drive);
	int master_port		= HWIF(drive)->index ? 0x42 : 0x40;
	int slave_port		= 0x44;
				 /* ISP  RTC */
	byte timings[][2]	= { { 0, 0 },
				    { 0, 0 },
				    { 1, 0 },
				    { 2, 1 },
				    { 2, 3 }, };

	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
	pci_read_config_word(HWIF(drive)->pci_dev, master_port, &master_data);
	if (is_slave) {
		master_data = master_data | 0x4000;
		if (pio > 1)
			/* enable PPE, IE and TIME */
			master_data = master_data | 0x0070;
		pci_read_config_byte(HWIF(drive)->pci_dev, slave_port, &slave_data);
		slave_data = slave_data & (HWIF(drive)->index ? 0x0f : 0xf0);
		slave_data = slave_data | ((timings[pio][0] << 2) | (timings[pio][1]
					   << (HWIF(drive)->index ? 4 : 0)));
	} else {
		master_data = master_data & 0xccf8;
		if (pio > 1)
			/* enable PPE, IE and TIME */
			master_data = master_data | 0x0007;
		master_data = master_data | (timings[pio][0] << 12) |
			      (timings[pio][1] << 8);
	}
	save_flags(flags);
	cli();
	pci_write_config_word(HWIF(drive)->pci_dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(HWIF(drive)->pci_dev, slave_port, slave_data);
	restore_flags(flags);
}

#ifdef CONFIG_BLK_DEV_PIIX_TUNING

static int piix_config_drive_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	int			sitre;
	short			reg4042, reg44, reg48, reg4a;
	byte			speed;
	int			u_speed;

	byte maslave		= hwif->channel ? 0x42 : 0x40;
	byte udma_66		= ((id->word93 & 0x2000) && (hwif->udma_four)) ? 1 : 0;
	int ultra		= ((dev->device == PCI_DEVICE_ID_INTEL_82371AB) ||
				   (dev->device == PCI_DEVICE_ID_INTEL_82801AA_1)) ? 1 : 0;
	int ultra66		= (dev->device == PCI_DEVICE_ID_INTEL_82801AB_1) ? 1 :  0; 
	int drive_number	= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
	int a_speed		= 2 << (drive_number * 4);
	int u_flag		= 1 << drive_number;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre =  (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);

	if (id->dma_ultra && (ultra)) {
		if (!(reg48 & u_flag)) {
			pci_write_config_word(dev, 0x48, reg48|u_flag);
		}
	} else {
		if (reg48 & u_flag) {
			pci_write_config_word(dev, 0x48, reg48 & ~u_flag);
		}
	}

	if (((id->dma_ultra & 0x0010) || (id->dma_ultra & 0x0008) || (id->dma_ultra & 0x0004)) && (ultra)) {
		u_speed = 2 << (drive_number * 4);
		if (!(reg4a & u_speed)) {
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		u_speed = 1 << (drive_number * 4);
		if (!(reg4a & u_speed)) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
		u_speed = 0 << (drive_number * 4);
		if (!(reg4a & u_speed)) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		speed = XFER_MW_DMA_1;
	} else if (id->dma_1word & 0x0004) {
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		speed = XFER_SW_DMA_2;
        } else {
		speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);
	}

	piix_tune_drive(drive, piix_dma_2_pio(speed));

	(void) ide_config_drive_speed(drive, speed);

#if PIIX_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d ", drive->name, ide_xfer_verbose(speed), drive_number);
	printk("\n");
#endif /* PIIX_DEBUG_DRIVE_INFO */

	return ((int)	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int piix_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			 return ide_dmaproc((ide_dma_action_t) piix_config_drive_for_dma(drive), drive);
		default :
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_PIIX_TUNING */

unsigned int __init pci_init_piix (struct pci_dev *dev, const char *name)
{
#if defined(DISPLAY_PIIX_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!piix_proc) {
		piix_proc = 1;
		bmide_dev = dev;
		piix_display_info = &piix_get_info;
	}
#endif /* DISPLAY_PIIX_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

unsigned int __init ata66_piix (ide_hwif_t *hwif)
{
	if (0)
		return 1;
	return 0;
}

void __init ide_init_piix (ide_hwif_t *hwif)
{
	hwif->tuneproc = &piix_tune_drive;

	if (hwif->dma_base) {
#ifdef CONFIG_BLK_DEV_PIIX_TUNING
		hwif->dmaproc = &piix_dmaproc;
#endif /* CONFIG_BLK_DEV_PIIX_TUNING */
		hwif->drives[0].autotune = 0;
		hwif->drives[1].autotune = 0;
	} else {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;
}
