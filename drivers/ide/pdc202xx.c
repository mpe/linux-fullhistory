/*
 *  linux/drivers/ide/pdc202xx.c	Version 0.30	Mar. 18, 2000
 *
 *  Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  Promise Ultra33 cards with BIOS v1.20 through 1.28 will need this
 *  compiled into the kernel if you have more than one card installed.
 *  Note that BIOS v1.29 is reported to fix the problem.  Since this is
 *  safe chipset tuning, including this support is harmless
 *
 *  Promise Ultra66 cards with BIOS v1.11 this
 *  compiled into the kernel if you have more than one card installed.
 *
 *  Promise Ultra100 cards.
 *
 *  The latest chipset code will support the following ::
 *  Three Ultra33 controllers and 12 drives.
 *  8 are UDMA supported and 4 are limited to DMA mode 2 multi-word.
 *  The 8/4 ratio is a BIOS code limit by promise.
 *
 *  UNLESS you enable "CONFIG_PDC202XX_BURST"
 *
 */

/*
 *  Portions Copyright (C) 1999 Promise Technology, Inc.
 *  Author: Frank Tiernan (frankt@promise.com)
 *  Released under terms of General Public License
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

#include "ata-timing.h"

#define PDC202XX_DEBUG_DRIVE_INFO		0
#define PDC202XX_DECODE_REGISTER_INFO		0

#define DISPLAY_PDC202XX_TIMINGS

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

#if defined(DISPLAY_PDC202XX_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int pdc202xx_get_info(char *, char **, off_t, int);
extern int (*pdc202xx_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

char *pdc202xx_pio_verbose (u32 drive_pci)
{
	if ((drive_pci & 0x000ff000) == 0x000ff000) return("NOTSET");
	if ((drive_pci & 0x00000401) == 0x00000401) return("PIO 4");
	if ((drive_pci & 0x00000602) == 0x00000602) return("PIO 3");
	if ((drive_pci & 0x00000803) == 0x00000803) return("PIO 2");
	if ((drive_pci & 0x00000C05) == 0x00000C05) return("PIO 1");
	if ((drive_pci & 0x00001309) == 0x00001309) return("PIO 0");
	return("PIO ?");
}

char *pdc202xx_dma_verbose (u32 drive_pci)
{
	if ((drive_pci & 0x00036000) == 0x00036000) return("MWDMA 2");
	if ((drive_pci & 0x00046000) == 0x00046000) return("MWDMA 1");
	if ((drive_pci & 0x00056000) == 0x00056000) return("MWDMA 0");
	if ((drive_pci & 0x00056000) == 0x00056000) return("SWDMA 2");
	if ((drive_pci & 0x00068000) == 0x00068000) return("SWDMA 1");
	if ((drive_pci & 0x000BC000) == 0x000BC000) return("SWDMA 0");
	return("PIO---");
}

char *pdc202xx_ultra_verbose (u32 drive_pci, u16 slow_cable)
{
	if ((drive_pci & 0x000ff000) == 0x000ff000)
		return("NOTSET");
	if ((drive_pci & 0x00012000) == 0x00012000)
		return((slow_cable) ? "UDMA 2" : "UDMA 4");
	if ((drive_pci & 0x00024000) == 0x00024000)
		return((slow_cable) ? "UDMA 1" : "UDMA 3");
	if ((drive_pci & 0x00036000) == 0x00036000)
		return("UDMA 0");
	return(pdc202xx_dma_verbose(drive_pci));
}

static char * pdc202xx_info (char *buf, struct pci_dev *dev)
{
	char *p = buf;

	u32 bibma  = pci_resource_start(dev, 4);
	u32 reg60h = 0, reg64h = 0, reg68h = 0, reg6ch = 0;
	u16 reg50h = 0, pmask = (1<<10), smask = (1<<11);
	u8 hi = 0, lo = 0;

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	u8 c0	= inb_p((unsigned short)bibma + 0x02);
	u8 c1	= inb_p((unsigned short)bibma + 0x0a);

	u8 sc11	= inb_p((unsigned short)bibma + 0x11);
	u8 sc1a	= inb_p((unsigned short)bibma + 0x1a);
	u8 sc1b	= inb_p((unsigned short)bibma + 0x1b);
	u8 sc1c	= inb_p((unsigned short)bibma + 0x1c); 
	u8 sc1d	= inb_p((unsigned short)bibma + 0x1d);
	u8 sc1e	= inb_p((unsigned short)bibma + 0x1e);
	u8 sc1f	= inb_p((unsigned short)bibma + 0x1f);

	pci_read_config_word(dev, 0x50, &reg50h);
	pci_read_config_dword(dev, 0x60, &reg60h);
	pci_read_config_dword(dev, 0x64, &reg64h);
	pci_read_config_dword(dev, 0x68, &reg68h);
	pci_read_config_dword(dev, 0x6c, &reg6ch);

	switch(dev->device) {
		case PCI_DEVICE_ID_PROMISE_20267:
			p += sprintf(p, "\n                                PDC20267 Chipset.\n");
			break;
		case PCI_DEVICE_ID_PROMISE_20265:
			p += sprintf(p, "\n                                PDC20265 Chipset.\n");
			break;
		case PCI_DEVICE_ID_PROMISE_20262:
			p += sprintf(p, "\n                                PDC20262 Chipset.\n");
			break;
		case PCI_DEVICE_ID_PROMISE_20246:
			p += sprintf(p, "\n                                PDC20246 Chipset.\n");
			reg50h |= 0x0c00;
			break;
		default:
			p += sprintf(p, "\n                                PDC202XX Chipset.\n");
			break;
	}

	p += sprintf(p, "------------------------------- General Status ---------------------------------\n");
	p += sprintf(p, "Burst Mode                           : %sabled\n", (sc1f & 0x01) ? "en" : "dis");
	p += sprintf(p, "Host Mode                            : %s\n", (sc1f & 0x08) ? "Tri-Stated" : "Normal");
	p += sprintf(p, "Bus Clocking                         : %s\n",
		((sc1f & 0xC0) == 0xC0) ? "100 External" :
		((sc1f & 0x80) == 0x80) ? "66 External" :
		((sc1f & 0x40) == 0x40) ? "33 External" : "33 PCI Internal");
	p += sprintf(p, "IO pad select                        : %s mA\n",
		((sc1c & 0x03) == 0x03) ? "10" :
		((sc1c & 0x02) == 0x02) ? "8" :
		((sc1c & 0x01) == 0x01) ? "6" :
		((sc1c & 0x00) == 0x00) ? "4" : "??");
	SPLIT_BYTE(sc1e, hi, lo);
	p += sprintf(p, "Status Polling Period                : %d\n", hi);
	p += sprintf(p, "Interrupt Check Status Polling Delay : %d\n", lo);
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %s                         %s\n",
		(c0&0x80)?"disabled":"enabled ",
		(c1&0x80)?"disabled":"enabled ");
	p += sprintf(p, "66 Clocking     %s                         %s\n",
		(sc11&0x02)?"enabled ":"disabled",
		(sc11&0x08)?"enabled ":"disabled");
	p += sprintf(p, "           Mode %s                      Mode %s\n",
		(sc1a & 0x01) ? "MASTER" : "PCI   ",
		(sc1b & 0x01) ? "MASTER" : "PCI   ");
	p += sprintf(p, "                %s                     %s\n",
		(sc1d & 0x08) ? "Error       " :
		((sc1d & 0x05) == 0x05) ? "Not My INTR " :
		(sc1d & 0x04) ? "Interrupting" :
		(sc1d & 0x02) ? "FIFO Full   " :
		(sc1d & 0x01) ? "FIFO Empty  " : "????????????",
		(sc1d & 0x80) ? "Error       " :
		((sc1d & 0x50) == 0x50) ? "Not My INTR " :
		(sc1d & 0x40) ? "Interrupting" :
		(sc1d & 0x20) ? "FIFO Full   " :
		(sc1d & 0x10) ? "FIFO Empty  " : "????????????");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
		(c0&0x20)?"yes":"no ",(c0&0x40)?"yes":"no ",(c1&0x20)?"yes":"no ",(c1&0x40)?"yes":"no ");
	p += sprintf(p, "DMA Mode:       %s           %s          %s            %s\n",
		pdc202xx_ultra_verbose(reg60h, (reg50h & pmask)),
		pdc202xx_ultra_verbose(reg64h, (reg50h & pmask)),
		pdc202xx_ultra_verbose(reg68h, (reg50h & smask)),
		pdc202xx_ultra_verbose(reg6ch, (reg50h & smask)));
	p += sprintf(p, "PIO Mode:       %s            %s           %s            %s\n",
		pdc202xx_pio_verbose(reg60h),
		pdc202xx_pio_verbose(reg64h),
		pdc202xx_pio_verbose(reg68h),
		pdc202xx_pio_verbose(reg6ch));
#if 0
	p += sprintf(p, "--------------- Can ATAPI DMA ---------------\n");
#endif
	return (char *)p;
}

static char * pdc202xx_info_new (char *buf, struct pci_dev *dev)
{
	char *p = buf;
//	u32 bibma = pci_resource_start(dev, 4);

//	u32 reg60h = 0, reg64h = 0, reg68h = 0, reg6ch = 0;
//	u16 reg50h = 0, word88 = 0;
//	int udmasel[4]={0,0,0,0}, piosel[4]={0,0,0,0}, i=0, hd=0;

	switch(dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
			p += sprintf(p, "\n                                PDC20275 Chipset.\n");
			break;
		case PCI_DEVICE_ID_PROMISE_20276:
			p += sprintf(p, "\n                                PDC20276 Chipset.\n");
			break;
		case PCI_DEVICE_ID_PROMISE_20269:
			p += sprintf(p, "\n                                PDC20269 TX2 Chipset.\n");
			break;
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			p += sprintf(p, "\n                                PDC20268 TX2 Chipset.\n");
			break;
default:
			p += sprintf(p, "\n                                PDC202XX Chipset.\n");
			break;
	}
	return (char *)p;
}

static int pdc202xx_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	switch(bmide_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			p = pdc202xx_info_new(buffer, bmide_dev);
			break;
		default:
			p = pdc202xx_info(buffer, bmide_dev);
			break;
	}
	return p-buffer;	/* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_PDC202XX_TIMINGS) && defined(CONFIG_PROC_FS) */

