/*
 *  linux/drivers/block/qd6580.c       Version 0.02  Feb 09, 1996
 *
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

/*
 * QDI QD6580 EIDE controller fast support by Colten Edwards.
 * No net access, but (maybe) can be reached at pje120@cs.usask.ca
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
#include "ide_modes.h"

/*
 * Register 0xb3 looks like:
 *	0x4f is fast		mode3 ?
 *	0x3f is medium		mode2 ?
 *	0x2f is slower		mode1 ?
 *	0x1f is slower yet	mode0 ?
 *	0x0f ???		???
 *
 * Don't know whether this sets BOTH drives, or just the first drive.
 * Don't know if there is a separate setting for the second drive.
 *
 * Feel free to patch this if you have one of these beasts
 * and can work out the answers!
 *
 * I/O ports are 0xb0 0xb2 and 0xb3
 *
 * More research on qd6580 being done by willmore@cig.mot.com (David)
 *	-- this is apparently a *dual* IDE interface
 */

static void tune_qd6580 (ide_drive_t *drive, byte pio)
{
	unsigned long flags;

	pio = ide_get_best_pio_mode(drive, pio, 3, NULL);

	save_flags(flags);	/* all CPUs */
	cli();			/* all CPUs */
	outb_p(0x8d,0xb0);
	outb_p(0x0 ,0xb2);
	outb_p(((pio+1)<<4)|0x0f,0xb3);
	inb(0x3f6);
	restore_flags(flags);	/* all CPUs */
}

void init_qd6580 (void)
{
	ide_hwifs[0].chipset = ide_qd6580;
	ide_hwifs[1].chipset = ide_qd6580;
	ide_hwifs[0].tuneproc = &tune_qd6580;
	ide_hwifs[0].mate = &ide_hwifs[1];
	ide_hwifs[1].mate = &ide_hwifs[0];
	ide_hwifs[1].channel = 1;
}
