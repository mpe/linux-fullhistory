/* $Id: cmd64x.c,v 1.20 1999/12/30 03:48:37
 *
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998       David S. Miller (davem@redhat.com)
 * Copyright (C) 1999       Andre Hedrick (andre@suse.com)
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#define CMD_DEBUG 0

#if CMD_DEBUG
#define cmdprintk(x...)	printk(##x)
#else
#define cmdprintk(x...)
#endif

static int config_chipset_for_dma (ide_drive_t *drive, unsigned int rev, byte ultra_66)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned long dma_base	= hwif->dma_base;

 	byte unit		= (drive->select.b.unit & 0x01);
	byte speed		= 0x00;
	byte udma_timing_bits	= 0x00;
	byte udma_33		= ((rev >= 0x05) || (ultra_66)) ? 1 : 0;
	byte udma_66		= ((id->hw_config & 0x2000) && (hwif->udma_four)) ? 1 : 0;
	/* int drive_number	= ((hwif->channel ? 2 : 0) + unit); */
	int rval;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
		case PCI_DEVICE_ID_CMD_646:
		case PCI_DEVICE_ID_CMD_648:
		default:
			break;
	}

	if (drive->media != ide_disk) {
		cmdprintk("CMD64X: drive->media != ide_disk at double check, inital check failed!!\n");
		return ((int) ide_dma_off);
	}

	/* UltraDMA only supported on PCI646U and PCI646U2,
	 * which correspond to revisions 0x03, 0x05 and 0x07 respectively.
	 * Actually, although the CMD tech support people won't
	 * tell me the details, the 0x03 revision cannot support
	 * UDMA correctly without hardware modifications, and even
	 * then it only works with Quantum disks due to some
	 * hold time assumptions in the 646U part which are fixed
	 * in the 646U2.
	 * So we only do UltraDMA on revision 0x05 and 0x07 chipsets.
	 */

	if ((id->dma_ultra & 0x0010) && (udma_66) && (udma_33)) {
		speed = XFER_UDMA_4;
		udma_timing_bits = 0x10;	/* 2 clock */
	} else if ((id->dma_ultra & 0x0008) && (udma_66) && (udma_33)) {
		speed = XFER_UDMA_3;
		udma_timing_bits = 0x20;	/* 3 clock */
	} else if ((id->dma_ultra & 0x0004) && (udma_33)) {
		speed = XFER_UDMA_2;
		udma_timing_bits = 0x10;	/* 2 clock */
	} else if ((id->dma_ultra & 0x0002) && (udma_33)) {
		speed = XFER_UDMA_1;
		udma_timing_bits = 0x20;	/* 3 clock */
	} else if ((id->dma_ultra & 0x0001) && (udma_33)) {
		speed = XFER_UDMA_0;
		udma_timing_bits = 0x30;	/* 4 clock */
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

	(void) ide_config_drive_speed(drive, speed);
	outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);

	if (speed >= XFER_UDMA_0) {
		byte udma_ctrl = inb(dma_base + 3);
		/* Put this channel into UDMA mode. */
		udma_ctrl |= (1 << unit);
		udma_ctrl &= ~(0x04 << unit);
		if (udma_66)
			udma_ctrl |= (0x04 << unit);
		udma_ctrl &= ~(0x30 << (unit * 2));
		udma_ctrl |=  (udma_timing_bits << (unit * 2));
		outb(udma_ctrl, dma_base+3);
	}

	rval = (int)(	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);

	return rval;
}

static void config_chipset_for_pio (ide_drive_t *drive, unsigned int rev)
{
	/*  FIXME!! figure out some PIOing junk.... */
}

static int cmd64x_config_drive_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int class_rev	= 0;
	byte can_ultra_33	= 0;
	byte can_ultra_66	= 0;
	ide_dma_action_t dma_func = ide_dma_on;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;	

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
			can_ultra_33 = 1;
			can_ultra_66 = 0;
			break;
		case PCI_DEVICE_ID_CMD_646:
			can_ultra_33 = (class_rev >= 0x05) ? 1 : 0;
			can_ultra_66 = 0;
			break;
		case PCI_DEVICE_ID_CMD_648:
			can_ultra_33 = 1;
			can_ultra_66 = 1;
			break;
		default:
			return hwif->dmaproc(ide_dma_off, drive);
	}

	if ((id != NULL) && ((id->capability & 1) != 0) &&
	    hwif->autodma && (drive->media == ide_disk)) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if ((id->field_valid & 4) && (can_ultra_33)) {
			if (id->dma_ultra & 0x001F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive, class_rev, can_ultra_66);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive, class_rev, 0);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive, class_rev, 0);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		config_chipset_for_pio(drive, class_rev);
	}
	return hwif->dmaproc(dma_func, drive);
}