byte pdc202xx_proc = 0;

const char *pdc_quirk_drives[] = {
	"QUANTUM FIREBALLlct08 08",
	"QUANTUM FIREBALLP KA6.4",
	"QUANTUM FIREBALLP LM20.4",
	"QUANTUM FIREBALLP KX20.5",
	"QUANTUM FIREBALLP KX27.3",
	"QUANTUM FIREBALLP LM20.5",
	NULL
};

extern char *ide_xfer_verbose (byte xfer_rate);

/* A Register */
#define	SYNC_ERRDY_EN	0xC0

#define	SYNC_IN		0x80	/* control bit, different for master vs. slave drives */
#define	ERRDY_EN	0x40	/* control bit, different for master vs. slave drives */
#define	IORDY_EN	0x20	/* PIO: IOREADY */
#define	PREFETCH_EN	0x10	/* PIO: PREFETCH */

#define	PA3		0x08	/* PIO"A" timing */
#define	PA2		0x04	/* PIO"A" timing */
#define	PA1		0x02	/* PIO"A" timing */
#define	PA0		0x01	/* PIO"A" timing */

/* B Register */

#define	MB2		0x80	/* DMA"B" timing */
#define	MB1		0x40	/* DMA"B" timing */
#define	MB0		0x20	/* DMA"B" timing */

#define	PB4		0x10	/* PIO_FORCE 1:0 */

