/*
 *  linux/drivers/block/ht6580.c       Version 0.04  Mar 19, 1996
 *
 *  Copyright (C) 1995-1996  Linus Torvalds & author (see below)
 */

/*
 *
 *  Version 0.01        Initial version hacked out of ide.c
 *
 *  Version 0.02        Added support for PIO modes, auto-tune
 *
 *  Version 0.03        Some cleanups
 *
 * I reviewed some assembler source listings of htide drivers and found
 * out how they setup those cycle time interfacing values, as they at Holtek
 * call them. IDESETUP.COM that is supplied with the drivers figures out
 * optimal values and fetches those values to drivers. I found out that
 * they use IDE_SELECT_REG to fetch timings to the ide board right after
 * interface switching. After that it was quite easy to add code to
 * ht6560b.c.
 *
 * IDESETUP.COM gave me values 0x24, 0x45, 0xaa, 0xff that worked fine
 * for hda and hdc. But hdb needed higher values to work, so I guess
 * that sometimes it is necessary to give higher value than IDESETUP
 * gives.   [see cmd640.c for an extreme example of this. -ml]
 *
 * Perhaps I should explain something about these timing values:
 * The higher nibble of value is the Recovery Time  (rt) and the lower nibble
 * of the value is the Active Time  (at). Minimum value 2 is the fastest and
 * the maximum value 15 is the slowest. Default values should be 15 for both.
 * So 0x24 means 2 for rt and 4 for at. Each of the drives should have
 * both values, and IDESETUP gives automatically rt=15 st=15 for cdroms or
 * similar. If value is too small there will be all sorts of failures.
 *
 * Port 0x3e6 bit 0x20 sets these timings on/off. If 0x20 bit is set
 * these timings are disabled.
 *
 * Mikko Ala-Fossi
 *
 * More notes:
 *
 * There's something still missing from the initialization code, though.
 * If I have booted to dos sometime after power on, I can get smaller
 * timing values working. Perhaps I could soft-ice the initialization.
 *
 * -=- malafoss@snakemail.hut.fi -=- searching the marvels of universe -=-
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
 * This routine handles interface switching for the peculiar hardware design
 * on the F.G.I./Holtek HT-6560B VLB IDE interface.
 * The HT-6560B can only enable one IDE port at a time, and requires a
 * silly sequence (below) whenever we switch between primary and secondary.
 *
 * This stuff is courtesy of malafoss@snakemail.hut.fi
 *                          (or maf@nemesis.tky.hut.fi)
 *
 * At least one user has reported that this code can confuse the floppy
 * controller and/or driver -- perhaps this should be changed to use
 * a read-modify-write sequence, so as not to disturb other bits in the reg?
 */

/*
 * The special i/o-port that HT-6560B uses to select interfaces:
 */
#define HT_SELECT_PORT     0x3e6

/*
 * We don't know what all of the bits are for, but we *do* know about these:
 *	bit5 (0x20): "1" selects slower speed by disabling use of timing values
 *	bit0 (0x01): "1" selects second interface
 */
static byte ht6560b_selects [2][MAX_DRIVES] = {{0x3c,0x3c}, {0x3d,0x3d}};

/*
 * VLB ht6560b Timing values:
 *
 * Timing byte consists of
 *      High nibble:  Recovery Time  (rt)
 *           The valid values range from 2 to 15. The default is 15.
 *
 *      Low nibble:   Active Time    (at)
 *           The valid values range from 2 to 15. The default is 15.
 *
 * You can obtain optimized timing values by running Holtek IDESETUP.COM
 * for DOS. DOS drivers get their timing values from command line, where
 * the first value is the Recovery Time and the second value is the
 * Active Time for each drive. Smaller value gives higher speed.
 * In case of failures you should probably fall back to a higher value.
 *
 * Hopefully this example will make it clearer:
 *
 * DOS:    DEVICE=C:\bin\HTIDE\HTIDE.SYS /D0=2,4 /D1=4,5 /D2=10,10 /D3=15,15
 * Linux:  byte ht6560b_timings [][] = {{0x24, 0x45}, {0xaa, 0xff}};
 *
 * Note: There are no ioctls to change these values directly,
 * but settings can be approximated as PIO modes, using "hdparm":
 *
 * rc.local:  hdparm -p3 /dev/hda -p2 /dev/hdb -p1 /dev/hdc -p0 /dev/hdd
 */