static int cmd64x_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return cmd64x_config_drive_for_dma(drive);
		default:
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}

/*
 * ASUS P55T2P4D with CMD646 chipset revision 0x01 requires the old
 * event order for DMA transfers.
 */
static int cmd646_1_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long dma_base = hwif->dma_base;
	byte dma_stat;

	if (func == ide_dma_end) {
		drive->waiting_for_dma = 0;
		dma_stat = inb(dma_base+2);		/* get DMA status */
		outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
		outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
		ide_destroy_dmatable(drive);		/* and free any DMA resources */
		return (dma_stat & 7) != 4;		/* verify good DMA status */
	}

	/* Other cases are done by generic IDE-DMA code. */
	return cmd64x_dmaproc(func, drive);
}

unsigned int __init pci_init_cmd64x (struct pci_dev *dev, const char *name)
{
	unsigned char mrdmode;
	unsigned int class_rev;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
			break;
		case PCI_DEVICE_ID_CMD_646:
			printk("%s: chipset revision 0x%02X, ", name, class_rev);
			switch(class_rev) {
				case 0x07:
				case 0x05:
					printk("UltraDMA Capable");
					break;
				case 0x03:
					printk("MultiWord DMA Force Limited");
					break;
				case 0x01:
				default:
					printk("MultiWord DMA Limited, IRQ workaround enabled");
					break;
				}
			printk("\n");
                        break;
		case PCI_DEVICE_ID_CMD_648:
			break;
		default:
			break;
	}

	/* Set a good latency timer and cache line size value. */
	(void) pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
#ifdef __sparc_v9__
	(void) pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x10);
#endif

	/* Setup interrupts. */
	(void) pci_read_config_byte(dev, 0x71, &mrdmode);
	mrdmode &= ~(0x30);
	(void) pci_write_config_byte(dev, 0x71, mrdmode);

	/* Use MEMORY READ LINE for reads.
	 * NOTE: Although not mentioned in the PCI0646U specs,
	 *       these bits are write only and won't be read
	 *       back as set or not.  The PCI0646U2 specs clarify
	 *       this point.
	 */
	(void) pci_write_config_byte(dev, 0x71, mrdmode | 0x02);

	/* Set reasonable active/recovery/address-setup values. */
	(void) pci_write_config_byte(dev, 0x53, 0x40);
	(void) pci_write_config_byte(dev, 0x54, 0x3f);
	(void) pci_write_config_byte(dev, 0x55, 0x40);
	(void) pci_write_config_byte(dev, 0x56, 0x3f);
	(void) pci_write_config_byte(dev, 0x57, 0x5c);
	(void) pci_write_config_byte(dev, 0x58, 0x3f);
	(void) pci_write_config_byte(dev, 0x5b, 0x3f);
	return 0;
}

unsigned int __init ata66_cmd64x (ide_hwif_t *hwif)
{
	byte ata66 = 0;
	byte mask = (hwif->channel) ? 0x02 : 0x01;

	pci_read_config_byte(hwif->pci_dev, 0x79, &ata66);
	return (ata66 & mask) ? 1 : 0;
}

void __init ide_init_cmd64x (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int class_rev;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	if (!hwif->dma_base)
		return;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
			hwif->dmaproc = &cmd64x_dmaproc;
			break;
		case PCI_DEVICE_ID_CMD_646:
			hwif->chipset = ide_cmd646;
			if (class_rev == 0x01) {
				hwif->dmaproc = &cmd646_1_dmaproc;
			} else {
				hwif->dmaproc = &cmd64x_dmaproc;
			}
			break;
		case PCI_DEVICE_ID_CMD_648:
			hwif->dmaproc = &cmd64x_dmaproc;
			break;
		default:
			break;
	}
}
