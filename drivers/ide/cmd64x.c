/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * $Id: cmd64x.c,v 1.21 2000/01/30 23:23:16
 *
 * linux/drivers/ide/cmd64x.c		Version 1.22	June 9, 2000
 *
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998		Eddie C. Dost <ecd@skynet.be>
 * Copyright (C) 1998		David S. Miller <davem@redhat.com>
 * Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ata-timing.h"
#include "pcihost.h"

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

#define CMD_DEBUG 0

#if CMD_DEBUG
#define cmdprintk(x...)	printk(##x)
#else
#define cmdprintk(x...)
#endif

/*
 * CMD64x specific registers definition.
 */

#define CFR		0x50
#define   CFR_INTR_CH0		0x02
#define CNTRL		0x51
#define	  CNTRL_DIS_RA0		0x40
#define   CNTRL_DIS_RA1		0x80
#define	  CNTRL_ENA_2ND		0x08

#define	CMDTIM		0x52
#define	ARTTIM0		0x53
#define	DRWTIM0		0x54
#define ARTTIM1 	0x55
#define DRWTIM1		0x56
#define ARTTIM23	0x57
#define   ARTTIM23_DIS_RA2	0x04
#define   ARTTIM23_DIS_RA3	0x08
#define   ARTTIM23_INTR_CH1	0x10
#define ARTTIM2		0x57
#define ARTTIM3		0x57
#define DRWTIM23	0x58
#define DRWTIM2		0x58
#define BRST		0x59
#define DRWTIM3		0x5b

#define BMIDECR0	0x70
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define BMIDESR0	0x72
#define UDIDETCR0	0x73
#define DTPR0		0x74
#define BMIDECR1	0x78
#define BMIDECSR	0x79
#define BMIDESR1	0x7A
#define UDIDETCR1	0x7B
#define DTPR1		0x7C

/*
 * Registers and masks for easy access by drive index:
 */
#if 0
static byte prefetch_regs[4]  = {CNTRL, CNTRL, ARTTIM23, ARTTIM23};
static byte prefetch_masks[4] = {CNTRL_DIS_RA0, CNTRL_DIS_RA1, ARTTIM23_DIS_RA2, ARTTIM23_DIS_RA3};
#endif

/*
 * This routine writes the prepared setup/active/recovery counts
 * for a drive into the cmd646 chipset registers to active them.
 */
static void program_drive_counts(struct ata_device *drive, int setup_count, int active_count, int recovery_count)
{
	unsigned long flags;
	struct ata_device *drives = drive->channel->drives;
	byte temp_b;
	static const byte setup_counts[] = {0x40, 0x40, 0x40, 0x80, 0, 0xc0};
	static const byte recovery_counts[] =
		{15, 15, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0};
	static const byte arttim_regs[2][2] = {
			{ ARTTIM0, ARTTIM1 },
			{ ARTTIM23, ARTTIM23 }
		};
	static const byte drwtim_regs[2][2] = {
			{ DRWTIM0, DRWTIM1 },
			{ DRWTIM2, DRWTIM3 }
		};
	int channel = drive->channel->unit;
	int slave = (drives != drive);  /* Is this really the best way to determine this?? */

	cmdprintk("program_drive_count parameters = s(%d),a(%d),r(%d),p(%d)\n", setup_count,
		active_count, recovery_count, drive->present);
	/*
	 * Set up address setup count registers.
	 * Primary interface has individual count/timing registers for
	 * each drive.  Secondary interface has one common set of registers,
	 * for address setup so we merge these timings, using the slowest
	 * value.
	 */
	if (channel) {
		drive->drive_data = setup_count;
		setup_count = max(drives[0].drive_data, drives[1].drive_data);
		cmdprintk("Secondary interface, setup_count = %d\n", setup_count);
	}

	/*
	 * Convert values to internal chipset representation
	 */
	setup_count = (setup_count > 5) ? 0xc0 : (int) setup_counts[setup_count];
	active_count &= 0xf; /* Remember, max value is 16 */
	recovery_count = (int) recovery_counts[recovery_count];

	cmdprintk("Final values = %d,%d,%d\n", setup_count, active_count, recovery_count);

	/*
	 * Now that everything is ready, program the new timings
	 */
	__save_flags (flags);
	__cli();
	/*
	 * Program the address_setup clocks into ARTTIM reg,
	 * and then the active/recovery counts into the DRWTIM reg
	 */
	(void) pci_read_config_byte(drive->channel->pci_dev, arttim_regs[channel][slave], &temp_b);
	(void) pci_write_config_byte(drive->channel->pci_dev, arttim_regs[channel][slave],
		((byte) setup_count) | (temp_b & 0x3f));
	(void) pci_write_config_byte(drive->channel->pci_dev, drwtim_regs[channel][slave],
		(byte) ((active_count << 4) | recovery_count));
	cmdprintk ("Write %x to %x\n", ((byte) setup_count) | (temp_b & 0x3f), arttim_regs[channel][slave]);
	cmdprintk ("Write %x to %x\n", (byte) ((active_count << 4) | recovery_count), drwtim_regs[channel][slave]);
	__restore_flags(flags);
}