#define	PB3		0x08	/* PIO"B" timing */	/* PIO flow Control mode */
#define	PB2		0x04	/* PIO"B" timing */	/* PIO 4 */
#define	PB1		0x02	/* PIO"B" timing */	/* PIO 3 half */
#define	PB0		0x01	/* PIO"B" timing */	/* PIO 3 other half */

/* C Register */
#define	IORDYp_NO_SPEED	0x4F
#define	SPEED_DIS	0x0F

#define	DMARQp		0x80
#define	IORDYp		0x40
#define	DMAR_EN		0x20
#define	DMAW_EN		0x10

#define	MC3		0x08	/* DMA"C" timing */
#define	MC2		0x04	/* DMA"C" timing */
#define	MC1		0x02	/* DMA"C" timing */
#define	MC0		0x01	/* DMA"C" timing */

#if PDC202XX_DECODE_REGISTER_INFO

#define REG_A		0x01
#define REG_B		0x02
#define REG_C		0x04
#define REG_D		0x08

static void decode_registers (byte registers, byte value)
{
	byte	bit = 0, bit1 = 0, bit2 = 0;

	switch(registers) {
		case REG_A:
			bit2 = 0;
			printk("A Register ");
			if (value & 0x80) printk("SYNC_IN ");
			if (value & 0x40) printk("ERRDY_EN ");
			if (value & 0x20) printk("IORDY_EN ");
			if (value & 0x10) printk("PREFETCH_EN ");
			if (value & 0x08) { printk("PA3 ");bit2 |= 0x08; }
			if (value & 0x04) { printk("PA2 ");bit2 |= 0x04; }
			if (value & 0x02) { printk("PA1 ");bit2 |= 0x02; }
			if (value & 0x01) { printk("PA0 ");bit2 |= 0x01; }
			printk("PIO(A) = %d ", bit2);
			break;
		case REG_B:
			bit1 = 0;bit2 = 0;
			printk("B Register ");
			if (value & 0x80) { printk("MB2 ");bit1 |= 0x80; }
			if (value & 0x40) { printk("MB1 ");bit1 |= 0x40; }
			if (value & 0x20) { printk("MB0 ");bit1 |= 0x20; }
			printk("DMA(B) = %d ", bit1 >> 5);
			if (value & 0x10) printk("PIO_FORCED/PB4 ");
			if (value & 0x08) { printk("PB3 ");bit2 |= 0x08; }
			if (value & 0x04) { printk("PB2 ");bit2 |= 0x04; }
			if (value & 0x02) { printk("PB1 ");bit2 |= 0x02; }
			if (value & 0x01) { printk("PB0 ");bit2 |= 0x01; }
			printk("PIO(B) = %d ", bit2);
			break;
		case REG_C:
			bit2 = 0;
			printk("C Register ");
			if (value & 0x80) printk("DMARQp ");
			if (value & 0x40) printk("IORDYp ");
			if (value & 0x20) printk("DMAR_EN ");
			if (value & 0x10) printk("DMAW_EN ");

			if (value & 0x08) { printk("MC3 ");bit2 |= 0x08; }
			if (value & 0x04) { printk("MC2 ");bit2 |= 0x04; }
			if (value & 0x02) { printk("MC1 ");bit2 |= 0x02; }
			if (value & 0x01) { printk("MC0 ");bit2 |= 0x01; }
			printk("DMA(C) = %d ", bit2);
			break;
		case REG_D:
			printk("D Register ");
			break;
		default:
			return;
	}
	printk("\n        %s ", (registers & REG_D) ? "DP" :
				(registers & REG_C) ? "CP" :
				(registers & REG_B) ? "BP" :
				(registers & REG_A) ? "AP" : "ERROR");
	for (bit=128;bit>0;bit/=2)
		printk("%s", (value & bit) ? "1" : "0");
	printk("\n");
}

#endif /* PDC202XX_DECODE_REGISTER_INFO */

static int check_in_drive_lists (ide_drive_t *drive, const char **list)
{
	struct hd_driveid *id = drive->id;

	if (pdc_quirk_drives == list) {
		while (*list) {
			if (strstr(id->model, *list++)) {
				return 2;
			}
		}
	} else {
		while (*list) {
			if (!strcmp(*list++,id->model)) {
				return 1;
			}
		}
	}
	return 0;
}

