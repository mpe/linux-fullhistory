/* $Id: cmd646.c,v 1.10 1998/08/03 15:28:42 davem Exp $
 * cmd646.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998       David S. Miller (davem@dm.cobaltmicro.com)
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "ide.h"

static int cmd646_config_drive_for_dma(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);

	/* Even if the drive is not _currently_ in a DMA
	 * mode, we succeed, and we'll enable it manually
	 * below in cmd646_dma_onoff.
	 *
	 * This is done for disks only, CDROMs and other
	 * IDE devices are just too quirky.
	 */
	if((id != NULL) &&
	   ((id->capability & 1) != 0) &&
	   hwif->autodma &&
	   (drive->media == ide_disk)) {
		if(id->field_valid & 0x0004) {
			if(id->dma_ultra & 0x0007)
				return hwif->dmaproc(ide_dma_on, drive);
		}
		if(id->field_valid & 0x0002)
			if((id->dma_mword & 0x0004) || (id->dma_1word & 0x0004))
				return hwif->dmaproc(ide_dma_on, drive);
	}
	return hwif->dmaproc(ide_dma_off_quietly, drive);
}

/* This is fun.  -DaveM */
#define IDE_SETXFER		0x03
#define IDE_SETFEATURE		0xef
#define IDE_DMA2_ENABLE		0x22
#define IDE_DMA1_ENABLE		0x21
#define IDE_DMA0_ENABLE		0x20
#define IDE_UDMA2_ENABLE	0x42
#define IDE_UDMA1_ENABLE	0x41
#define IDE_UDMA0_ENABLE	0x40

static __inline__ unsigned char dma2_bits_to_command(unsigned char bits)
{
	if(bits & 0x04)
		return IDE_DMA2_ENABLE;
	if(bits & 0x02)
		return IDE_DMA1_ENABLE;
	return IDE_DMA0_ENABLE;
}

static __inline__ unsigned char udma2_bits_to_command(unsigned char bits)
{
	if(bits & 0x04)
		return IDE_UDMA2_ENABLE;
	if(bits & 0x02)
		return IDE_UDMA1_ENABLE;
	return IDE_UDMA0_ENABLE;
}

static __inline__ int wait_for_ready(ide_drive_t *drive)
{
	int timeout = 100;
	byte stat;

	while(--timeout) {
		stat = GET_STAT();

		printk("STAT(%2x) ", stat);
		if(!(stat & BUSY_STAT)) {
			if((stat & READY_STAT) || (stat & ERR_STAT))
				break;
		}
		udelay(100);
	}
	if((stat & ERR_STAT) || timeout <= 0)
		return 1;
	return 0;
}

static void cmd646_do_setfeature(ide_drive_t *drive, byte command)
{
	unsigned long flags;
	byte old_select;

	save_flags(flags);
	cli();
	printk("SELECT ");
	old_select = IN_BYTE(IDE_SELECT_REG);
	OUT_BYTE(drive->select.all, IDE_SELECT_REG);
	printk("SETXFER ");
	OUT_BYTE(IDE_SETXFER, IDE_FEATURE_REG);
	printk("CMND ");
	OUT_BYTE(command, IDE_NSECTOR_REG);
	printk("wait ");
	if(wait_for_ready(drive))
		goto out;
	printk("SETFEATURE ");
	OUT_BYTE(IDE_SETFEATURE, IDE_COMMAND_REG);
	printk("wait ");
	(void) wait_for_ready(drive);
out:
	OUT_BYTE(old_select, IDE_SELECT_REG);
	restore_flags(flags);
}

static void cmd646_dma2_enable(ide_drive_t *drive, unsigned long dma_base)
{
	byte unit = (drive->select.b.unit & 0x01);
	byte bits = (drive->id->dma_mword | drive->id->dma_1word) & 0x07;

	printk("CMD646: MDMA enable [");
	if((((drive->id->dma_mword & 0x0007) << 8) !=
	    (drive->id->dma_mword & 0x0700)))
		cmd646_do_setfeature(drive, dma2_bits_to_command(bits));
	printk("DMA_CAP ");
	outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);
	printk("DONE]\n");
}

static void cmd646_udma_enable(ide_drive_t *drive, unsigned long dma_base)
{
	byte unit = (drive->select.b.unit & 0x01);
	byte udma_ctrl, bits = drive->id->dma_ultra & 0x07;
	byte udma_timing_bits;

	printk("CMD646: UDMA enable [");
	if(((drive->id->dma_ultra & 0x0007) << 8) !=
	   (drive->id->dma_ultra & 0x0700))
		cmd646_do_setfeature(drive, udma2_bits_to_command(bits));

	/* Enable DMA and UltraDMA */
	printk("DMA_CAP ");
	outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);

	udma_ctrl = inb(dma_base + 3);

	/* Put this channel into UDMA mode. */
	printk("UDMA_CTRL ");
	udma_ctrl |= (1 << unit);

	/* Set UDMA2 usable timings. */
	if(bits & 0x04)
		udma_timing_bits = 0x10;
	else if(bits & 0x02)
		udma_timing_bits = 0x20;
	else
		udma_timing_bits = 0x30;
	udma_ctrl &= ~(0x30 << (unit * 2));
	udma_ctrl |=  (udma_timing_bits << (unit * 2));

	outb(udma_ctrl, dma_base+3);
	printk("DONE]\n");
}

static int cmd646_dma_onoff(ide_drive_t *drive, int enable)
{
	if(enable) {
		ide_hwif_t *hwif = HWIF(drive);
		unsigned long dma_base = hwif->dma_base;
		struct hd_driveid *id = drive->id;
		unsigned int class_rev;

		/* UltraDMA only supported on PCI646U and PCI646U2,
		 * which correspond to revisions 0x03 and 0x05 respectively.
		 * Actually, although the CMD tech support people won't
		 * tell me the details, the 0x03 revision cannot support
		 * UDMA correctly without hardware modifications, and even
		 * then it only works with Quantum disks due to some
		 * hold time assumptions in the 646U part which are fixed
		 * in the 646U2.
		 * So we only do UltraDMA on revision 0x05 chipsets.
		 */
		pci_read_config_dword(hwif->pci_dev,
				      PCI_CLASS_REVISION,
				      &class_rev);
		class_rev &= 0xff;
		if((class_rev == 0x05) &&
		   (id->field_valid & 0x0004) &&
		   (id->dma_ultra & 0x07)) {
			/* UltraDMA modes. */
			cmd646_udma_enable(drive, dma_base);
		} else {
			/* Normal MultiWord DMA modes. */
			cmd646_dma2_enable(drive, dma_base);
		}
	}
	drive->using_dma = enable;
	return 0;
}

static int cmd646_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	if(func == ide_dma_check)
		return cmd646_config_drive_for_dma(drive);
	else if(func == ide_dma_on || func == ide_dma_off || func == ide_dma_off_quietly)
		return cmd646_dma_onoff(drive, (func == ide_dma_on));

	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}

__initfunc(void ide_init_cmd646 (ide_hwif_t *hwif))
{
	struct pci_dev *dev = hwif->pci_dev;
	unsigned char mrdmode;

	hwif->chipset = ide_cmd646;

	/* Set a good latency timer value. */
	(void) pci_write_config_byte(dev, PCI_LATENCY_TIMER, 240);

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

	hwif->dmaproc = &cmd646_dmaproc;
}