/*
 * Attempts to set the interface PIO mode.
 * The preferred method of selecting PIO modes (e.g. mode 4) is
 * "echo 'piomode:4' > /proc/ide/hdx/settings".  Special cases are
 * 8: prefetch off, 9: prefetch on, 255: auto-select best mode.
 * Called with 255 at boot time.
 */
static void cmd64x_tuneproc(struct ata_device *drive, byte mode_wanted)
{
	int recovery_time, clock_time;
	byte recovery_count2, cycle_count;
	int setup_count, active_count, recovery_count;
	struct ata_timing *t;

	switch (mode_wanted) {
		case 8: /* set prefetch off */
		case 9: /* set prefetch on */
			mode_wanted &= 1;
			/*set_prefetch_mode(index, mode_wanted);*/
			cmdprintk("%s: %sabled cmd640 prefetch\n", drive->name, mode_wanted ? "en" : "dis");
			return;
		case 255: mode_wanted = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	}

	t = ata_timing_data(XFER_PIO_0 + min_t(byte, mode_wanted, 4));

	/*
	 * I copied all this complicated stuff from cmd640.c and made a few minor changes.
	 * For now I am just going to pray that it is correct.
	 */

	recovery_time = t->cycle - (t->setup + t->active);
	clock_time = 1000000 / system_bus_speed;
	cycle_count = (t->cycle + clock_time - 1) / clock_time;
	setup_count = (t->setup + clock_time - 1) / clock_time;
	active_count = (t->active + clock_time - 1) / clock_time;

	recovery_count = (recovery_time + clock_time - 1) / clock_time;
	recovery_count2 = cycle_count - (setup_count + active_count);
	if (recovery_count2 > recovery_count)
		recovery_count = recovery_count2;
	if (recovery_count > 16) {
		active_count += recovery_count - 16;
		recovery_count = 16;
	}
	if (active_count > 16)
		active_count = 16; /* maximum allowed by cmd646 */

	/*
	 * In a perfect world, we might set the drive pio mode here
	 * (using WIN_SETFEATURE) before continuing.
	 *
	 * But we do not, because:
	 *	1) this is the wrong place to do it (proper is do_special() in ide.c)
	 * 	2) in practice this is rarely, if ever, necessary
	 */
	program_drive_counts (drive, setup_count, active_count, recovery_count);

	cmdprintk("%s: selected cmd646 PIO mode%d : %d (%dns), clocks=%d/%d/%d\n",
		drive->name, t.mode - XFER_PIO_0, mode_wanted, cycle_time,
		setup_count, active_count, recovery_count);
}