static int pdc202xx_tune_chipset (ide_drive_t *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;

	unsigned int		drive_conf;
	int			err;
	byte			drive_pci, AP, BP, CP, DP;
	byte			TA = 0, TB = 0, TC = 0;

	switch (drive->dn) {
		case 0: drive_pci = 0x60; break;
		case 1: drive_pci = 0x64; break;
		case 2: drive_pci = 0x68; break;
		case 3: drive_pci = 0x6c; break;
		default: return -1;
	}

	if ((drive->type != ATA_DISK) && (speed < XFER_SW_DMA_0))
		return -1;

	pci_read_config_dword(dev, drive_pci, &drive_conf);
	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
	pci_read_config_byte(dev, (drive_pci)|0x03, &DP);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed >= XFER_SW_DMA_0) {
		if ((BP & 0xF0) && (CP & 0x0F)) {
			/* clear DMA modes of upper 842 bits of B Register */
			/* clear PIO forced mode upper 1 bit of B Register */
			pci_write_config_byte(dev, (drive_pci)|0x01, BP & ~0xF0);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);

			/* clear DMA modes of lower 8421 bits of C Register */
			pci_write_config_byte(dev, (drive_pci)|0x02, CP & ~0x0F);
			pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
		}
	} else {
#else
	{
#endif /* CONFIG_BLK_DEV_IDEDMA */
		if ((AP & 0x0F) || (BP & 0x07)) {
			/* clear PIO modes of lower 8421 bits of A Register */
			pci_write_config_byte(dev, (drive_pci), AP & ~0x0F);
			pci_read_config_byte(dev, (drive_pci), &AP);

			/* clear PIO modes of lower 421 bits of B Register */
			pci_write_config_byte(dev, (drive_pci)|0x01, BP & ~0x07);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);

			pci_read_config_byte(dev, (drive_pci), &AP);
			pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
		}
	}

	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		/* case XFER_UDMA_6: */
		case XFER_UDMA_5:
		case XFER_UDMA_4:	TB = 0x20; TC = 0x01; break;	/* speed 8 == UDMA mode 4 */
		case XFER_UDMA_3:	TB = 0x40; TC = 0x02; break;	/* speed 7 == UDMA mode 3 */
		case XFER_UDMA_2:	TB = 0x20; TC = 0x01; break;	/* speed 6 == UDMA mode 2 */
		case XFER_UDMA_1:	TB = 0x40; TC = 0x02; break;	/* speed 5 == UDMA mode 1 */
		case XFER_UDMA_0:	TB = 0x60; TC = 0x03; break;	/* speed 4 == UDMA mode 0 */
		case XFER_MW_DMA_2:	TB = 0x60; TC = 0x03; break;	/* speed 4 == MDMA mode 2 */
		case XFER_MW_DMA_1:	TB = 0x60; TC = 0x04; break;	/* speed 3 == MDMA mode 1 */
		case XFER_MW_DMA_0:	TB = 0x60; TC = 0x05; break;	/* speed 2 == MDMA mode 0 */
		case XFER_SW_DMA_2:	TB = 0x60; TC = 0x05; break;	/* speed 0 == SDMA mode 2 */
		case XFER_SW_DMA_1:	TB = 0x80; TC = 0x06; break;	/* speed 1 == SDMA mode 1 */
		case XFER_SW_DMA_0:	TB = 0xC0; TC = 0x0B; break;	/* speed 0 == SDMA mode 0 */
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:	TA = 0x01; TB = 0x04; break;
		case XFER_PIO_3:	TA = 0x02; TB = 0x06; break;
		case XFER_PIO_2:	TA = 0x03; TB = 0x08; break;
		case XFER_PIO_1:	TA = 0x05; TB = 0x0C; break;
		case XFER_PIO_0:
		default:		TA = 0x09; TB = 0x13; break;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
        if (speed >= XFER_SW_DMA_0) {
		pci_write_config_byte(dev, (drive_pci)|0x01, BP|TB);
		pci_write_config_byte(dev, (drive_pci)|0x02, CP|TC);
	} else {
#else
	{
#endif /* CONFIG_BLK_DEV_IDEDMA */
		pci_write_config_byte(dev, (drive_pci), AP|TA);
		pci_write_config_byte(dev, (drive_pci)|0x01, BP|TB);
	}

#if PDC202XX_DECODE_REGISTER_INFO
	pci_read_config_byte(dev, (drive_pci), &AP);
	pci_read_config_byte(dev, (drive_pci)|0x01, &BP);
	pci_read_config_byte(dev, (drive_pci)|0x02, &CP);
	pci_read_config_byte(dev, (drive_pci)|0x03, &DP);

	decode_registers(REG_A, AP);
	decode_registers(REG_B, BP);
	decode_registers(REG_C, CP);
	decode_registers(REG_D, DP);
#endif /* PDC202XX_DECODE_REGISTER_INFO */

	if (!drive->init_speed)
		drive->init_speed = speed;
	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;

#if PDC202XX_DEBUG_DRIVE_INFO
	printk("%s: %s drive%d 0x%08x ",
		drive->name, ide_xfer_verbose(speed),
		drive->dn, drive_conf);
		pci_read_config_dword(dev, drive_pci, &drive_conf);
	printk("0x%08x\n", drive_conf);
#endif /* PDC202XX_DEBUG_DRIVE_INFO */
	return err;
}

static int pdc202xx_new_tune_chipset (ide_drive_t *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
#ifdef CONFIG_BLK_DEV_IDEDMA
	unsigned long indexreg	= (hwif->dma_base + 1);
	unsigned long datareg	= (hwif->dma_base + 3);
#else
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long high_16	= pci_resource_start(dev, 4);
	unsigned long indexreg	= high_16 + (hwif->unit ? 0x09 : 0x01);
	unsigned long datareg	= (indexreg + 2);
#endif /* CONFIG_BLK_DEV_IDEDMA */
	byte thold		= 0x10;
	byte adj		= (drive->dn%2) ? 0x08 : 0x00;

	int                     err;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (speed == XFER_UDMA_2) {
		OUT_BYTE((thold + adj), indexreg);
		OUT_BYTE((IN_BYTE(datareg) & 0x7f), datareg);
	}
	switch (speed) {
		case XFER_UDMA_7:
			speed = XFER_UDMA_6;
		case XFER_UDMA_6:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x1a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x01, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xcb, datareg);
			break;
		case XFER_UDMA_5:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x1a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x02, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xcb, datareg);
			break;
		case XFER_UDMA_4:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x1a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x03, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xcd, datareg);
			break;
		case XFER_UDMA_3:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x1a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x05, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xcd, datareg);
			break;
		case XFER_UDMA_2:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x2a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x07, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xcd, datareg);
			break;
		case XFER_UDMA_1:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x3a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x0a, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xd0, datareg);
			break;
		case XFER_UDMA_0:
			OUT_BYTE((0x10 + adj), indexreg);
			OUT_BYTE(0x4a, datareg);
			OUT_BYTE((0x11 + adj), indexreg);
			OUT_BYTE(0x0f, datareg);
			OUT_BYTE((0x12 + adj), indexreg);
			OUT_BYTE(0xd5, datareg);
			break;
		case XFER_MW_DMA_2:
			OUT_BYTE((0x0e + adj), indexreg);
			OUT_BYTE(0x69, datareg);
			OUT_BYTE((0x0f + adj), indexreg);
			OUT_BYTE(0x25, datareg);
			break;
		case XFER_MW_DMA_1:
			OUT_BYTE((0x0e + adj), indexreg);
			OUT_BYTE(0x6b, datareg);
			OUT_BYTE((0x0f+ adj), indexreg);
			OUT_BYTE(0x27, datareg);
			break;
		case XFER_MW_DMA_0:
			OUT_BYTE((0x0e + adj), indexreg);
			OUT_BYTE(0xdf, datareg);
			OUT_BYTE((0x0f + adj), indexreg);
			OUT_BYTE(0x5f, datareg);
			break;
