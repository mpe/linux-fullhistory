/*
 * linux/drivers/block/osb4.c		Version 0.2	17 Oct 2000
 *
 *  Copyright (C) 2000 Cobalt Networks, Inc. <asun@cobalt.com>
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  interface borrowed from alim15x3.c:
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 *
 *  IDE support for the ServerWorks OSB4 IDE chipset
 *
 * here's the default lspci:
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0211 (prog-if 8a [Master SecP PriP])
 *	Control: I/O+ Mem- BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR+ FastB2B-
 *	Status: Cap- 66Mhz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
 *	Latency: 255
 *	Region 4: I/O ports at c200
 * 00: 66 11 11 02 05 01 00 02 00 8a 01 01 00 ff 80 00
 * 10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 20: 01 c2 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 40: 99 99 99 99 ff ff ff ff 0c 0c 00 00 00 00 00 00
 * 50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/delay.h>
#include <asm/io.h>

#include "ide_modes.h"

#define OSB4_DEBUG_DRIVE_INFO		0

#define DISPLAY_OSB4_TIMINGS

#if defined(DISPLAY_OSB4_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static byte osb4_revision = 0;
static struct pci_dev *bmide_dev;

static int osb4_get_info(char *, char **, off_t, int, int);
extern int (*osb4_display_info)(char *, char **, off_t, int, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);

static int osb4_get_info (char *buffer, char **addr, off_t offset, int count, int dummy)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u16 reg56;
	u8  c0 = 0, c1 = 0, reg54;

	pci_read_config_byte(bmide_dev, 0x54, &reg54);
	pci_read_config_word(bmide_dev, 0x56, &reg56);

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	p += sprintf(p, "\n                                ServerWorks OSB4 Chipset.\n");
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ",
			(c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ",
			(c1&0x40) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s              %s             %s               %s\n",
			(reg54 & 0x01) ? "yes" : "no ",
			(reg54 & 0x02) ? "yes" : "no ",
			(reg54 & 0x04) ? "yes" : "no ",
			(reg54 & 0x08) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s                %s               %s                 %s\n",
		     (reg56 & 0x0002) ? "2" : ((reg56 & 0x0001) ? "1" : 
					       ((reg56 & 0x000f) ? "X" : "0")),
		     (reg56 & 0x0020) ? "2" : ((reg56 & 0x0010) ? "1" : 
					       ((reg56 & 0x00f0) ? "X" : "0")),
		     (reg56 & 0x0200) ? "2" : ((reg56 & 0x0100) ? "1" : 
					       ((reg56 & 0x0f00) ? "X" : "0")),
		     (reg56 & 0x2000) ? "2" : ((reg56 & 0x1000) ? "1" : 
					       ((reg56 & 0xf000) ? "X" : "0")));
	return p-buffer;	 /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_OSB4_TIMINGS) && defined(CONFIG_PROC_FS) */

byte osb4_proc = 0;

extern char *ide_xfer_verbose (byte xfer_rate);

static void osb4_tune_drive (ide_drive_t *drive, byte pio)
{
        /* command/recover widths */
	byte timings[]	= { 0x5d, 0x47, 0x34, 0x22, 0x20 };
	int port		= HWIF(drive)->index ? 0x42 : 0x40;

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	if (&HWIF(drive)->drives[0] == drive)  /* master drive */
		port++;
	pci_write_config_byte(HWIF(drive)->pci_dev, port, timings[pio]);
}

#if defined(CONFIG_BLK_DEV_IDEDMA) && defined(CONFIG_BLK_DEV_OSB4)
static int osb4_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte is_slave           = (&HWIF(drive)->drives[1] == drive) ? 1 : 0;
	byte bit8, enable;
	int err;
	
	/* clear udma register if we don't want udma */
	if (speed < XFER_UDMA_0) {
		enable = 0x1 << (is_slave + (hwif->channel ? 2 : 0));
		pci_read_config_byte(dev, 0x54, &bit8);
		pci_write_config_byte(dev, 0x54, bit8 & ~enable);
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_MW_DMA_0) {
		byte channel = hwif->channel ? 0x46 : 0x44;
		if (!is_slave)
			channel++;

		switch (speed) {
		case XFER_MW_DMA_0:
			bit8 = 0x77;
			break;
		case XFER_MW_DMA_1:
			bit8 = 0x21;
			break;
		case XFER_MW_DMA_2:
		default:
			bit8 = 0x20;
			break;
		}
		pci_write_config_byte(dev, channel, bit8);
	}

	if (speed >= XFER_UDMA_0) {
		byte channel = hwif->channel ? 0x57 : 0x56;
		int slave = is_slave ? 4 : 0;

		pci_read_config_byte(dev, channel, &bit8);
		bit8 &= ~(0xf << slave);
		switch (speed) {
		case XFER_UDMA_0:
			break;
		case XFER_UDMA_1:
			bit8 |= 0x1 << slave;
			break;
		case XFER_UDMA_2:
		default:
			bit8 |= 0x2 << slave;
			break;
		}
		pci_write_config_byte(dev, channel, bit8);

		enable = 0x1 << (is_slave + (hwif->channel ? 2 : 0));
		pci_read_config_byte(dev, 0x54, &bit8);
		pci_write_config_byte(dev, 0x54, bit8 | enable);
	}