static int cmd64x_ratemask(struct ata_device *drive)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	int map = 0;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_680:
			map |= XFER_UDMA_133;
		case PCI_DEVICE_ID_CMD_649:
			map |= XFER_UDMA_100;
		case PCI_DEVICE_ID_CMD_648:
			map |= XFER_UDMA_66;
		case PCI_DEVICE_ID_CMD_643:
			map |= XFER_UDMA;
			break;
		case PCI_DEVICE_ID_CMD_646:
		{
			unsigned int class_rev	= 0;
			pci_read_config_dword(dev,
				PCI_CLASS_REVISION, &class_rev);
			class_rev &= 0xff;
		/*
		 * UltraDMA only supported on PCI646U and PCI646U2, which
		 * correspond to revisions 0x03, 0x05 and 0x07 respectively.
		 * Actually, although the CMD tech support people won't
		 * tell me the details, the 0x03 revision cannot support
		 * UDMA correctly without hardware modifications, and even
		 * then it only works with Quantum disks due to some
		 * hold time assumptions in the 646U part which are fixed
		 * in the 646U2.
		 *
		 * So we only do UltraDMA on revision 0x05 and 0x07 chipsets.
		 */
			switch(class_rev) {
				case 0x07:
				case 0x05:
					map |= XFER_UDMA;
				case 0x03:
				case 0x01:
				default:
					break;
			}
		}
	}

	if (!eighty_ninty_three(drive)) {
		if (map & XFER_UDMA)
			return XFER_UDMA;
		return 0;
	}
	return map;
}

static byte cmd680_taskfile_timing(struct ata_channel *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	byte addr_mask		= (hwif->unit) ? 0xB2 : 0xA2;
	unsigned short		timing;

	pci_read_config_word(dev, addr_mask, &timing);

	switch (timing) {
		case 0x10c1:	return 4;
		case 0x10c3:	return 3;
		case 0x1281:	return 2;
		case 0x2283:	return 1;
		case 0x328a:
		default:	return 0;
	}
}

static void cmd680_tuneproc(struct ata_device *drive, byte mode_wanted)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	byte			drive_pci;
	unsigned short		speedt;

	switch (drive->dn) {
		case 0: drive_pci = 0xA4; break;
		case 1: drive_pci = 0xA6; break;
		case 2: drive_pci = 0xB4; break;
		case 3: drive_pci = 0xB6; break;
		default: return;
        }

	pci_read_config_word(dev, drive_pci, &speedt);

	/* cheat for now and use the docs */
//	switch(cmd680_taskfile_timing(hwif)) {
	switch(mode_wanted) {
		case 4:		speedt = 0x10c1; break;
		case 3:		speedt = 0x10C3; break;
		case 2:		speedt = 0x1104; break;
		case 1:		speedt = 0x2283; break;
		case 0:
		default:	speedt = 0x328A; break;
	}
	pci_write_config_word(dev, drive_pci, speedt);
}

static void config_cmd64x_chipset_for_pio(struct ata_device *drive, byte set_speed)
{
	byte speed	= 0x00;
	byte set_pio	= ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

	cmd64x_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}

static void config_cmd680_chipset_for_pio(struct ata_device *drive, byte set_speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 addr_mask		= (hwif->unit) ? 0x84 : 0x80;
	u8 speed		= 0x00;
	u8 mode_pci		= 0x00;
	u8 channel_timings	= cmd680_taskfile_timing(hwif);
	u8 set_pio		= ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

	pci_read_config_byte(dev, addr_mask, &mode_pci);
	mode_pci &= ~((unit) ? 0x30 : 0x03);

	/* WARNING PIO timing mess is going to happen b/w devices, argh */
	if ((channel_timings != set_pio) && (set_pio > channel_timings))
		set_pio = channel_timings;

	cmd680_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed) {
		(void) ide_config_drive_speed(drive, speed);
		drive->current_speed = speed;
	}
}