#else
	switch (speed) {
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:
			OUT_BYTE((0x0c + adj), indexreg);
			OUT_BYTE(0x23, datareg);
			OUT_BYTE((0x0d + adj), indexreg);
			OUT_BYTE(0x09, datareg);
			OUT_BYTE((0x13 + adj), indexreg);
			OUT_BYTE(0x25, datareg);
			break;
		case XFER_PIO_3:
			OUT_BYTE((0x0c + adj), indexreg);
			OUT_BYTE(0x27, datareg);
			OUT_BYTE((0x0d + adj), indexreg);
			OUT_BYTE(0x0d, datareg);
			OUT_BYTE((0x13 + adj), indexreg);
			OUT_BYTE(0x35, datareg);
			break;
		case XFER_PIO_2:
			OUT_BYTE((0x0c + adj), indexreg);
			OUT_BYTE(0x23, datareg);
			OUT_BYTE((0x0d + adj), indexreg);
			OUT_BYTE(0x26, datareg);
			OUT_BYTE((0x13 + adj), indexreg);
			OUT_BYTE(0x64, datareg);
			break;
		case XFER_PIO_1:
			OUT_BYTE((0x0c + adj), indexreg);
			OUT_BYTE(0x46, datareg);
			OUT_BYTE((0x0d + adj), indexreg);
			OUT_BYTE(0x29, datareg);
			OUT_BYTE((0x13 + adj), indexreg);
			OUT_BYTE(0xa4, datareg);
			break;
		case XFER_PIO_0:
			OUT_BYTE((0x0c + adj), indexreg);
			OUT_BYTE(0xfb, datareg);
			OUT_BYTE((0x0d + adj), indexreg);
			OUT_BYTE(0x2b, datareg);
			OUT_BYTE((0x13 + adj), indexreg);
			OUT_BYTE(0xac, datareg);
			break;
		default:
			;
	}

	if (!drive->init_speed)
		drive->init_speed = speed;
	err = ide_config_drive_speed(drive, speed);
	drive->current_speed = speed;

	return err;
}

/*   0    1    2    3    4    5    6   7   8
 * 960, 480, 390, 300, 240, 180, 120, 90, 60
 *           180, 150, 120,  90,  60
 * DMA_Speed
 * 180, 120,  90,  90,  90,  60,  30
 *  11,   5,   4,   3,   2,   1,   0
 */
static int config_chipset_for_pio (ide_drive_t *drive, byte pio)
{
	byte speed = 0x00;

	if (pio == 255)
		speed = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		speed = XFER_PIO_0 + min_t(byte, pio, 4);

	return ((int) pdc202xx_tune_chipset(drive, speed));
}