static byte ht6560b_timings [2][MAX_DRIVES] = {{0xff,0xff}, {0xff,0xff}};

static byte pio_to_timings[6] = {0xff, 0xaa, 0x45, 0x24, 0x13, 0x12};

/*
 * This routine is invoked from ide.c to prepare for access to a given drive.
 */
static void ht6560b_selectproc (ide_drive_t *drive)
{
	byte t;
	unsigned long flags;
	static byte current_select = 0;
	static byte current_timing = 0;
	byte select = ht6560b_selects[HWIF(drive)->index][drive->select.b.unit];
        byte timing = ht6560b_timings[HWIF(drive)->index][drive->select.b.unit];

	if (select != current_select || timing != current_timing) {
		current_select = select;
		current_timing = timing;
		save_flags (flags);
		cli();
		(void) inb(HT_SELECT_PORT);
		(void) inb(HT_SELECT_PORT);
		(void) inb(HT_SELECT_PORT);
		/*
		 * Note: input bits are reversed to output bits!!
		 */
		t = inb(HT_SELECT_PORT) ^ 0x3f;
		t &= (~0x21);
		t |= (current_select & 0x21);
		outb(t, HT_SELECT_PORT);
                /*
                 * Set timing for this drive:
                 */
                outb (timing, IDE_SELECT_REG);
                (void) inb (IDE_STATUS_REG);
		restore_flags (flags);
#ifdef DEBUG
		printk("ht6560b: %s: select=%#x timing=%#x\n", drive->name, t, timing);
#endif
	}
	OUT_BYTE(drive->select.all,IDE_SELECT_REG);
}

/*
 * Autodetection and initialization of ht6560b
 */
int try_to_init_ht6560b(void)
{
	byte orig_value;
	int i;

	/* Autodetect ht6560b */
	if ((orig_value=inb(HT_SELECT_PORT)) == 0xff)
		return 0;

	for (i=3;i>0;i--) {
		outb(0x00, HT_SELECT_PORT);
		if (!( (~inb(HT_SELECT_PORT)) & 0x3f )) {
			  outb(orig_value, HT_SELECT_PORT);
			  return 0;
		}
	}
	outb(0x00, HT_SELECT_PORT);
	if ((~inb(HT_SELECT_PORT))& 0x3f) {
		outb(orig_value, HT_SELECT_PORT);
		return 0;
	}
	/*
	 * Ht6560b autodetected:
	 *     reverse input bits to output bits
	 *     initialize bit1 to 0
	 */
	outb((orig_value ^ 0x3f) & 0xfd, HT_SELECT_PORT);

	printk("\nht6560b: detected and initialized");
	return 1;
}

static void tune_ht6560b (ide_drive_t *drive, byte pio)
{
	unsigned int hwif, unit;

	if (pio == 255)  {	/* auto-tune */
		if (drive->media != ide_disk)
			pio = 0; /* some cdroms don't like fast modes (?) */
		else
			pio = ide_get_best_pio_mode (drive);
	}
	if (pio > 5)
		pio = 5;
	unit = drive->select.b.unit;
	hwif = HWIF(drive)->index;
	ht6560b_timings[hwif][unit] = pio_to_timings[pio];
	if (pio == 0)
		ht6560b_selects[hwif][unit] |= 0x20;
	else
		ht6560b_selects[hwif][unit] &= ~0x20;
}

void init_ht6560b (void)
{
	if (check_region(HT_SELECT_PORT,1)) {
		printk("\nht6560b: PORT 0x3e6 ALREADY IN USE\n");
	} else {
		if (try_to_init_ht6560b()) {
			request_region(HT_SELECT_PORT, 1, ide_hwifs[0].name);
			ide_hwifs[0].chipset = ide_ht6560b;
			ide_hwifs[1].chipset = ide_ht6560b;
			ide_hwifs[0].selectproc = &ht6560b_selectproc;
			ide_hwifs[1].selectproc = &ht6560b_selectproc;
			ide_hwifs[0].tuneproc = &tune_ht6560b;
			ide_hwifs[1].tuneproc = &tune_ht6560b;
			ide_hwifs[0].serialized = 1;
			ide_hwifs[1].serialized = 1;
		} else
			printk("\nht6560b: not found\n");
	}
}
