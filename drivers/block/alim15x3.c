/*
 * linux/drivers/block/alim15x3.c	Version 0.05	Jun. 29, 1999
 *
 *  Copyright (C) 1998-99 Michel Aubry, Maintainer
 *  Copyright (C) 1998-99 Andrzej Krzysztofowicz, Maintainer
 *  Copyright (C) 1998-99 Andre Hedrick, Integrater and Maintainer
 *
 *  (U)DMA capable version of ali 1533/1543(C)
 *
 *  Default disable (U)DMA on all devices execpt hard disks.
 *  This measure of overkill is needed to stablize the chipset code.
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ide_modes.h"

#define DISPLAY_ALI_TIMINGS

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int ali_get_info(char *buffer, char **addr, off_t offset, int count, int dummy);
extern int (*ali_display_info)(char *, char **, off_t, int, int);  /* ide-proc.c */
struct pci_dev *bmide_dev;

char *fifo[4] = {
	"FIFO Off",
	"FIFO On ",
	"DMA mode",
	"PIO mode" };

char *udmaT[8] = {
	"1.5T",
	"  2T",
	"2.5T",
	"  3T",
	"3.5T",
	"  4T",
	"  6T",
	"  8T"
};

char *channel_status[8] = {
	"OK            ",
	"busy          ",
	"DRQ           ",
	"DRQ busy      ",
	"error         ",
	"error busy    ",
	"error DRQ     ",
	"error DRQ busy"
};
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

static void ali15x3_tune_drive (ide_drive_t *drive, byte pio)
{
	ide_pio_data_t d;
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	int s_time, a_time, c_time;
	byte s_clc, a_clc, r_clc;
	unsigned long flags;
	int bus_speed = ide_system_bus_speed();
	int port = hwif->index ? 0x5c : 0x58;

	pio = ide_get_best_pio_mode(drive, pio, 5, &d);
	s_time = ide_pio_timings[pio].setup_time;
	a_time = ide_pio_timings[pio].active_time;
	if ((s_clc = (s_time * bus_speed + 999) / 1000) >= 8)
		s_clc = 0;
	if ((a_clc = (a_time * bus_speed + 999) / 1000) >= 8)
		a_clc = 0;
	c_time = ide_pio_timings[pio].cycle_time;

#if 0
	if ((r_clc = ((c_time - s_time - a_time) * bus_speed + 999) / 1000) >= 16)
		r_clc = 0;
#endif

	if (!(r_clc = (c_time * bus_speed + 999) / 1000 - a_clc - s_clc)) {
		r_clc = 1;
	} else {
		if (r_clc >= 16)
		r_clc = 0;
	}
	save_flags(flags);
	cli();
	pci_write_config_byte(dev, port, s_clc);
	pci_write_config_byte(dev, port+drive->select.b.unit+2, (a_clc << 4) | r_clc);
	restore_flags(flags);

	/*
	 * setup   active  rec
	 * { 70,   165,    365 },   PIO Mode 0
	 * { 50,   125,    208 },   PIO Mode 1
	 * { 30,   100,    110 },   PIO Mode 2
	 * { 30,   80,     70  },   PIO Mode 3 with IORDY
	 * { 25,   70,     25  },   PIO Mode 4 with IORDY  ns
	 * { 20,   50,     30  }    PIO Mode 5 with IORDY (nonstandard)
	 */

}

__initfunc(unsigned int pci_init_ali15x3 (struct pci_dev *dev, const char *name))
{
	byte confreg0 = 0, confreg1 =0, progif = 0;
	int errors = 0;

	if (pci_read_config_byte(dev, 0x50, &confreg1))
		goto veryspecialsettingserror;
	if (!(confreg1 & 0x02))
		if (pci_write_config_byte(dev, 0x50, confreg1 | 0x02))
			goto veryspecialsettingserror;

	if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif))
		goto veryspecialsettingserror;
	if (!(progif & 0x40)) {
		/*
		 * The way to enable them is to set progif
		 * writable at 0x4Dh register, and set bit 6
		 * of progif to 1:
		 */
		if (pci_read_config_byte(dev, 0x4d, &confreg0))
			goto veryspecialsettingserror;
		if (confreg0 & 0x80)
			if (pci_write_config_byte(dev, 0x4d, confreg0 & ~0x80))
				goto veryspecialsettingserror;
		if (pci_write_config_byte(dev, PCI_CLASS_PROG, progif | 0x40))
			goto veryspecialsettingserror;
		if (confreg0 & 0x80)
			if (pci_write_config_byte(dev, 0x4d, confreg0))
				errors++;
	}

	if ((pci_read_config_byte(dev, PCI_CLASS_PROG, &progif)) || (!(progif & 0x40)))
		goto veryspecialsettingserror;

	printk("%s: enabled read of IDE channels state (en/dis-abled) %s.\n",
		name, errors ? "with Error(s)" : "Succeeded" );
	return 0;

veryspecialsettingserror:
	printk("%s: impossible to enable read of IDE channels state (en/dis-abled)!\n", name);
	return 0;
}