static void pdc202xx_tune_drive (ide_drive_t *drive, byte pio)
{
	(void) config_chipset_for_pio(drive, pio);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma (ide_drive_t *drive, byte ultra)
{
	struct hd_driveid *id	= drive->id;
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long high_16   = pci_resource_start(dev, 4);
	unsigned long dma_base  = hwif->dma_base;
	unsigned long indexreg	= dma_base + 1;
	unsigned long datareg	= dma_base + 3;
	byte iordy		= 0x13;
	byte adj		= (drive->dn%2) ? 0x08 : 0x00;
	byte cable		= 0;
	byte jumpbit		= 0;
	byte unit		= (drive->select.b.unit & 0x01);
	unsigned int		drive_conf;
	byte			drive_pci = 0;
	byte			test1, test2, speed = -1;
	byte			AP;
	unsigned short		EP;
	byte CLKSPD		= 0;
	byte udma_33		= ultra;
//	byte udma_33		= ultra ? (IN_BYTE(high_16 + 0x001f) & 1) : 0;
	byte udma_66		= ((eighty_ninty_three(drive)) && udma_33) ? 1 : 0;
	byte udma_100		= 0;
	byte udma_133		= 0;
	byte mask		= hwif->unit ? 0x08 : 0x02;
	unsigned short c_mask	= hwif->unit ? (1<<11) : (1<<10);

	byte ultra_66		= ((id->dma_ultra & 0x0010) ||
				   (id->dma_ultra & 0x0008)) ? 1 : 0;
	byte ultra_100		= ((id->dma_ultra & 0x0020) ||
				   (ultra_66)) ? 1 : 0;
	byte ultra_133		= ((id->dma_ultra & 0x0040) ||
				   (ultra_100)) ? 1 : 0;

	switch(dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
			udma_133 = (udma_66) ? 1 : 0;
			udma_100 = (udma_66) ? 1 : 0;
			OUT_BYTE(0x0b, (hwif->dma_base + 1));
			cable = ((IN_BYTE((hwif->dma_base + 3)) & 0x04));
			jumpbit = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20268R:
			udma_100 = 1;
			udma_66 = 1;
			OUT_BYTE(0x0b, (hwif->dma_base + 1));
			cable = ((IN_BYTE((hwif->dma_base + 3)) & 0x04));
			jumpbit = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20268:
			udma_100 = (udma_66) ? 1 : 0;
			OUT_BYTE(0x0b, (hwif->dma_base + 1));
			cable = ((IN_BYTE((hwif->dma_base + 3)) & 0x04));
			jumpbit = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
			udma_100 = (udma_66) ? 1 : 0;
			pci_read_config_word(dev, 0x50, &EP);
			cable = (EP & c_mask);
			jumpbit = 0;
			break;
		case PCI_DEVICE_ID_PROMISE_20262:
			pci_read_config_word(dev, 0x50, &EP);
			cable = (EP & c_mask);
			jumpbit = 0;
			break;
		default:
			udma_100 = 0; udma_133 = 0; cable = 1; jumpbit = 0;
			break;
	}

	if (!jumpbit)
		CLKSPD = IN_BYTE(high_16 + 0x11);
	/*
	 * Set the control register to use the 66Mhz system
	 * clock for UDMA 3/4 mode operation. If one drive on
	 * a channel is U66 capable but the other isn't we
	 * fall back to U33 mode. The BIOS INT 13 hooks turn
	 * the clock on then off for each read/write issued. I don't
	 * do that here because it would require modifying the
	 * kernel, seperating the fop routines from the kernel or
	 * somehow hooking the fops calls. It may also be possible to
	 * leave the 66Mhz clock on and readjust the timing
	 * parameters.
	 */

	if (((ultra_66) || (ultra_100) || (ultra_133)) && (cable)) {
#ifdef DEBUG
		printk("ULTRA66: %s channel of Ultra 66 requires an 80-pin cable for Ultra66 operation.\n", hwif->unit ? "Secondary" : "Primary");
		printk("         Switching to Ultra33 mode.\n");
#endif /* DEBUG */
		/* Primary   : zero out second bit */
		/* Secondary : zero out fourth bit */
		if (!jumpbit)
			OUT_BYTE(CLKSPD & ~mask, (high_16 + 0x11));
		printk("Warning: %s channel requires an 80-pin cable for operation.\n", hwif->unit ? "Secondary":"Primary");
		printk("%s reduced to Ultra33 mode.\n", drive->name);
		udma_66 = 0; udma_100 = 0; udma_133 = 0;
	} else {
		if ((ultra_66) || (ultra_100) || (ultra_133)) {
			/*
			 * check to make sure drive on same channel
			 * is u66 capable
			 */
			if (hwif->drives[!(drive->dn%2)].id) {
				if ((hwif->drives[!(drive->dn%2)].id->dma_ultra & 0x0040) ||
				    (hwif->drives[!(drive->dn%2)].id->dma_ultra
& 0x0020) ||
				    (hwif->drives[!(drive->dn%2)].id->dma_ultra & 0x0010) ||
				    (hwif->drives[!(drive->dn%2)].id->dma_ultra & 0x0008)) {
					if (!jumpbit)
						OUT_BYTE(CLKSPD | mask, (high_16 + 0x11));
				} else {
					if (!jumpbit)
						OUT_BYTE(CLKSPD & ~mask, (high_16 + 0x11));
				}
			} else { /* udma4 drive by itself */
				if (!jumpbit)
					OUT_BYTE(CLKSPD | mask, (high_16 + 0x11));
			}
		}
	}

	if (jumpbit) {
		if (drive->type != ATA_DISK)
			return ide_dma_off_quietly;
		if (id->capability & 4) {	/* IORDY_EN & PREFETCH_EN */
			OUT_BYTE((iordy + adj), indexreg);
			OUT_BYTE((IN_BYTE(datareg)|0x03), datareg);
		}
		goto jumpbit_is_set;
	}

	switch(drive->dn) {
		case 0:	drive_pci = 0x60;
			pci_read_config_dword(dev, drive_pci, &drive_conf);
			if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
				goto chipset_is_set;
			pci_read_config_byte(dev, (drive_pci), &test1);
			if (!(test1 & SYNC_ERRDY_EN))
				pci_write_config_byte(dev, (drive_pci), test1|SYNC_ERRDY_EN);
			break;
		case 1:	drive_pci = 0x64;
			pci_read_config_dword(dev, drive_pci, &drive_conf);
			if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
				goto chipset_is_set;
			pci_read_config_byte(dev, 0x60, &test1);
			pci_read_config_byte(dev, (drive_pci), &test2);
			if ((test1 & SYNC_ERRDY_EN) && !(test2 & SYNC_ERRDY_EN))
				pci_write_config_byte(dev, (drive_pci), test2|SYNC_ERRDY_EN);
			break;
		case 2:	drive_pci = 0x68;
			pci_read_config_dword(dev, drive_pci, &drive_conf);
			if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
				goto chipset_is_set;
			pci_read_config_byte(dev, (drive_pci), &test1);
			if (!(test1 & SYNC_ERRDY_EN))
				pci_write_config_byte(dev, (drive_pci), test1|SYNC_ERRDY_EN);
			break;
		case 3:	drive_pci = 0x6c;
			pci_read_config_dword(dev, drive_pci, &drive_conf);
			if ((drive_conf != 0x004ff304) && (drive_conf != 0x004ff3c4))
				goto chipset_is_set;
			pci_read_config_byte(dev, 0x68, &test1);
			pci_read_config_byte(dev, (drive_pci), &test2);
			if ((test1 & SYNC_ERRDY_EN) && !(test2 & SYNC_ERRDY_EN))
				pci_write_config_byte(dev, (drive_pci), test2|SYNC_ERRDY_EN);
			break;
		default:
			return ide_dma_off;
	}

chipset_is_set:

	if (drive->type != ATA_DISK)
		return ide_dma_off_quietly;

	pci_read_config_byte(dev, (drive_pci), &AP);
	if (id->capability & 4)	/* IORDY_EN */
		pci_write_config_byte(dev, (drive_pci), AP|IORDY_EN);
	pci_read_config_byte(dev, (drive_pci), &AP);
	if (drive->type == ATA_DISK)	/* PREFETCH_EN */
		pci_write_config_byte(dev, (drive_pci), AP|PREFETCH_EN);

jumpbit_is_set:

	if ((id->dma_ultra & 0x0040)&&(udma_133))	speed = XFER_UDMA_6;
	else if ((id->dma_ultra & 0x0020)&&(udma_100))	speed = XFER_UDMA_5;
	else if ((id->dma_ultra & 0x0010)&&(udma_66))	speed = XFER_UDMA_4;
	else if ((id->dma_ultra & 0x0008)&&(udma_66))	speed = XFER_UDMA_3;
	else if ((id->dma_ultra & 0x0004)&&(udma_33))	speed = XFER_UDMA_2;
	else if ((id->dma_ultra & 0x0002)&&(udma_33))	speed = XFER_UDMA_1;
	else if ((id->dma_ultra & 0x0001)&&(udma_33))	speed = XFER_UDMA_0;
	else if (id->dma_mword & 0x0004)		speed = XFER_MW_DMA_2;
	else if (id->dma_mword & 0x0002)		speed = XFER_MW_DMA_1;
	else if (id->dma_mword & 0x0001)		speed = XFER_MW_DMA_0;
	else if ((id->dma_1word & 0x0004)&&(!jumpbit))	speed = XFER_SW_DMA_2;
	else if ((id->dma_1word & 0x0002)&&(!jumpbit))	speed = XFER_SW_DMA_1;
	else if ((id->dma_1word & 0x0001)&&(!jumpbit))	speed = XFER_SW_DMA_0;
	else {
		/* restore original pci-config space */
		if (!jumpbit)
			pci_write_config_dword(dev, drive_pci, drive_conf);
		return ide_dma_off_quietly;
	}

	outb(inb(dma_base+2) & ~(1<<(5+unit)), dma_base+2);
	(void) hwif->speedproc(drive, speed);

	return ((int)	((id->dma_ultra >> 14) & 3) ? ide_dma_on :
			((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on : 
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *hwif = drive->channel;
	ide_dma_action_t dma_func = ide_dma_off_quietly;

	if (id && (id->capability & 1) && hwif->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x007F) {
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
		(void) config_chipset_for_pio(drive, 5);
	}

	return drive->channel->dmaproc(dma_func, drive);
}

int pdc202xx_quirkproc (ide_drive_t *drive)
{
	return ((int) check_in_drive_lists(drive, pdc_quirk_drives));
}

/*
 * pdc202xx_dmaproc() initiates/aborts (U)DMA read/write operations on a drive.
 */
int pdc202xx_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	byte dma_stat		= 0;
	byte sc1d		= 0;
	byte newchip		= 0;
	byte clock		= 0;
	byte hardware48hack	= 0;
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long high_16	= pci_resource_start(dev, 4);
	unsigned long atapi_reg	= high_16 + (hwif->unit ? 0x24 : 0x00);
	unsigned long dma_base	= hwif->dma_base;

	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
			newchip = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			hardware48hack = 1;
			clock = IN_BYTE(high_16 + 0x11);
		default:
			break;
	}

	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		case ide_dma_begin:
			/* Note that this is done *after* the cmd has
			 * been issued to the drive, as per the BM-IDE spec.
			 * The Promise Ultra33 doesn't work correctly when
			 * we do this part before issuing the drive cmd.
			 */
			if ((drive->addressing) && (hardware48hack)) {
				struct request *rq = HWGROUP(drive)->rq;
				unsigned long word_count = 0;

				outb(clock|(hwif->unit ? 0x08 : 0x02), high_16 + 0x11);
				word_count = (rq->nr_sectors << 8);
				word_count = (rq_data_dir(rq) == READ) ? word_count | 0x05000000 : word_count | 0x06000000;
				outl(word_count, atapi_reg);
			}
			break;
		case ide_dma_end:
			if ((drive->addressing) && (hardware48hack)) {
				outl(0, atapi_reg);	/* zero out extra */
				clock = IN_BYTE(high_16 + 0x11);
				OUT_BYTE(clock & ~(hwif->unit ? 0x08:0x02), high_16 + 0x11);
			}
			break;
		case ide_dma_test_irq:	/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = IN_BYTE(dma_base+2);
			if (newchip)
				return (dma_stat & 4) == 4;

			sc1d = IN_BYTE(high_16 + 0x001d);
			if (drive->channel->unit) {
				if ((sc1d & 0x50) == 0x50) goto somebody_else;
				else if ((sc1d & 0x40) == 0x40)
					return (dma_stat & 4) == 4;
			} else {
				if ((sc1d & 0x05) == 0x05) goto somebody_else;
				else if ((sc1d & 0x04) == 0x04)
					return (dma_stat & 4) == 4;
			}
somebody_else:
			return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
		case ide_dma_lostirq:
		case ide_dma_timeout:
			if (drive->channel->resetproc != NULL)
				drive->channel->resetproc(drive);
		/*
		 * we cannot support queued operations on promise, so fail to
		 * to enable it...
		 */
		case ide_dma_queued_on:
			return 1;
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

void pdc202xx_new_reset (ide_drive_t *drive)
{
	OUT_BYTE(0x04,IDE_CONTROL_REG);
	mdelay(1000);
	OUT_BYTE(0x00,IDE_CONTROL_REG);
	mdelay(1000);
	printk("PDC202XX: %s channel reset.\n",
		drive->channel->unit ? "Secondary" : "Primary");
}

void pdc202xx_reset (ide_drive_t *drive)
{
	unsigned long high_16	= pci_resource_start(drive->channel->pci_dev, 4);
	byte udma_speed_flag	= IN_BYTE(high_16 + 0x001f);

	OUT_BYTE(udma_speed_flag | 0x10, high_16 + 0x001f);
	mdelay(100);
	OUT_BYTE(udma_speed_flag & ~0x10, high_16 + 0x001f);
	mdelay(2000);		/* 2 seconds ?! */
	printk("PDC202XX: %s channel reset.\n",
		drive->channel->unit ? "Secondary" : "Primary");
}

/*
 * Since SUN Cobalt is attempting to do this operation, I should disclose
 * this has been a long time ago Thu Jul 27 16:40:57 2000 was the patch date
 * HOTSWAP ATA Infrastructure.
 */
static int pdc202xx_tristate (ide_drive_t * drive, int state)
{
#if 0
	struct ata_channel *hwif = drive->channel;
	unsigned long high_16	= pci_resource_start(hwif->pci_dev, 4);
	byte sc1f		= inb(high_16 + 0x001f);

	if (!hwif)
		return -EINVAL;

//	hwif->bus_state = state;

	if (state) {
		outb(sc1f | 0x08, high_16 + 0x001f);
	} else {
		outb(sc1f & ~0x08, high_16 + 0x001f);
	}
#endif
	return 0;
}

unsigned int __init pci_init_pdc202xx(struct pci_dev *dev)
{
	unsigned long high_16	= pci_resource_start(dev, 4);
	byte udma_speed_flag	= IN_BYTE(high_16 + 0x001f);
	byte primary_mode	= IN_BYTE(high_16 + 0x001a);
	byte secondary_mode	= IN_BYTE(high_16 + 0x001b);
	byte newchip		= 0;

	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", dev->name, dev->resource[PCI_ROM_RESOURCE].start);
	}

	switch (dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268R:
		case PCI_DEVICE_ID_PROMISE_20268:
			newchip = 1;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
			OUT_BYTE(udma_speed_flag | 0x10, high_16 + 0x001f);
			mdelay(100);
			OUT_BYTE(udma_speed_flag & ~0x10, high_16 + 0x001f);
			mdelay(2000);   /* 2 seconds ?! */
			break;
		case PCI_DEVICE_ID_PROMISE_20262:
			/*
			 * software reset -  this is required because the bios
			 * will set UDMA timing on if the hdd supports it. The
			 * user may want to turn udma off. A bug in the pdc20262
			 * is that it cannot handle a downgrade in timing from
			 * UDMA to DMA. Disk accesses after issuing a set
			 * feature command will result in errors. A software
			 * reset leaves the timing registers intact,
			 * but resets the drives.
			 */
			OUT_BYTE(udma_speed_flag | 0x10, high_16 + 0x001f);
			mdelay(100);
			OUT_BYTE(udma_speed_flag & ~0x10, high_16 + 0x001f);
			mdelay(2000);	/* 2 seconds ?! */
		default:
			if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE) {
				byte irq = 0, irq2 = 0;
				pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
				pci_read_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, &irq2);	/* 0xbc */
				if (irq != irq2) {
					pci_write_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, irq);	/* 0xbc */
					printk("%s: pci-config space interrupt mirror fixed.\n", dev->name);
				}
			}
			break;
	}

	if (newchip)
		goto fttk_tx_series;

	printk("%s: (U)DMA Burst Bit %sABLED " \
		"Primary %s Mode " \
		"Secondary %s Mode.\n",
		dev->name,
		(udma_speed_flag & 1) ? "EN" : "DIS",
		(primary_mode & 1) ? "MASTER" : "PCI",
		(secondary_mode & 1) ? "MASTER" : "PCI" );