static void config_chipset_for_pio(struct ata_device *drive, byte set_speed)
{
        if (drive->channel->pci_dev->device == PCI_DEVICE_ID_CMD_680) {
		config_cmd680_chipset_for_pio(drive, set_speed);
	} else {
		config_cmd64x_chipset_for_pio(drive, set_speed);
	}
}

static int cmd64x_tune_chipset(struct ata_device *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;

	u8 unit			= (drive->select.b.unit & 0x01);
	u8 pciU			= (hwif->unit) ? UDIDETCR1 : UDIDETCR0;
	u8 pciD			= (hwif->unit) ? BMIDESR1 : BMIDESR0;
	u8 regU			= 0;
	u8 regD			= 0;

	if ((drive->type != ATA_DISK) && (speed < XFER_SW_DMA_0))
		return 1;

	(void) pci_read_config_byte(dev, pciD, &regD);
	(void) pci_read_config_byte(dev, pciU, &regU);
	regD &= ~(unit ? 0x40 : 0x20);
	regU &= ~(unit ? 0xCA : 0x35);
	(void) pci_write_config_byte(dev, pciD, regD);
	(void) pci_write_config_byte(dev, pciU, regU);

	(void) pci_read_config_byte(dev, pciD, &regD);
	(void) pci_read_config_byte(dev, pciU, &regU);
	switch(speed) {
		case XFER_UDMA_5:	regU |= (unit ? 0x0A : 0x05); break;
		case XFER_UDMA_4:	regU |= (unit ? 0x4A : 0x15); break;
		case XFER_UDMA_3:	regU |= (unit ? 0x8A : 0x25); break;
		case XFER_UDMA_2:	regU |= (unit ? 0x42 : 0x11); break;
		case XFER_UDMA_1:	regU |= (unit ? 0x82 : 0x21); break;
		case XFER_UDMA_0:	regU |= (unit ? 0xC2 : 0x31); break;
		case XFER_MW_DMA_2:	regD |= (unit ? 0x40 : 0x10); break;
		case XFER_MW_DMA_1:	regD |= (unit ? 0x80 : 0x20); break;
		case XFER_MW_DMA_0:	regD |= (unit ? 0xC0 : 0x30); break;
		case XFER_SW_DMA_2:	regD |= (unit ? 0x40 : 0x10); break;
		case XFER_SW_DMA_1:	regD |= (unit ? 0x80 : 0x20); break;
		case XFER_SW_DMA_0:	regD |= (unit ? 0xC0 : 0x30); break;
#else
	switch(speed) {
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:	cmd64x_tuneproc(drive, 4); break;
		case XFER_PIO_3:	cmd64x_tuneproc(drive, 3); break;
		case XFER_PIO_2:	cmd64x_tuneproc(drive, 2); break;
		case XFER_PIO_1:	cmd64x_tuneproc(drive, 1); break;
		case XFER_PIO_0:	cmd64x_tuneproc(drive, 0); break;

		default:
			return 1;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	(void) pci_write_config_byte(dev, pciU, regU);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	drive->current_speed = speed;

#ifdef CONFIG_BLK_DEV_IDEDMA
	regD |= (unit ? 0x40 : 0x20);
	(void) pci_write_config_byte(dev, pciD, regD);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	return ide_config_drive_speed(drive, speed);
}

static int cmd680_tune_chipset(struct ata_device *drive, byte speed)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	u8 addr_mask		= (hwif->unit) ? 0x84 : 0x80;
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 dma_pci		= 0;
	u8 udma_pci		= 0;
	u8 mode_pci		= 0;
	u8 scsc			= 0;
	u16 ultra		= 0;
	u16 multi		= 0;

        pci_read_config_byte(dev, addr_mask, &mode_pci);
	pci_read_config_byte(dev, 0x8A, &scsc);

        switch (drive->dn) {
		case 0: dma_pci = 0xA8; udma_pci = 0xAC; break;
		case 1: dma_pci = 0xAA; udma_pci = 0xAE; break;
		case 2: dma_pci = 0xB8; udma_pci = 0xBC; break;
		case 3: dma_pci = 0xBA; udma_pci = 0xBE; break;
		default: return 1;
	}

	pci_read_config_byte(dev, addr_mask, &mode_pci);
	mode_pci &= ~((unit) ? 0x30 : 0x03);
	pci_read_config_word(dev, dma_pci, &multi);
	pci_read_config_word(dev, udma_pci, &ultra);

	if ((speed == XFER_UDMA_6) && (scsc & 0x30) == 0x00) {
		pci_write_config_byte(dev, 0x8A, scsc|0x01);
		pci_read_config_byte(dev, 0x8A, &scsc);
	}

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_6:
			if ((scsc & 0x30) == 0x00)
				goto speed_break;
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= 0x01;
			break;
speed_break :
			speed = XFER_UDMA_5;
		case XFER_UDMA_5:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x01 : 0x02);
			break;
		case XFER_UDMA_4:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x02 : 0x03);
			break;
		case XFER_UDMA_3:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x04 : 0x05);
			break;
		case XFER_UDMA_2:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x05 : 0x07);
			break;
		case XFER_UDMA_1:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x07 : 0x0B);
			break;
		case XFER_UDMA_0:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x0C : 0x0F);
			break;
		case XFER_MW_DMA_2:
			multi = 0x10C1;
			break;
		case XFER_MW_DMA_1:
			multi = 0x10C2;
			break;
		case XFER_MW_DMA_0:
			multi = 0x2208;
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:	cmd680_tuneproc(drive, 4); break;
		case XFER_PIO_3:	cmd680_tuneproc(drive, 3); break;
		case XFER_PIO_2:	cmd680_tuneproc(drive, 2); break;
		case XFER_PIO_1:	cmd680_tuneproc(drive, 1); break;
		case XFER_PIO_0:	cmd680_tuneproc(drive, 0); break;
		default:
			return 1;
	}

	if (speed >= XFER_MW_DMA_0) 
		config_cmd680_chipset_for_pio(drive, 0);

	if (speed >= XFER_UDMA_0)
		mode_pci |= ((unit) ? 0x30 : 0x03);
	else if (speed >= XFER_MW_DMA_0)
		mode_pci |= ((unit) ? 0x20 : 0x02);
	else
		mode_pci |= ((unit) ? 0x10 : 0x01);

	pci_write_config_byte(dev, addr_mask, mode_pci);
	pci_write_config_word(dev, dma_pci, multi);
	pci_write_config_word(dev, udma_pci, ultra);

	drive->current_speed = speed;

	return ide_config_drive_speed(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma(struct ata_device *drive, u8 udma)
{
	int map;
	u8 mode;

	if (drive->type != ATA_DISK) {
		cmdprintk("CMD64X: drive is not a disk at double check, inital check failed!!\n");
		return 0;
	}

	if (udma)
		map = cmd64x_ratemask(drive);
	else
		map = XFER_SWDMA | XFER_MWDMA;

	mode = ata_timing_mode(drive, map);

	if (mode < XFER_SW_DMA_0) {
		config_chipset_for_pio(drive, 1);
		return 0;
	}

	config_chipset_for_pio(drive, 0);
	return !drive->channel->speedproc(drive, mode);
}

static int cmd64x_config_drive_for_dma(struct ata_device *drive)
{
	struct hd_driveid *id	= drive->id;
	struct ata_channel *hwif = drive->channel;
	int on = 1;
	int verbose = 1;

	if ((id != NULL) && ((id->capability & 1) != 0) &&
	    hwif->autodma && (drive->type == ATA_DISK)) {
		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			goto fast_ata_pio;
		}
		on = 0;
		verbose = 0;
		if ((id->field_valid & 4)) {
			if (id->dma_ultra & 0x007F) {
				/* Force if Capable UltraDMA */
				on = config_chipset_for_dma(drive, 1);
				if ((id->field_valid & 2) &&
				    (!on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				on = config_chipset_for_dma(drive, 0);
				if (!on)
					goto no_dma_set;
			}
		} else if (udma_white_list(drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			on = config_chipset_for_dma(drive, 0);
			if (!on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		on = 0;
		verbose = 0;
no_dma_set:
		config_chipset_for_pio(drive, 1);
	}

	udma_enable(drive, on, verbose);

	return 0;
}

static int cmd680_dmaproc(struct ata_device *drive)
{
	return cmd64x_config_drive_for_dma(drive);
}

static int cmd64x_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	u8 dma_stat = 0;
	unsigned long dma_base	= ch->dma_base;
	struct pci_dev *dev	= ch->pci_dev;
	u8 jack_slap		= ((dev->device == PCI_DEVICE_ID_CMD_648) || (dev->device == PCI_DEVICE_ID_CMD_649)) ? 1 : 0;

	drive->waiting_for_dma = 0;
	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	if (jack_slap) {
		byte dma_intr = 0;
		byte dma_mask = (ch->unit) ? ARTTIM23_INTR_CH1 : CFR_INTR_CH0;
		byte dma_reg = (ch->unit) ? ARTTIM2 : CFR;
		(void) pci_read_config_byte(dev, dma_reg, &dma_intr);
		/*
		 * DAMN BMIDE is not connected to PCI space!
		 * Have to manually jack-slap that bitch!
		 * To allow the PCI side to read incoming interrupts.
		 */
		(void) pci_write_config_byte(dev, dma_reg, dma_intr|dma_mask);	/* clear the INTR bit */
	}
	udma_destroy_table(ch);	/* purge DMA mappings */
	return (dma_stat & 7) != 4;		/* verify good DMA status */
}

static int cmd64x_udma_irq_status(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	u8 dma_stat = 0;
	u8 dma_alt_stat	= 0;
	unsigned long dma_base	= ch->dma_base;
	struct pci_dev *dev	= ch->pci_dev;
	u8 mask	= (ch->unit) ? MRDMODE_INTR_CH1 : MRDMODE_INTR_CH0;

	dma_stat = inb(dma_base+2);
	(void) pci_read_config_byte(dev, MRDMODE, &dma_alt_stat);
#ifdef DEBUG
	printk("%s: dma_stat: 0x%02x dma_alt_stat: 0x%02x mask: 0x%02x\n", drive->name, dma_stat, dma_alt_stat, mask);
#endif
	if (!(dma_alt_stat & mask)) {
		return 0;
	}
	return (dma_stat & 4) == 4;	/* return 1 if INTR asserted */
}

static int cmd64x_dmaproc(struct ata_device *drive)
{
	return cmd64x_config_drive_for_dma(drive);
}

static int cmd646_1_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	drive->waiting_for_dma = 0;
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* and free any DMA resources */
	return (dma_stat & 7) != 4;		/* verify good DMA status */
}

/*
 * ASUS P55T2P4D with CMD646 chipset revision 0x01 requires the old
 * event order for DMA transfers.
 */
static int cmd646_1_dmaproc(struct ata_device *drive)
{
	return cmd64x_config_drive_for_dma(drive);
}
#endif

static int cmd680_busproc(struct ata_device * drive, int state)
{
#if 0
	struct ata_channel *ch	= drive->channel;
	u8 addr_mask		= (ch->unit) ? 0xB0 : 0xA0;
	u32 stat_config		= 0;

        pci_read_config_dword(ch->pci_dev, addr_mask, &stat_config);

	if (!ch)
		return -EINVAL;

	switch (state) {
		case BUSSTATE_ON:
			ch->drives[0].failures = 0;
			ch->drives[1].failures = 0;
			break;
		case BUSSTATE_OFF:
			ch->drives[0].failures = ch->drives[0].max_failures + 1;
			ch->drives[1].failures = ch->drives[1].max_failures + 1;
			break;
		case BUSSTATE_TRISTATE:
			ch->drives[0].failures = ch->drives[0].max_failures + 1;
			ch->drives[1].failures = ch->drives[1].max_failures + 1;
			break;
		default:
			return 0;
	}
	ch->bus_state = state;
#endif
	return 0;
}

static void cmd680_reset(struct ata_device *drive)
{
#if 0
	struct ata_channel *ch = drive->channel;
	u8 addr_mask = (ch->unit) ? 0xB0 : 0xA0;
	u8 reset = 0;

	pci_read_config_byte(ch->pci_dev, addr_mask, &reset);
	pci_write_config_byte(ch->pci_dev, addr_mask, reset|0x03);
#endif
}

static unsigned int cmd680_pci_init(struct pci_dev *dev)
{
	u8 tmpbyte	= 0;
	pci_write_config_byte(dev, 0x80, 0x00);
	pci_write_config_byte(dev, 0x84, 0x00);
	pci_read_config_byte(dev, 0x8A, &tmpbyte);
	pci_write_config_byte(dev, 0x8A, tmpbyte|0x01);
	pci_write_config_word(dev, 0xA2, 0x328A);
	pci_write_config_dword(dev, 0xA4, 0x328A);
	pci_write_config_dword(dev, 0xA8, 0x4392);
	pci_write_config_dword(dev, 0xAC, 0x4009);
	pci_write_config_word(dev, 0xB2, 0x328A);
	pci_write_config_dword(dev, 0xB4, 0x328A);
	pci_write_config_dword(dev, 0xB8, 0x4392);
	pci_write_config_dword(dev, 0xBC, 0x4009);

	return 0;
}

static unsigned int cmd64x_pci_init(struct pci_dev *dev)
{
	unsigned char mrdmode;
	unsigned int class_rev;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

#ifdef __i386__
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", dev->name, dev->resource[PCI_ROM_RESOURCE].start);
	}
#endif

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
			break;
		case PCI_DEVICE_ID_CMD_646:
			printk("%s: chipset revision 0x%02X, ", dev->name, class_rev);
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
		case PCI_DEVICE_ID_CMD_649:
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
	(void) pci_read_config_byte(dev, MRDMODE, &mrdmode);
	mrdmode &= ~(0x30);
	(void) pci_write_config_byte(dev, MRDMODE, mrdmode);

	/* Use MEMORY READ LINE for reads.
	 * NOTE: Although not mentioned in the PCI0646U specs,
	 *       these bits are write only and won't be read
	 *       back as set or not.  The PCI0646U2 specs clarify
	 *       this point.
	 */
	(void) pci_write_config_byte(dev, MRDMODE, mrdmode | 0x02);

	/* Set reasonable active/recovery/address-setup values. */
	(void) pci_write_config_byte(dev, ARTTIM0,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM0,  0x3f);
	(void) pci_write_config_byte(dev, ARTTIM1,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM1,  0x3f);
#ifdef __i386__
	(void) pci_write_config_byte(dev, ARTTIM23, 0x1c);
#else
	(void) pci_write_config_byte(dev, ARTTIM23, 0x5c);
#endif
	(void) pci_write_config_byte(dev, DRWTIM23, 0x3f);
	(void) pci_write_config_byte(dev, DRWTIM3,  0x3f);
#ifdef CONFIG_PPC
	(void) pci_write_config_byte(dev, UDIDETCR0, 0xf0);
#endif /* CONFIG_PPC */

	return 0;
}

static unsigned int __init cmd64x_init_chipset(struct pci_dev *dev)
{
	if (dev->device == PCI_DEVICE_ID_CMD_680)
		return cmd680_pci_init (dev);
	return cmd64x_pci_init (dev);
}

static unsigned int cmd680_ata66(struct ata_channel *hwif)
{
	byte ata66	= 0;
	byte addr_mask	= (hwif->unit) ? 0xB0 : 0xA0;

	pci_read_config_byte(hwif->pci_dev, addr_mask, &ata66);
	return (ata66 & 0x01) ? 1 : 0;
}

static unsigned int cmd64x_ata66(struct ata_channel *hwif)
{
	byte ata66 = 0;
	byte mask = (hwif->unit) ? 0x02 : 0x01;

	pci_read_config_byte(hwif->pci_dev, BMIDECSR, &ata66);
	return (ata66 & mask) ? 1 : 0;
}

static unsigned int __init cmd64x_ata66_check(struct ata_channel *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	if (dev->device == PCI_DEVICE_ID_CMD_680)
		return cmd680_ata66(hwif);
	return cmd64x_ata66(hwif);
}

static void __init cmd64x_init_channel(struct ata_channel *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int class_rev;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_680:
			hwif->busproc	= cmd680_busproc;
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base)
				hwif->XXX_udma	= cmd680_dmaproc;
#endif
			hwif->resetproc = cmd680_reset;
			hwif->speedproc	= cmd680_tune_chipset;
			hwif->tuneproc	= cmd680_tuneproc;
			break;
		case PCI_DEVICE_ID_CMD_649:
		case PCI_DEVICE_ID_CMD_648:
		case PCI_DEVICE_ID_CMD_643:
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base) {
				hwif->XXX_udma	= cmd64x_dmaproc;
				hwif->udma_stop	= cmd64x_udma_stop;
				hwif->udma_irq_status = cmd64x_udma_irq_status;
			}
#endif
			hwif->tuneproc	= cmd64x_tuneproc;
			hwif->speedproc = cmd64x_tune_chipset;
			break;
		case PCI_DEVICE_ID_CMD_646:
			hwif->chipset = ide_cmd646;
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base) {
				if (class_rev == 0x01) {
					hwif->XXX_udma = cmd646_1_dmaproc;
					hwif->udma_stop = cmd646_1_udma_stop;
				} else {
					hwif->XXX_udma = cmd64x_dmaproc;
					hwif->udma_stop = cmd64x_udma_stop;
					hwif->udma_irq_status = cmd64x_udma_irq_status;
				}
			}
