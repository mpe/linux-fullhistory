/*
 *  linux/drivers/block/ht6580.c       Version 0.01  Feb 06, 1996
 *
 *  Copyright (C) 1995-1996  Linus Torvalds & author (see below)
 */

#undef REALLY_SLOW_IO           /* most systems can safely undef this */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <asm/io.h>
#include "ide.h"

/*
 * This routine handles interface switching for the peculiar hardware design
 * on the F.G.I./Holtek HT-6560B VLB IDE interface.
 * The HT-6560B can only enable one IDE port at a time, and requires a
 * silly sequence (below) whenever we switch between primary and secondary.
 *
 * This stuff is courtesy of malafoss@snakemail.hut.fi
 *
 * At least one user has reported that this code can confuse the floppy
 * controller and/or driver -- perhaps this should be changed to use
 * a read-modify-write sequence, so as not to disturb other bits in the reg?
 */

/*
 * We don't know what all of the bits are for, but we *do* know about these:
 *	bit5 (0x20): "1" selects slower speed (?)
 *	bit0 (0x01): "1" selects second interface
 */
static byte qd6560b_selects [2][MAX_DRIVES] = {{0x3c,0x3c}, {0x3d,0x3d}};

static void qd6560b_selectproc (ide_drive_t *drive)	/* called from ide.c */
{
	static byte current_select = 0;
	byte drive_select = qd6560b_selects[HWIF(drive)->index][drive->select.b.unit];

	if (drive_select != current_select) {
		byte t;
		unsigned long flags;
		save_flags (flags);
		cli();
		current_select = drive_select;
		(void) inb(0x3e6);
		(void) inb(0x3e6);
		(void) inb(0x3e6);
		/*
		 * Note: input bits are reversed to output bits!!
		 */
		t = inb(0x3e6) ^ 0x3f;
		t &= (~0x21);
		t |= (current_select & 0x21);
		outb(t,0x3e6);
		restore_flags (flags);
	}
}

/*
 * Autodetection and initialization of ht6560b
 */
int try_to_init_ht6560b(void)
{
	byte orig_value;
	int i;

	/* Autodetect ht6560b */
	if ((orig_value=inb(0x3e6)) == 0xff)
		return 0;

	for (i=3;i>0;i--) {
		outb(0x00,0x3e6);
		if (!( (~inb(0x3e6)) & 0x3f )) {
			  outb(orig_value,0x3e6);
			  return 0;
		}
	}
	outb(0x00,0x3e6);
	if ((~inb(0x3e6))& 0x3f) {
		outb(orig_value,0x3e6);
		return 0;
	}
	/* 
	 * Ht6560b autodetected:
	 *     reverse input bits to output bits
	 *     initialize bit1 to 0
	 */
	outb((orig_value ^ 0x3f) & 0xfd,0x3e6);
	printk("ht6560b: detected and initialized\n");
	return 1;
}

static void tune_ht6560b (ide_drive_t *drive, byte pio)
{
	unsigned int hwif, unit;

	if (pio == 255)  {	/* auto-tune */
		if (drive->media != ide_disk) {
			pio = 0; /* cdroms don't like our fast mode */
		} else {
			struct hd_driveid *id = drive->id;
			pio = id->tPIO;
			if ((id->field_valid & 0x02) && (id->eide_pio_modes & 0x01))
				pio = 3;
		}
	}
	hwif = HWIF(drive)->index;
	unit = drive->select.b.unit;
	if (pio < 3)
		qd6560b_selects[hwif][unit] |= 0x20;
	else
		qd6560b_selects[hwif][unit] &= ~0x20;
}

void init_ht6560b (void)
{
	if (check_region(0x3e6,1)) {
		printk("\nht6560b: PORT 0x3e6 ALREADY IN USE\n");
	} else {
		if (try_to_init_ht6560b()) {
			request_region(0x3e6, 1, "ht6560b");
			ide_hwifs[0].chipset = ide_ht6560b;
			ide_hwifs[1].chipset = ide_ht6560b;
			ide_hwifs[0].selectproc = &qd6560b_selectproc;
			ide_hwifs[1].selectproc = &qd6560b_selectproc;
			ide_hwifs[0].tuneproc = &tune_ht6560b;
			ide_hwifs[1].tuneproc = &tune_ht6560b;
			ide_hwifs[0].serialized = 1;
		}
	}
}