int ali15x3_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			if (drive->media == ide_cdrom) {
				ide_hwif_t *hwif	= HWIF(drive);
				struct pci_dev *dev	= hwif->pci_dev;
				struct hd_driveid *id	= drive->id;
				byte cd_dma_fifo	= 0;

				pci_read_config_byte(dev, 0x53, &cd_dma_fifo);

				if (((id->field_valid & 4) || (id->field_valid & 2)) &&
				    (id->capability & 1) && hwif->autodma) {
					unsigned long dma_set_bit  = hwif->dma_base + 2;
#if 0
					if (cd_dma_fifo & 0x02)
						pci_write_config_byte(dev, 0x53, cd_dma_fifo & ~0x02);
					pci_write_config_byte(dev, 0x53, cd_dma_fifo|0x01);
#else
					pci_write_config_byte(dev, 0x53, cd_dma_fifo|0x01|0x02);
#endif
					if (drive->select.b.unit & 0x01) {
						outb(inb(dma_set_bit)|0x40, dma_set_bit);
					} else {
						outb(inb(dma_set_bit)|0x20, dma_set_bit);
					}
				} else {
					if (cd_dma_fifo & 0x01)
						pci_write_config_byte(dev, 0x53, cd_dma_fifo & ~0x01);
					pci_write_config_byte(dev, 0x53, cd_dma_fifo|0x02);
				}
			} else if (drive->media != ide_disk) {
				return ide_dmaproc(ide_dma_off_quietly, drive);
			}
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