#endif

#if OSB4_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d\n", drive->name, ide_xfer_verbose(speed), drive->dn);
#endif /* OSB4_DEBUG_DRIVE_INFO */
	if (!drive->init_speed)
		drive->init_speed = speed;
	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;
	return err;
}

static int osb4_config_drive_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte			speed;

	byte udma_66		= eighty_ninty_three(drive);
	/* need specs to figure out if osb4 is capable of ata/66/100 */
	int ultra100		= 0;
	int ultra66		= 0;
	int ultra		= 1;

	if ((id->dma_ultra & 0x0020) && (udma_66) && (ultra100)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) && (ultra)) {
		speed = ((udma_66) && (ultra66)) ? XFER_UDMA_4 : XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0008) && (ultra)) {
		speed = ((udma_66) && (ultra66)) ? XFER_UDMA_3 : XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0004) && (ultra)) {
		speed = XFER_UDMA_2;
	} else if ((id->dma_ultra & 0x0002) && (ultra)) {
		speed = XFER_UDMA_1;
	} else if ((id->dma_ultra & 0x0001) && (ultra)) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_1word & 0x0004) {
		speed = XFER_SW_DMA_2;
	} else {
		speed = XFER_PIO_0 + ide_get_best_pio_mode(drive, 255, 5, NULL);
	}

	(void) osb4_tune_chipset(drive, speed);

	return ((int)	((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int osb4_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			 return ide_dmaproc((ide_dma_action_t) osb4_config_drive_for_dma(drive), drive);
		default :
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* defined(CONFIG_BLK_DEV_IDEDMA) && (CONFIG_BLK_DEV_OSB4) */

unsigned int __init pci_init_osb4 (struct pci_dev *dev, const char *name)
{
	u16 word;
	byte bit8;

	pci_read_config_byte(dev, PCI_REVISION_ID, &osb4_revision);

	/* setup command register. just make sure that bus master and
	 * i/o ports are on. */
	pci_read_config_word(dev, PCI_COMMAND, &word);
	if ((word & (PCI_COMMAND_MASTER | PCI_COMMAND_IO)) !=
	     (PCI_COMMAND_MASTER | PCI_COMMAND_IO))
		pci_write_config_word(dev, PCI_COMMAND, word |
				      PCI_COMMAND_MASTER | PCI_COMMAND_IO);
	
	/* make sure that we're in pci native mode for both the primary
	 * and secondary channel. */
	pci_read_config_byte(dev, PCI_CLASS_PROG, &bit8);
	if ((bit8 & 0x5) != 0x5)
		pci_write_config_byte(dev, PCI_CLASS_PROG, bit8 | 0x5);

	/* setup up our latency. the default is 255 which is a bit large.
	 * set it to 64 instead. */
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &bit8);
	if (bit8 != 0x40)
	    pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);

#if defined(DISPLAY_OSB4_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!osb4_proc) {
		osb4_proc = 1;
		bmide_dev = dev;
		osb4_display_info = &osb4_get_info;
	}
#endif /* DISPLAY_OSB4_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

unsigned int __init ata66_osb4 (ide_hwif_t *hwif)
{
	return 0;
}

void __init ide_init_osb4 (ide_hwif_t *hwif)
{
	if (!hwif->irq)
		hwif->irq = hwif->channel ? 15 : 14;

	hwif->tuneproc = &osb4_tune_drive;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	if (!hwif->dma_base)
		return;

#ifndef CONFIG_BLK_DEV_IDEDMA
	hwif->autodma = 0;
#else /* CONFIG_BLK_DEV_IDEDMA */
#ifdef CONFIG_BLK_DEV_OSB4
	hwif->autodma = 1;
	hwif->dmaproc = &osb4_dmaproc;
	hwif->speedproc = &osb4_tune_chipset;
#endif /* CONFIG_BLK_DEV_OSB4 */
#endif /* !CONFIG_BLK_DEV_IDEDMA */
}
