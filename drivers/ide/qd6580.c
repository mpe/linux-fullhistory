/*
 *  linux/drivers/ide/qd6580.c      Version 0.03    May 13, 2000
 *
 *  Copyright (C) 1996-2000  Linus Torvalds & author (see below)
 */

/*
 *  Version 0.03	Cleaned auto-tune, added probe
 * 
 * QDI QD6580 EIDE controller fast support
 * 
 * To activate controller support use kernel parameter "ide0=qd6580"
 * To enable tuning use kernel parameter "ide0=autotune"
 */

/* 
 * Rewritten from the work of Colten Edwards <pje120@cs.usask.ca> by
 * Samuel Thibault <samuel.thibault@fnac.net>
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
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ide_modes.h"

/*
 * I/O ports are 0xb0 0xb1 0xb2 and 0xb3
 *            or 0x30 0x31 0x32 and 0x33
 *	-- this is a dual IDE interface with I/O chips
 *
 * More research on qd6580 being done by willmore@cig.mot.com (David)
 */

/* 
 * 0xb0: Timer1
 *
 *  
 * 0xb1: Status
 *
 * && 0xf0 is either 0b1010000 or 0b01010000, or else it isn't a qd6580
 * bit 3 & 2: unknown (useless ?) I have 0 & 1, respectively
 * bit 1: 1 if qd6580 baseport is 0xb0
 *        0 if qd6580 baseport is 0x30
 * bit 0: 1 if ide baseport is 0x1f0
 *        0 if ide baseport is 0x170
 *   (? Strange: the Dos driver uses it, and then forces baseport to 0x1f0 ?)
 * 
 *        
 * 0xb2: Timer2
 *
 * 
 * 0xb3: Control
 *
 * bits 0-3 are always set 1
 * bit 6 : if 1, must be set 1
 * bit 1 : if 1, bit 7 must be set 1
 * bit 0 : if 1, drives are independant, we can have two different timers for
 *               the two drives.
 *         if 0, we have to take the slowest drive into account,
 *               but we may tune the second hwif ?
 */

typedef struct ide_hd_timings_s {
	int active_time;		/* Active pulse (ns) minimum */
	int recovery_time;		/* Recovery pulse (ns) minimum */
} ide_hd_timings_t;

static int basePort;		/* base port address (0x30 or 0xb0) */
static byte status;			/* status register of qd6580 */
static byte control;		/* control register of qd6580 */

/* truncates a in [b,c] */
#define IDE_IN(a,b,c)   ( ((a)<(b)) ? (b) : ( (a)>(c) ? (c) : (a)) )

static int bus_clock;		/* Vesa local bus clock (ns) */
static int tuned=0;			/* to remember whether we've already been tuned */

/*
 * tune_drive
 *
 * Finds timings for the specified drive, returns it in struc t
 */

static void tune_drive ( ide_drive_t *drive, byte pio, ide_hd_timings_t *t)
{
	ide_pio_data_t d;

	t->active_time   = 0xaf;
	t->recovery_time = 0x19f; /* worst cases values from the dos driver */

	if (drive->present == 0) {	/* not present : free to give any timing */
		t->active_time = 0x0;
		t->recovery_time = 0x0;
		return;
	}
	
	pio = ide_get_best_pio_mode(drive, pio, 4, &d);

	if (pio) {
	
		switch (pio) {
			case 0: break;
			case 3: t->active_time = 0x56;
						t->recovery_time = d.cycle_time-0x66;
					break;
			case 4: t->active_time = 0x46;
						t->recovery_time = d.cycle_time-0x3d;
					break;
			default: if (d.cycle_time >= 0xb4) {
						t->active_time = 0x6e;
						t->recovery_time = d.cycle_time - 0x78;
					} else {
						t->active_time = ide_pio_timings[pio].active_time;
						t->recovery_time = d.cycle_time 
								-t->active_time
								-ide_pio_timings[pio].setup_time;
					}
		}		
	}	
	printk("%s: PIO mode%d, tim1=%dns tim2=%dns\n", drive->name, pio, t->active_time, t->recovery_time);
}

