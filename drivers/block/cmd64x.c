/* $Id: cmd64x.c,v 1.21 2000/01/30 23:23:16
 *
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998       David S. Miller (davem@redhat.com)
 * Copyright (C) 1999-2000  Andre Hedrick (andre@suse.com)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>
#include "ide_modes.h"

#define CMD_DEBUG 0
#undef NO_WRITE

#if CMD_DEBUG
#define cmdprintk(x...)	printk(##x)
#else
#define cmdprintk(x...)
#endif

/*
 * CMD64x specific registers definition.
 */

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
#define DRWTIM23	0x58
#define DRWTIM2		0x58
#define BRST		0x59
#define DRWTIM3		0x5b

#define MRDMODE		0x71

#define DISPLAY_CMD64X_TIMINGS

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int cmd64x_get_info(char *, char **, off_t, int);
extern int (*cmd64x_display_info)(char *, char **, off_t, int); /* ide-proc.c */
extern char *ide_media_verbose(ide_drive_t *);
static struct pci_dev *bmide_dev;

static int cmd64x_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = bmide_dev->resource[4].start;
	u8  c0 = 0, c1 = 0;

	switch(bmide_dev->device) {
		case PCI_DEVICE_ID_CMD_648:
			p += sprintf(p, "\n                                CMD648 Chipset.\n");
			break;
		case PCI_DEVICE_ID_CMD_646:
			p += sprintf(p, "\n                                CMD646 Chipset.\n");
			break;
		case PCI_DEVICE_ID_CMD_643:
			p += sprintf(p, "\n                                CMD643 Chipset.\n");
			break;
		default:
			p += sprintf(p, "\n                                CMD64? Chipse.\n");
			break;
	}

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

	return p-buffer;	/* => must be less than 4k! */
}
#endif	/* defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS) */

byte cmd64x_proc = 0;

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
static void program_drive_counts (ide_drive_t *drive, int setup_count, int active_count, int recovery_count)
{
	unsigned long flags;
	ide_drive_t *drives = HWIF(drive)->drives;
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
	int channel = (int) HWIF(drive)->channel;
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
		setup_count = IDE_MAX(drives[0].drive_data, drives[1].drive_data);
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
	(void) pci_read_config_byte(HWIF(drive)->pci_dev, arttim_regs[channel][slave], &temp_b);
#ifndef NO_WRITE
	(void) pci_write_config_byte(HWIF(drive)->pci_dev, arttim_regs[channel][slave],
		((byte) setup_count) | (temp_b & 0x3f));
	(void) pci_write_config_byte(HWIF(drive)->pci_dev, drwtim_regs[channel][slave],
		(byte) ((active_count << 4) | recovery_count));
#endif
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
static void cmd64x_tuneproc (ide_drive_t *drive, byte mode_wanted)
{
	int setup_time, active_time, recovery_time, clock_time, pio_mode, cycle_time;
	byte recovery_count2, cycle_count;
	int setup_count, active_count, recovery_count;
	int bus_speed = ide_system_bus_speed();
	/*byte b;*/
	ide_pio_data_t  d;

	switch (mode_wanted) {
		case 8: /* set prefetch off */
		case 9: /* set prefetch on */
			mode_wanted &= 1;
			/*set_prefetch_mode(index, mode_wanted);*/
			cmdprintk("%s: %sabled cmd640 prefetch\n", drive->name, mode_wanted ? "en" : "dis");
			return;
	}

	(void) ide_get_best_pio_mode (drive, mode_wanted, 5, &d);
	pio_mode = d.pio_mode;
	cycle_time = d.cycle_time;

	/*
	 * I copied all this complicated stuff from cmd640.c and made a few minor changes.
	 * For now I am just going to pray that it is correct.
	 */
	if (pio_mode > 5)
		pio_mode = 5;
	setup_time  = ide_pio_timings[pio_mode].setup_time;
	active_time = ide_pio_timings[pio_mode].active_time;
	recovery_time = cycle_time - (setup_time + active_time);
	clock_time = 1000 / bus_speed;
	cycle_count = (cycle_time + clock_time - 1) / clock_time;

	setup_count = (setup_time + clock_time - 1) / clock_time;

	active_count = (active_time + clock_time - 1) / clock_time;

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

	printk ("%s: selected cmd646 PIO mode%d (%dns)%s, clocks=%d/%d/%d\n",
		drive->name, pio_mode, cycle_time,
		d.overridden ? " (overriding vendor mode)" : "",
		setup_count, active_count, recovery_count);
}

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
	cmd64x_tuneproc(drive, 5);
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
	return HWIF(drive)->dmaproc(dma_func, drive);
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
#if 0
	/* Set reasonable active/recovery/address-setup values. */
	(void) pci_write_config_byte(dev, ARTTIM0,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM0,  0x3f);
	(void) pci_write_config_byte(dev, ARTTIM1,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM1,  0x3f);
	(void) pci_write_config_byte(dev, ARTTIM23, 0x5c);
	(void) pci_write_config_byte(dev, DRWTIM23, 0x3f);
	(void) pci_write_config_byte(dev, DRWTIM3,  0x3f);
#else
	(void) pci_write_config_byte(dev, ARTTIM23, 0x1c);
#endif

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)
	cmd64x_proc = 1;
	bmide_dev = dev;
	cmd64x_display_info = &cmd64x_get_info;
#endif /* DISPLAY_CMD64X_TIMINGS && CONFIG_PROC_FS */

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

	hwif->tuneproc = &cmd64x_tuneproc;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

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
