/*
 *  linux/drivers/block/buddha.c -- Amiga Buddha and Catweasel IDE Driver
 *
 *	Copyright (C) 1997 by Geert Uytterhoeven
 *
 *  This driver was written by based on the specifications in README.buddha.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  TODO:
 *    - test it :-)
 *    - tune the timings using the speed-register
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/zorro.h>
#include <linux/ide.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>


    /*
     *  The Buddha has 2 IDE interfaces, the Catweasel has 3
     */

#define BUDDHA_NUM_HWIFS	2
#define CATWEASEL_NUM_HWIFS	3


    /*
     *  Bases of the IDE interfaces (relative to the board address)
     */

#define BUDDHA_BASE1	0x800
#define BUDDHA_BASE2	0xa00
#define BUDDHA_BASE3	0xc00

static const u_int buddha_bases[CATWEASEL_NUM_HWIFS] = {
    BUDDHA_BASE1, BUDDHA_BASE2, BUDDHA_BASE3
};


    /*
     *  Offsets from one of the above bases
     */

#define BUDDHA_DATA	0x00
#define BUDDHA_ERROR	0x06		/* see err-bits */
#define BUDDHA_NSECTOR	0x0a		/* nr of sectors to read/write */
#define BUDDHA_SECTOR	0x0e		/* starting sector */
#define BUDDHA_LCYL	0x12		/* starting cylinder */
#define BUDDHA_HCYL	0x16		/* high byte of starting cyl */
#define BUDDHA_SELECT	0x1a		/* 101dhhhh , d=drive, hhhh=head */
#define BUDDHA_STATUS	0x1e		/* see status-bits */
#define BUDDHA_CONTROL	0x11a

static int buddha_offsets[IDE_NR_PORTS] = {
    BUDDHA_DATA, BUDDHA_ERROR, BUDDHA_NSECTOR, BUDDHA_SECTOR, BUDDHA_LCYL,
    BUDDHA_HCYL, BUDDHA_SELECT, BUDDHA_STATUS, BUDDHA_CONTROL
};


    /*
     *  Other registers
     */

#define BUDDHA_IRQ1	0xf00		/* MSB = 1, Harddisk is source of */
#define BUDDHA_IRQ2	0xf40		/* interrupt */
#define BUDDHA_IRQ3	0xf80

static const int buddha_irqports[CATWEASEL_NUM_HWIFS] = {
    BUDDHA_IRQ1, BUDDHA_IRQ2, BUDDHA_IRQ3
};

#define BUDDHA_IRQ_MR	0xfc0		/* master interrupt enable */


    /*
     *  Board information
     */

static u_long buddha_board = 0;
static int buddha_num_hwifs = -1;


    /*
     *  Check and acknowledge the interrupt status
     */

static int buddha_ack_intr(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = inb(hwif->io_ports[IDE_IRQ_OFFSET]);
    if (!(ch & 0x80))
	return 0;
    return 1;
}


    /*
     *  Any Buddha or Catweasel boards present?
     */

static int find_buddha(void)
{
    u_int key;
    const struct ConfigDev *cd;

    buddha_num_hwifs = 0;
    if ((key = zorro_find(ZORRO_PROD_INDIVIDUAL_COMPUTERS_BUDDHA, 0, 0)))
	buddha_num_hwifs = BUDDHA_NUM_HWIFS;
    else if ((key = zorro_find(ZORRO_PROD_INDIVIDUAL_COMPUTERS_CATWEASEL, 0,
			       0)))
	buddha_num_hwifs = CATWEASEL_NUM_HWIFS;
    if (key) {
	cd = zorro_get_board(key);
	buddha_board = (u_long)cd->cd_BoardAddr;
	if (buddha_board) {
	    buddha_board = ZTWO_VADDR(buddha_board);
	    /* write to BUDDHA_IRQ_MR to enable the board IRQ */
	    *(char *)(buddha_board+BUDDHA_IRQ_MR) = 0;
	    zorro_config_board(key, 0);
	}
    }
    return buddha_num_hwifs;
}


    /*
     *  Probe for a Buddha or Catweasel IDE interface
     *  We support only _one_ of them, no multiple boards!
     */

void buddha_init(void)
{
    hw_regs_t hw;
    int i, index;

    if (buddha_num_hwifs < 0 && !find_buddha())
	return;

    for (i = 0; i < buddha_num_hwifs; i++) {
	ide_setup_ports(&hw, (ide_ioreg_t)(buddha_board+buddha_bases[i]),
			buddha_offsets, 0,
			(ide_ioreg_t)(buddha_board+buddha_irqports[i]),
			buddha_ack_intr, IRQ_AMIGA_PORTS);
	index = ide_register_hw(&hw, NULL);
	if (index != -1)
	    printk("ide%d: %s IDE interface\n", index,
		   buddha_num_hwifs == BUDDHA_NUM_HWIFS ? "Buddha" :
		   					  "Catweasel");
    }
}