/* 
 * tune_ide
 *
 * Tunes the whole ide, ie tunes each drives, and takes the worst timings
 * to tune qd6580
 */

static void tune_ide ( ide_hwif_t *hwif, byte pio )
{
	unsigned long flags;
	ide_hd_timings_t t[2]={{0,0},{0,0}};
	
	byte active_cycle;
	byte recovery_cycle;
	byte parameter;
	int bus_speed = ide_system_bus_speed ();
	
	bus_clock = 1000 / bus_speed;
	
	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	outb( (bus_clock<30) ? 0x0 : 0x0a, basePort + 0x02);
	outb( 0x40 | ((control & 0x02) ? 0x9f:0x1f), basePort+0x03);
	restore_flags(flags);	

	tune_drive (&hwif->drives[0], pio, &t[0]);
	tune_drive (&hwif->drives[1], pio, &t[1]);

	t[0].active_time   = IDE_MAX(t[0].active_time,  t[1].active_time);
	t[0].recovery_time = IDE_MAX(t[0].recovery_time,t[1].recovery_time);
	
	active_cycle   = 17-IDE_IN(t[0].active_time   / bus_clock + 1, 2, 17);
	recovery_cycle = 15-IDE_IN(t[0].recovery_time / bus_clock + 1, 2, 15);
	
	parameter=active_cycle | (recovery_cycle<<4);
		
	printk("%s: tim1=%dns tim2=%dns => %#x\n", hwif->name, t[0].active_time, t[0].recovery_time, parameter);
	
	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	outb_p(parameter,0xb0);
	inb(0x3f6);
	restore_flags(flags);	/* all CPUs */
	
}

/*
 * tune_qd6580
 *
 * tunes the hwif if not tuned
 */

static void tune_qd6580 (ide_drive_t *drive, byte pio)
{
	if (! tuned) {
		tune_ide(HWIF(drive), pio);
		tuned = 1;
	}
}

/*
 * testreg
 *
 * tests if the given port is a register
 */

static int __init testreg(int port)
{
	byte savereg;
	byte readreg;
	unsigned long flags;
	
	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	savereg = inb(port);
	outb_p(0x15,port);		/* safe value */
	readreg = inb_p(port);
	outb(savereg,port);
	restore_flags(flags);	/* all CPUs */

	if (savereg == 0x15) {
		printk("Outch ! the probe for qd6580 isn't reliable !\n");
		printk("Please contact samuel.thibault@fnac.net to tell about your hardware\n");
		printk("Assuming qd6580 is present");
	}

	return (readreg == 0x15);
}

/* 
 * trybase:
 *
 * tries to find a qd6580 at the given base and save it if found
 */

static int __init trybase (int base)
{
	unsigned long flags;

	save_flags(flags);		/* all CPUs */
	cli();					/* all CPUs */
	status = inb(base+0x01);
	control = inb(base+0x03);
	restore_flags(flags);	/* all CPUs */

	if (((status & 0xf0) != 0x50) && ((status & 0xf0) != 0xa0)) return(0);
	if (! ( ((status & 0x02) == 0x0) == (base == 0x30) ) ) return (0);

	/* Seems to be OK, let's use it */
	
	basePort = base;
	return(testreg(base));
}

/* 
 * probe:
 *
 * probes qd6580 at 0xb0 (the default) or 0x30
 */

static int __init probe (void)
{
	return (trybase(0xb0) ? 1 : trybase(0x30));
}


void __init init_qd6580 (void)
{
	if (! probe()) {
		printk("qd6580: not found\n");
		return;
	}
	
	printk("qd6580: base=%#x, status=%#x, control=%#x\n", basePort, status, control);
	
	ide_hwifs[0].chipset = ide_qd6580;
	ide_hwifs[1].chipset = ide_qd6580;
	ide_hwifs[0].tuneproc = &tune_qd6580;
	ide_hwifs[0].mate = &ide_hwifs[1];
	ide_hwifs[1].mate = &ide_hwifs[0];
	ide_hwifs[1].channel = 1;
}
