/*
 *  linux/drivers/block/ali14xx.c       Version 0.03  Feb 09, 1996
 *
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

/*
 * ALI M14xx chipset EIDE controller
 *
 * Adapted from code developed by derekn@vw.ece.cmu.edu.  -ml
 * Derek's notes follow:
 *
 * I think the code should be pretty understandable,
 * but I'll be happy to (try to) answer questions.
 *
 * The critical part is in the setupDrive function.  The initRegisters
 * function doesn't seem to be necessary, but the DOS driver does it, so
 * I threw it in.
 *
 * I've only tested this on my system, which only has one disk.  I posted
 * it to comp.sys.linux.hardware, so maybe some other people will try it
 * out.
 *
 * Derek Noonburg  (derekn@ece.cmu.edu)
 * 95-sep-26
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
 * This should be set to the system's local bus (PCI or VLB) speed,
 * e.g., 33 for a 486DX33 or 486DX2/66.  Legal values are anything
 * from 25 to 50.  Setting this too *low* will make the EIDE
 * controller unable to communicate with the disks.
 *
 * I suggest using a default of 50, since it should work ok with any
 * system.  (Low values cause problems because it multiplies by bus speed
 * to get cycles, and thus gets a too-small cycle count and tries to
 * access the disks too fast.  I tried this once under DOS and it locked
 * up the system.)	-- derekn@vw.ece.cmu.edu
 */
#define ALI_14xx_BUS_SPEED	50	/* PCI / VLB bus speed */

/* port addresses for auto-detection */
#define ALI_NUM_PORTS 4
static int ports[ALI_NUM_PORTS] = {0x074, 0x0f4, 0x034, 0x0e4};

/* register initialization data */
typedef struct { byte reg, data; } RegInitializer;

static RegInitializer initData[] = {
	{0x01, 0x0f}, {0x02, 0x00}, {0x03, 0x00}, {0x04, 0x00},
	{0x05, 0x00}, {0x06, 0x00}, {0x07, 0x2b}, {0x0a, 0x0f},
	{0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},
	{0x29, 0x00}, {0x2a, 0x00}, {0x2f, 0x00}, {0x2b, 0x00},
	{0x2c, 0x00}, {0x2d, 0x00}, {0x2e, 0x00}, {0x30, 0x00},
	{0x31, 0x00}, {0x32, 0x00}, {0x33, 0x00}, {0x34, 0xff},
	{0x35, 0x03}, {0x00, 0x00}
};

/* default timing parameters for each PIO mode */
static struct { int time1, time2; } timeTab[4] = {
	{600, 165},	/* PIO 0 */
	{383, 125},	/* PIO 1 */
	{240, 100},	/* PIO 2 */
	{180,  80}	/* PIO 3 */
};

/* timing parameter registers for each drive */
static struct { byte reg1, reg2, reg3, reg4; } regTab[4] = {
	{0x03, 0x26, 0x04, 0x27},     /* drive 0 */
	{0x05, 0x28, 0x06, 0x29},     /* drive 1 */
	{0x2b, 0x30, 0x2c, 0x31},     /* drive 2 */
	{0x2d, 0x32, 0x2e, 0x33},     /* drive 3 */
};

static int basePort = 0;	/* base port address */
static int regPort = 0;		/* port for register number */
static int dataPort = 0;	/* port for register data */
static byte regOn;	/* output to base port to access registers */
static byte regOff;	/* output to base port to close registers */

/*------------------------------------------------------------------------*/

/*
 * Read a controller register.
 */
static inline byte inReg (byte reg)
{
	outb_p(reg, regPort);
	return inb(dataPort);
}

/*
 * Write a controller register.
 */
static void outReg (byte data, byte reg)
{
	outb_p(reg, regPort);
	outb_p(data, dataPort);
}

/*
 * Set PIO mode for the specified drive.
 * This function computes timing parameters
 * and sets controller registers accordingly.
 */
static void ali14xx_tune_drive (ide_drive_t *drive, byte pio)
{
	int driveNum;
	int time1, time2, time1a;
	byte param1, param2, param3, param4;
	struct hd_driveid *id = drive->id;
	unsigned long flags;

	if (pio == 255)
		pio = ide_get_best_pio_mode(drive);
	if (pio > 3)
		pio = 3;

	/* calculate timing, according to PIO mode */
	time1 = timeTab[pio].time1;
	time2 = timeTab[pio].time2;
	if (pio == 3) {
		time1a = (id->capability & 0x08) ? id->eide_pio_iordy : id->eide_pio;
		if (time1a != 0 && time1a < time1)
			time1 = time1a;
	}
	param3 = param1 = (time2 * ALI_14xx_BUS_SPEED + 999) / 1000;
	param4 = param2 = (time1 * ALI_14xx_BUS_SPEED + 999) / 1000 - param1;
	if (pio != 3) {
		param3 += 8;
		param4 += 8;
	}
	printk("%s: PIO mode%d, t1=%dns, t2=%dns, cycles = %d+%d, %d+%d\n",
		drive->name, pio, time1, time2, param1, param2, param3, param4);

	/* stuff timing parameters into controller registers */
	driveNum = (HWIF(drive)->index << 1) + drive->select.b.unit;
	save_flags(flags);
	cli();
	outb_p(regOn, basePort);
	outReg(param1, regTab[driveNum].reg1);
	outReg(param2, regTab[driveNum].reg2);
	outReg(param3, regTab[driveNum].reg3);
	outReg(param4, regTab[driveNum].reg4);
	outb_p(regOff, basePort);
	restore_flags(flags);
}

/*
 * Auto-detect the IDE controller port.
 */
static int findPort (void)
{
	int i;
	byte t;
	unsigned long flags;

	save_flags(flags);
	cli();
	for (i = 0; i < ALI_NUM_PORTS; ++i) {
		basePort = ports[i];
		regOff = inb(basePort);
		for (regOn = 0x30; regOn <= 0x33; ++regOn) {
			outb_p(regOn, basePort);
			if (inb(basePort) == regOn) {
				regPort = basePort + 4;
				dataPort = basePort + 8;
				t = inReg(0) & 0xf0;
				outb_p(regOff, basePort);
				restore_flags(flags);
				if (t != 0x50)
					return 0;
				return 1;  /* success */
			}
		}
		outb_p(regOff, basePort);
	}
	restore_flags(flags);
	return 0;
}

/*
 * Initialize controller registers with default values.
 */
static int initRegisters (void) {
	RegInitializer *p;
	byte t;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(regOn, basePort);
	for (p = initData; p->reg != 0; ++p)
		outReg(p->data, p->reg);
	outb_p(0x01, regPort);
	t = inb(regPort) & 0x01;
	outb_p(regOff, basePort);
	restore_flags(flags);
	return t;
}

void init_ali14xx (void)
{
	/* auto-detect IDE controller port */
	if (!findPort()) {
		printk("ali14xx: not found\n");
		return;
	}

	printk("ali14xx: base= 0x%03x, regOn = 0x%02x\n", basePort, regOn);
	ide_hwifs[0].chipset = ide_ali14xx;
	ide_hwifs[1].chipset = ide_ali14xx;
	ide_hwifs[0].tuneproc = &ali14xx_tune_drive;
	ide_hwifs[1].tuneproc = &ali14xx_tune_drive;

	/* initialize controller registers */
	if (!initRegisters()) {
		printk("ali14xx: Chip initialization failed\n");
		return;
	}
}