#ifdef CONFIG_PDC202XX_BURST
	if (!(udma_speed_flag & 1)) {
		printk("%s: FORCING BURST BIT 0x%02x -> 0x%02x ", dev->name, udma_speed_flag, (udma_speed_flag|1));
		OUT_BYTE(udma_speed_flag|1, high_16 + 0x001f);
		printk("%sCTIVE\n", (IN_BYTE(high_16 + 0x001f) & 1) ? "A" : "INA");
	}
#endif /* CONFIG_PDC202XX_BURST */

#ifdef CONFIG_PDC202XX_MASTER
	if (!(primary_mode & 1)) {
		printk("%s: FORCING PRIMARY MODE BIT 0x%02x -> 0x%02x ",
			dev->name, primary_mode, (primary_mode|1));
		OUT_BYTE(primary_mode|1, high_16 + 0x001a);
		printk("%s\n", (IN_BYTE(high_16 + 0x001a) & 1) ? "MASTER" : "PCI");
	}

	if (!(secondary_mode & 1)) {
		printk("%s: FORCING SECONDARY MODE BIT 0x%02x -> 0x%02x ",
			dev->name, secondary_mode, (secondary_mode|1));
		OUT_BYTE(secondary_mode|1, high_16 + 0x001b);
		printk("%s\n", (IN_BYTE(high_16 + 0x001b) & 1) ? "MASTER" : "PCI");
	}
#endif /* CONFIG_PDC202XX_MASTER */

fttk_tx_series:

#if defined(DISPLAY_PDC202XX_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!pdc202xx_proc) {
		pdc202xx_proc = 1;
		bmide_dev = dev;
		pdc202xx_display_info = &pdc202xx_get_info;
	}
#endif /* DISPLAY_PDC202XX_TIMINGS && CONFIG_PROC_FS */
	return dev->irq;
}

unsigned int __init ata66_pdc202xx(struct ata_channel *hwif)
{
	unsigned short mask = (hwif->unit) ? (1<<11) : (1<<10);
	unsigned short CIS;

        switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			OUT_BYTE(0x0b, (hwif->dma_base + 1));
			return (!(IN_BYTE((hwif->dma_base + 3)) & 0x04));
		default:
			pci_read_config_word(hwif->pci_dev, 0x50, &CIS);
			return (!(CIS & mask));
	}
}

void __init ide_init_pdc202xx(struct ata_channel *hwif)
{
	hwif->tuneproc  = &pdc202xx_tune_drive;
	hwif->quirkproc = &pdc202xx_quirkproc;

        switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20268R:
			hwif->speedproc = &pdc202xx_new_tune_chipset;
			hwif->resetproc = &pdc202xx_new_reset;
			break;
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20262:
			hwif->busproc   = &pdc202xx_tristate;
			hwif->resetproc	= &pdc202xx_reset;
		case PCI_DEVICE_ID_PROMISE_20246:
			hwif->speedproc = &pdc202xx_tune_chipset;
		default:
			break;
	}

#undef CONFIG_PDC202XX_32_UNMASK
#ifdef CONFIG_PDC202XX_32_UNMASK
	hwif->drives[0].io_32bit = 1;
	hwif->drives[1].io_32bit = 1;

	hwif->drives[0].unmask = 1;
	hwif->drives[1].unmask = 1;
#endif

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->dmaproc = &pdc202xx_dmaproc;
		hwif->highmem = 1;
		if (!noautodma)
			hwif->autodma = 1;
	} else {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		hwif->autodma = 0;
	}
#else
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#endif
}
