/*
 *  linux/drivers/block/macide.c -- Macintosh IDE Driver
 *
 *     Copyright (C) 1998 by Michael Schmitz
 *
 *  This driver was written based on information obtained from the MacOS IDE
 *  driver binary by Mikael Forselius
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/zorro.h>
#include <linux/ide.h>

#include <asm/machw.h>
#include <asm/macintosh.h>
#include <asm/macints.h>

    /*
     *  Base of the IDE interface (see ATAManager ROM code)
     */

#define MAC_HD_BASE	0x50f1a000

    /*
     *  Offsets from the above base (scaling 4)
     */

#define MAC_HD_DATA	0x00
#define MAC_HD_ERROR	0x04		/* see err-bits */
#define MAC_HD_NSECTOR	0x08		/* nr of sectors to read/write */
#define MAC_HD_SECTOR	0x0c		/* starting sector */
#define MAC_HD_LCYL	0x10		/* starting cylinder */
#define MAC_HD_HCYL	0x14		/* high byte of starting cyl */
#define MAC_HD_SELECT	0x18		/* 101dhhhh , d=drive, hhhh=head */
#define MAC_HD_STATUS	0x1c		/* see status-bits */
#define MAC_HD_CONTROL	0x38		/* control/altstatus */

static int macide_offsets[IDE_NR_PORTS] = {
    MAC_HD_DATA, MAC_HD_ERROR, MAC_HD_NSECTOR, MAC_HD_SECTOR, MAC_HD_LCYL,
    MAC_HD_HCYL, MAC_HD_SELECT, MAC_HD_STATUS, MAC_HD_CONTROL
};

	/*
	 * Other registers
	 */

	/* 
	 * IDE interrupt status register for both (?) hwifs on Quadra
	 * Initial setting: 0xc
	 * Guessing again:
	 * Bit 0+1: some interrupt flags
	 * Bit 2+3: some interrupt enable
	 * Bit 4:   ??
	 * Bit 5:   IDE interrupt flag (any hwif)
	 * Bit 6:   maybe IDE interrupt enable (any hwif) ??
	 * Bit 7:   Any interrupt condition
	 *
	 * Only relevant item: bit 5, to be checked by mac_ack_intr
	 */

#define MAC_HD_ISR	0x101

	/*
	 * IDE interrupt glue - seems to be wired to Nubus, Slot C?
	 * (ROM code disassembly again)
	 * First try: just use Nubus interrupt for Slot C. Have Nubus code call
	 * a wrapper to ide_intr that checks the ISR (see above).
	 * Need to #define IDE_IRQ_NUBUS though.
	 * Alternative method: set a mac_ide_hook function pointer to the wrapper 
	 * here and have via_do_nubus call that hook if set. 
	 *
	 * Quadra needs the hook, Powerbook can use Nubus slot C. 
	 * Checking the ISR on Quadra is done by mac_ack_intr (see Amiga code). mac_ide_intr
	 * mac_ide_intr is obsolete except for providing the hwgroup argument.
	 */

	/* The Mac hwif data, for passing hwgroup to ide_intr */
static ide_hwif_t *mac_hwif = NULL;

	/* The function pointer used in the Nubus handler */
void (*mac_ide_intr_hook)(int, void *, struct pt_regs *) = NULL;

	/*
	 * Only purpose: feeds the hwgroup to the main IDE handler. 
	 * Obsolete as soon as Nubus code is fixed WRT pseudo slot C int.
	 * (should be the case on Powerbooks)
	 * Alas, second purpose: feed correct irq to IDE handler (I know,
	 * that's cheating) :-(((
	 * Fix needed for interrupt code: accept Nubus ints in the regular
	 * request_irq code, then register Powerbook IDE as Nubus slot C, 
	 * Quadra as slot F (F for fictious).
	 */
void mac_ide_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	ide_intr(mac_hwif->irq, mac_hwif->hwgroup, regs);
}

    /*
     *  Check the interrupt status
     *
     *  Note: In 2.0 kernels, there have been timing problems with the 
     *  Powerbook IDE interface (BUSY was asserted too long after the
     *  interrupt triggered). Result: repeated errors, recalibrate etc. 
     *  Adding a wait loop to read_intr, write_intr and set_geom_intr
     *  fixed the problem (waits in read/write_intr were present for Amiga
     *  already). 
     *  Powerbooks were not tested with 2.1 due to lack of FPU emulation
     *  (thanks Apple for using LC040). If the BUSY problem resurfaces in 
     *  2.1, my best bet would be to add the wait loop right here, afterr
     *  checking the interrupt register.
     */

static int mac_ack_intr(ide_hwif_t *hwif)
{
    unsigned char ch;

    ch = inb(hwif->io_ports[IDE_IRQ_OFFSET]);
    if (!(ch & 0x20))
	return 0;
    return 1;
}

    /*
     *  Probe for a Macintosh IDE interface
     */

void macide_init(void)
{
    hw_regs_t hw;
    int index = -1;

    if (MACH_IS_MAC) {
	switch(macintosh_config->ide_type) {
	case 0:
	    break;

	case MAC_IDE_QUADRA:
	    ide_setup_ports(&hw, (ide_ioreg_t)MAC_HD_BASE, macide_offsets,
	    		    0, (ide_ioreg_t)(MAC_HD_BASE+MAC_HD_ISR),
			    mac_ack_intr, IRQ_MAC_NUBUS);
	    index = ide_register_hw(&hw, &mac_hwif);
	    mac_ide_intr_hook = mac_ide_intr;
	    break;

	default:
	    ide_setup_ports(&hw, (ide_ioreg_t)MAC_HD_BASE, macide_offsets,
	    		    0, 0, NULL, IRQ_MAC_NUBUS);
	    index = ide_register_hw(&hw, &mac_hwif);
	    break;
	}

        if (index != -1) {
	    if (macintosh_config->ide_type == MAC_IDE_QUADRA)
		printk("ide%d: Macintosh Quadra IDE interface\n", index);
	    else
		printk("ide%d: Macintosh Powerbook IDE interface\n", index);
	}
    }
}