#endif
			hwif->tuneproc	= cmd64x_tuneproc;
			hwif->speedproc	= cmd64x_tune_chipset;
			break;
		default:
			break;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->highmem = 1;
# ifdef CONFIG_IDEDMA_AUTO
		if (!noautodma)
			hwif->autodma = 1;
# endif
	}
#endif
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
	{
		vendor: PCI_VENDOR_ID_CMD,
		device: PCI_DEVICE_ID_CMD_643,
		init_chipset: cmd64x_init_chipset,
		init_channel: cmd64x_init_channel,
		bootable: ON_BOARD,
		flags: ATA_F_SIMPLEX,
	},
	{
		vendor: PCI_VENDOR_ID_CMD,
		device: PCI_DEVICE_ID_CMD_646,
		init_chipset: cmd64x_init_chipset,
		init_channel: cmd64x_init_channel,
		enablebits: {{0x00,0x00,0x00}, {0x51,0x80,0x80}},
		bootable: ON_BOARD,
		flags: ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_CMD,
		device: PCI_DEVICE_ID_CMD_648,
		init_chipset: cmd64x_init_chipset,
		ata66_check: cmd64x_ata66_check,
		init_channel: cmd64x_init_channel,
		bootable: ON_BOARD,
		flags: ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_CMD,
		device: PCI_DEVICE_ID_CMD_649,
		init_chipset: cmd64x_init_chipset,
		ata66_check: cmd64x_ata66_check,
		init_channel: cmd64x_init_channel,
		bootable: ON_BOARD,
		flags: ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_CMD,
		device: PCI_DEVICE_ID_CMD_680,
		init_chipset: cmd64x_init_chipset,
		ata66_check: cmd64x_ata66_check,
		init_channel: cmd64x_init_channel,
		bootable: ON_BOARD,
		flags: ATA_F_DMA
	},
};

int __init init_cmd64x(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

        return 0;
}