__initfunc(void ide_init_ali15x3 (ide_hwif_t *hwif))
{
	struct pci_dev *dev;
	byte ideic, inmir, iderev;
	byte irq_routing_table[] = { -1,  9, 3, 10, 4,  5, 7,  6,
				      1, 11, 0, 12, 0, 14, 0, 15 };

	pci_read_config_byte(hwif->pci_dev, PCI_REVISION_ID, &iderev);

	hwif->irq = hwif->channel ? 15 : 14;
	for (dev = pci_devices; dev; dev=dev->next) /* look for ISA bridge */
		if (dev->vendor==PCI_VENDOR_ID_AL &&
		    dev->device==PCI_DEVICE_ID_AL_M1533)
			break;
	if (dev) {			
		pci_read_config_byte(dev, 0x58, &ideic);
		ideic = ideic & 0x03;
		if ((hwif->channel && ideic == 0x03) ||
		    (!hwif->channel && !ideic)) {
			pci_read_config_byte(dev, 0x44, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		} else
			if (hwif->channel && !(ideic & 0x01)) {
				pci_read_config_byte(dev, 0x75, &inmir);
				inmir = inmir & 0x0f;
				hwif->irq = irq_routing_table[inmir];
			}
	}
#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
	bmide_dev = hwif->pci_dev;
	ali_display_info = &ali_get_info;
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

	hwif->tuneproc = &ali15x3_tune_drive;
	if ((hwif->dma_base) && (iderev >= 0xC1)) {
		/* M1543C or newer for DMAing */
		hwif->dmaproc = &ali15x3_dmaproc;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
	return;
}

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
static int ali_get_info(char *buffer, char **addr, off_t offset, int count, int dummy)
{
	byte reg53h, reg5xh, reg5yh, reg5xh1, reg5yh1;
	unsigned int bibma;
	byte c0, c1;
	byte rev, tmp;
	char *p = buffer;
	char *q;

	/* fetch rev. */
	pci_read_config_byte(bmide_dev, 0x08, &rev);
	if (rev >= 0xc1)	/* M1543C or newer */
		udmaT[7] = " ???";
	else
		fifo[3]  = "   ???  ";

	/* first fetch bibma: */
	pci_read_config_dword(bmide_dev, 0x20, &bibma);
	bibma = (bibma & 0xfff0) ;
	/*
	 * at that point bibma+0x2 et bibma+0xa are byte
	 * registers to investigate:
	 */
	c0 = inb((unsigned short)bibma + 0x02);
	c1 = inb((unsigned short)bibma + 0x0a);

	p += sprintf(p,
		"\n                                Ali M15x3 Chipset.\n");
	p += sprintf(p,
		"                                ------------------\n");
	pci_read_config_byte(bmide_dev, 0x78, &reg53h);
	p += sprintf(p, "PCI Clock: %d.\n", reg53h);

	pci_read_config_byte(bmide_dev, 0x53, &reg53h);
	p += sprintf(p,
		"CD_ROM FIFO:%s, CD_ROM DMA:%s\n",
		(reg53h & 0x02) ? "Yes" : "No ",
		(reg53h & 0x01) ? "Yes" : "No " );
	pci_read_config_byte(bmide_dev, 0x74, &reg53h);
	p += sprintf(p,
		"FIFO Status: contains %d Words, runs%s%s\n\n",
		(reg53h & 0x3f),
		(reg53h & 0x40) ? " OVERWR" : "",
		(reg53h & 0x80) ? " OVERRD." : "." );

	p += sprintf(p,
		"-------------------primary channel-------------------secondary channel---------\n\n");

	pci_read_config_byte(bmide_dev, 0x09, &reg53h);
	p += sprintf(p,
		"channel status:       %s                               %s\n",
		(reg53h & 0x20) ? "On " : "Off",
		(reg53h & 0x10) ? "On " : "Off" );

	p += sprintf(p,
		"both channels togth:  %s                               %s\n",
		(c0&0x80) ? "No " : "Yes",
		(c1&0x80) ? "No " : "Yes" );

	pci_read_config_byte(bmide_dev, 0x76, &reg53h);
	p += sprintf(p,
		"Channel state:        %s                    %s\n",
		channel_status[reg53h & 0x07],
		channel_status[(reg53h & 0x70) >> 4] );

	pci_read_config_byte(bmide_dev, 0x58, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5c, &reg5yh);
	p += sprintf(p,
		"Add. Setup Timing:    %dT                                %dT\n",
		(reg5xh & 0x07) ? (reg5xh & 0x07) : 8,
		(reg5yh & 0x07) ? (reg5yh & 0x07) : 8 );

	pci_read_config_byte(bmide_dev, 0x59, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5d, &reg5yh);
	p += sprintf(p,
		"Command Act. Count:   %dT                                %dT\n"
		"Command Rec. Count:   %dT                               %dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8, 
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16 );

	p += sprintf(p,
		"----------------drive0-----------drive1------------drive0-----------drive1------\n\n");
	p += sprintf(p,
		"DMA enabled:      %s              %s               %s              %s\n",
		(c0&0x20) ? "Yes" : "No ",
		(c0&0x40) ? "Yes" : "No ",
		(c1&0x20) ? "Yes" : "No ",
		(c1&0x40) ? "Yes" : "No " );

	pci_read_config_byte(bmide_dev, 0x54, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x55, &reg5yh);
	q = "FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n";
	if (rev < 0xc1) {
		if ((rev == 0x20) && (pci_read_config_byte(bmide_dev, 0x4f, &tmp), (tmp &= 0x20))) {
			p += sprintf(p, q, 8, 8, 8, 8);
		} else {
			p += sprintf(p, q,
				(reg5xh & 0x03) + 12,
				((reg5xh & 0x30)>>4) + 12,
				(reg5yh & 0x03) + 12,
				((reg5yh & 0x30)>>4) + 12 );
		}
	} else {
		p += sprintf(p, q,
			(tmp = (reg5xh & 0x03)) ? (tmp << 3) : 4,
			(tmp = ((reg5xh & 0x30)>>4)) ? (tmp << 3) : 4,
			(tmp = (reg5yh & 0x03)) ? (tmp << 3) : 4,
			(tmp = ((reg5yh & 0x30)>>4)) ? (tmp << 3) : 4 );
	}

#if 0
	p += sprintf(p, 
		"FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n",
		(reg5xh & 0x03) + 12,
		((reg5xh & 0x30)>>4) + 12,
		(reg5yh & 0x03) + 12,
		((reg5yh & 0x30)>>4) + 12 );
#endif

	p += sprintf(p,
		"FIFO mode:        %s         %s          %s         %s\n",
		fifo[((reg5xh & 0x0c) >> 2)],
		fifo[((reg5xh & 0xc0) >> 6)],
		fifo[((reg5yh & 0x0c) >> 2)],
		fifo[((reg5yh & 0xc0) >> 6)] );

	pci_read_config_byte(bmide_dev, 0x5a, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5b, &reg5xh1);
	pci_read_config_byte(bmide_dev, 0x5e, &reg5yh);
	pci_read_config_byte(bmide_dev, 0x5f, &reg5yh1);

	p += sprintf(p,/*
		"------------------drive0-----------drive1------------drive0-----------drive1------\n")*/
		"Dt RW act. Cnt    %2dT              %2dT               %2dT              %2dT\n"
		"Dt RW rec. Cnt    %2dT              %2dT               %2dT              %2dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5xh1 & 0x70) ? ((reg5xh1 & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8,
		(reg5yh1 & 0x70) ? ((reg5yh1 & 0x70) >> 4) : 8,
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5xh1 & 0x0f) ? (reg5xh1 & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16,
		(reg5yh1 & 0x0f) ? (reg5yh1 & 0x0f) : 16 );

	p += sprintf(p,
		"-----------------------------------UDMA Timings--------------------------------\n\n");

	pci_read_config_byte(bmide_dev, 0x56, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x57, &reg5yh);
	p += sprintf(p,
		"UDMA:             %s               %s                %s               %s\n"
		"UDMA timings:     %s             %s              %s             %s\n\n",
		(reg5xh & 0x08) ? "OK" : "No",
		(reg5xh & 0x80) ? "OK" : "No",
		(reg5yh & 0x08) ? "OK" : "No",
		(reg5yh & 0x80) ? "OK" : "No",
		udmaT[(reg5xh & 0x07)],
		udmaT[(reg5xh & 0x70) >> 4],
		udmaT[reg5yh & 0x07],
		udmaT[(reg5yh & 0x70) >> 4] );

	return p-buffer; /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */
