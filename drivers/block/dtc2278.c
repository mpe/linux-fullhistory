/*
 *  linux/drivers/block/dtc2278.c       Version 0.01  Feb 06, 1996
 *
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
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
 * From: andy@cercle.cts.com (Dyan Wile)
 *
 * Below is a patch for DTC-2278 - alike software-programmable controllers
 * The code enables the secondary IDE controller and the PIO4 (3?) timings on
 * the primary (EIDE). You may probably have to enable the 32-bit support to
 * get the full speed. You better get the disk interrupts disabled ( hdparm -u0
 * /dev/hd.. ) for the drives connected to the EIDE interface. (I get my
 * filesystem  corrupted with -u1, but under heavy disk load only :-)
 *
 * From: mlord@bnr.ca -- this chipset is now forced to use the "serialize" feature,
 * which hopefully will make it more reliable to use.. maybe it has the same bugs
 * as the CMD640B and RZ1000 ??
 */

static void sub22 (char b, char c)
{
	int i;

	for(i = 0; i < 3; ++i) {
		inb(0x3f6);
		outb_p(b,0xb0);
		inb(0x3f6);
		outb_p(c,0xb4);
		inb(0x3f6);
		if(inb(0xb4) == c) {
			outb_p(7,0xb0);
			inb(0x3f6);
			return;	/* success */
		}
	}
}

static void tune_dtc2278 (ide_drive_t *drive, byte pio_mode)
{
	unsigned long flags;

	if (pio_mode != 255) {	/* auto-tune not yet supported here */
		if (pio_mode >= 3) {
			save_flags(flags);
			cli();
			/*
			 * This enables PIO mode4 (3?) on the first interface
			 */
			sub22(1,0xc3);
			sub22(0,0xa0);
			restore_flags(flags);
		} else {
			/* we don't know how to set it back again.. */
		}
	}
}

void init_dtc2278 (void)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	/*
	 * This enables the second interface
	 */
	outb_p(4,0xb0);
	inb(0x3f6);
	outb_p(0x20,0xb4);
	inb(0x3f6);
	restore_flags(flags);

	ide_hwifs[0].serialized = 1;
	ide_hwifs[0].chipset = ide_dtc2278;
	ide_hwifs[1].chipset = ide_dtc2278;
	ide_hwifs[0].tuneproc = &tune_dtc2278;
}
