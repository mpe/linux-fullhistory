/*
 * linux/drivers/block/piix.c	Version 0.23	May 29, 1999
 *
 *  Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
 *  Copyright (C) 1998-1999 Andre Hedrick, Author and Maintainer
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
 */

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

extern char *ide_xfer_verbose (byte xfer_rate);

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

#if 1
	pio = ide_get_best_pio_mode(drive, pio, 5, NULL);
#else
	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
#endif
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

static int piix_config_drive_for_dma(ide_drive_t *drive, int ultra)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;

	unsigned long		flags;
	int			sitre;
	short			reg4042, reg44, reg48, reg4a;
	byte			speed;
	int			u_speed;
	byte maslave		= hwif->channel ? 0x42 : 0x40;			
	int drive_number	= ((hwif->channel ? 2 : 0) + (drive->select.b.unit & 0x01));
	int a_speed		= 2 << (drive_number * 4);
	int u_flag		= 1 << drive_number;

	pci_read_config_word(dev, maslave, &reg4042);
	sitre =  (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);

	save_flags(flags);
	cli();

	if (id->dma_ultra && (ultra)) {
		if (!(reg48 & u_flag)) {
			pci_write_config_word(dev, 0x48, reg48|u_flag);
		}
	} else {
		if (reg48 & u_flag) {
			pci_write_config_word(dev, 0x48, reg48 & ~u_flag);
		}
	}

	if ((id->dma_ultra & 0x0004) && (ultra)) {
		drive->id->dma_mword &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		if (!((id->dma_ultra >> 8) & 4)) {
			drive->id->dma_ultra &= ~0x0F00;
			drive->id->dma_ultra |= 0x0404;
		}
		u_speed = 2 << (drive_number * 4);
		if (!(reg4a & u_speed)) {
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		drive->id->dma_mword &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		if (!((id->dma_ultra >> 8) & 2)) {
			drive->id->dma_ultra &= ~0x0F00;
			drive->id->dma_ultra |= 0x0202;
		}
		u_speed = 1 << (drive_number * 4);
		if (!(reg4a & u_speed)) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
		drive->id->dma_mword &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		if (!((id->dma_ultra >> 8) & 1)) {
			drive->id->dma_ultra &= ~0x0F00;
			drive->id->dma_ultra |= 0x0101;
		}
		u_speed = 0 << (drive_number * 4);
		if (!(reg4a & u_speed)) {
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
			pci_write_config_word(dev, 0x4a, reg4a|u_speed);
		}
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		drive->id->dma_ultra &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		if (!((id->dma_mword >> 8) & 4)) {
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_mword |= 0x0404;
		}
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		drive->id->dma_ultra &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		if (!((id->dma_mword >> 8) & 2)) {
			drive->id->dma_mword &= ~0x0F00;
			drive->id->dma_mword |= 0x0202;
		}
		speed = XFER_MW_DMA_1;
	} else if (id->dma_1word & 0x0004) {
		if (reg4a & a_speed)
			pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		drive->id->dma_ultra &= ~0x0F00;
		drive->id->dma_mword &= ~0x0F00;
		if (!((id->dma_1word >> 8) & 4)) {
			drive->id->dma_1word &= ~0x0F00;
			drive->id->dma_1word |= 0x0404;
		}
		speed = XFER_SW_DMA_2;
        } else {
#if 0
		speed = XFER_PIO_0;
#else
		speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);
#endif
	}

	restore_flags(flags);
	piix_tune_drive(drive, piix_dma_2_pio(speed));

	(void) ide_wait_cmd(drive, WIN_SETFEATURES, speed, SETFEATURES_XFER, 0, NULL);

#if PIIX_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d ",
		drive->name,
		ide_xfer_verbose(speed),
		drive_number);
	printk("\n");
#endif /* PIIX_DEBUG_DRIVE_INFO */

	return ((int)	((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int piix_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	int ultra = (HWIF(drive)->pci_dev->device == PCI_DEVICE_ID_INTEL_82371AB) ? 1 : 0;
	switch (func) {
		case ide_dma_check:
			 return ide_dmaproc((ide_dma_action_t) piix_config_drive_for_dma(drive, ultra), drive);
		default :
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}

void ide_init_piix (ide_hwif_t *hwif)
{
	hwif->tuneproc = &piix_tune_drive;
	if (hwif->dma_base) {
		hwif->dmaproc = &piix_dmaproc;
	}
}
