/*
 *  linux/drivers/block/trm290.c	Version 1.00  December 3, 1997
 *
 *  Copyright (c) 1997-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA function
 * of the Tekram TRM290 chip, used on a variety of PCI IDE add-on boards,
 * including a "Precision Instruments" board.  The TRM290 pre-dates
 * the sff-8038 standard (ide-dma.c) by a few months, and differs
 * significantly enough to warrant separate routines for some functions,
 * while re-using others from ide-dma.c.
 *
 * EXPERIMENTAL!  It works for me (a sample of one).
 *
 * Works reliably for me in DMA mode (READs only),
 * DMA WRITEs are disabled by default (see #define below);
 *
 * DMA is not enabled automatically for this chipset,
 * but can be turned on manually (with "hdparm -d1") at run time.
 *
 * I need volunteers with "spare" drives for further testing
 * and development, and maybe to help figure out the peculiarities.
 * Even knowing the registers (below), some things behave strangely.
 */

#define TRM290_NO_DMA_WRITES	/* DMA writes seem unreliable sometimes */

/*
 * TRM-290 PCI-IDE2 Bus Master Chip
 * ================================
 * The configuration registers are addressed in normal I/O port space
 * and are used as follows:
 *
 * 0x3df2 when WRITTEN: chiptest register (byte, write-only)
 *	bit7 must always be written as "1"
 *	bits6-2 undefined
 *	bit1 1=legacy_compatible_mode, 0=native_pci_mode
 *	bit0 1=test_mode, 0=normal(default)
 *
 * 0x3df2 when READ: status register (byte, read-only)
 *	bits7-2 undefined
 *	bit1 channel0 busmaster interrupt status 0=none, 1=asserted
 *	bit0 channel0 interrupt status 0=none, 1=asserted
 *
 * 0x3df3 Interrupt mask register
 *	bits7-5 undefined
 *	bit4 legacy_header: 1=present, 0=absent
 *	bit3 channel1 busmaster interrupt status 0=none, 1=asserted (read only)
 *	bit2 channel1 interrupt status 0=none, 1=asserted (read only)
 *	bit1 channel1 interrupt mask: 1=masked, 0=unmasked(default)
 *	bit0 channel0 interrupt mask: 1=masked, 0=unmasked(default)
 *
 * 0x3df1 "CPR" Config Pointer Register (byte)
 *	bit7 1=autoincrement CPR bits 2-0 after each access of CDR
 *	bit6 1=min. 1 wait-state posted write cycle (default), 0=0 wait-state
 *	bit5 0=enabled master burst access (default), 1=disable  (write only)
 *	bit4 PCI DEVSEL# timing select: 1=medium(default), 0=fast
 *	bit3 0=primary IDE channel, 1=secondary IDE channel
 *	bits2-0 register index for accesses through CDR port
 *
 * 0x3df0 "CDR" Config Data Register (word)
 *	two sets of seven config registers,
 *	selected by CPR bit 3 (channel) and CPR bits 2-0 (index 0 to 6),
 *	each index defined below:
 *
 * Index-0 Base address register for command block (word)
 *	defaults: 0x1f0 for primary, 0x170 for secondary
 *
 * Index-1 general config register (byte)
 *	bit7 1=DMA enable, 0=DMA disable
 *	bit6 1=activate IDE_RESET, 0=no action (default)
 *	bit5 1=enable IORDY, 0=disable IORDY (default)
 *	bit4 0=16-bit data port(default), 1=8-bit (XT) data port
 *	bit3 interrupt polarity: 1=active_low, 0=active_high(default)
 *	bit2 power-saving-mode(?): 1=enable, 0=disable(default) (write only)
 *	bit1 bus_master_mode(?): 1=enable, 0=disable(default)
 *	bit0 enable_io_ports: 1=enable(default), 0=disable
 *
 * Index-2 read-ahead counter preload bits 0-7 (byte, write only)
 *	bits7-0 bits7-0 of readahead count
 *
 * Index-3 read-ahead config register (byte, write only)
 *	bit7 1=enable_readahead, 0=disable_readahead(default)
 *	bit6 1=clear_FIFO, 0=no_action
 *	bit5 undefined
 *	bit4 mode4 timing control: 1=enable, 0=disable(default)
 *	bit3 undefined
 *	bit2 undefined
 *	bits1-0 bits9-8 of read-ahead count
 *
 * Index-4 base address register for control block (word)
 *	defaults: 0x3f6 for primary, 0x376 for secondary
 *
 * Index-5 data port timings (shared by both drives) (byte)
 *	standard PCI "clk" (clock) counts, default value = 0xf5
 *
 *	bits7-6 setup time:  00=1clk, 01=2clk, 10=3clk, 11=4clk
 *	bits5-3 hold time:	000=1clk, 001=2clk, 010=3clk,
 *				011=4clk, 100=5clk, 101=6clk,
 *				110=8clk, 111=12clk
 *	bits2-0 active time:	000=2clk, 001=3clk, 010=4clk,
 *				011=5clk, 100=6clk, 101=8clk,
 *				110=12clk, 111=16clk
 *
 * Index-6 command/control port timings (shared by both drives) (byte)
 *	same layout as Index-5, default value = 0xde
 *
 * Suggested CDR programming for PIO mode0 (600ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0xf5,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0xf5,0xde	; secondary
 *
 * Suggested CDR programming for PIO mode3 (180ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0x09,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0x09,0xde	; secondary
 *
 * Suggested CDR programming for PIO mode4 (120ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0x00,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0x00,0xde	; secondary
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <asm/io.h>

#include "ide.h"

static void select_dma_or_pio(ide_hwif_t *hwif, int dma)
{
	static int previous[2] = {-1,-1};
	unsigned long flags;

	if (previous[hwif->pci_port] != dma) {
		unsigned short cfg1 = dma ? 0xa3 : 0x21;
		previous[hwif->pci_port] = dma;
		save_flags(flags);
		cli();
		outb(0x51|(hwif->pci_port<<3),0x3df1);
		outw(cfg1,0x3df0);
		restore_flags(flags);
	}
}

/*
 * trm290_dma_intr() is the handler for trm290 disk read/write DMA interrupts
 */
static void trm290_dma_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	struct request *rq = HWGROUP(drive)->rq;

	stat = GET_STAT();	/* get drive status */
	if (OK_STAT(stat,DRIVE_READY,drive->bad_wstat|DRQ_STAT)) {
		unsigned short dma_stat = inw(HWIF(drive)->dma_base + 2);
		if (dma_stat == 0x00ff) {
			rq = HWGROUP(drive)->rq;
			for (i = rq->nr_sectors; i > 0;) {
				i -= rq->current_nr_sectors;
				ide_end_request(1, HWGROUP(drive));
			}
			return;
		}
		printk("%s: bad trm290 DMA status: 0x%04x\n", drive->name, dma_stat);
	}
	sti();
	ide_error(drive, "dma_intr", stat);
}

static int trm290_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int count, reading = 1 << 1;

	if (drive->media == ide_disk) {
		switch (func) {
			case ide_dma_write:
				reading = 0;
#ifdef TRM290_NO_DMA_WRITES
				break;	/* always use PIO for writes */
#endif
			case ide_dma_read:
				if (!(count = ide_build_dmatable(drive)))
					break;		/* try PIO instead of DMA */
				select_dma_or_pio(hwif, 1);	/* select DMA mode */
				outl_p(virt_to_bus(hwif->dmatable)|reading, hwif->dma_base);
				outw((count * 2) - 1, hwif->dma_base+2); /* start DMA */
				ide_set_handler(drive, &trm290_dma_intr, WAIT_CMD);
				OUT_BYTE(reading ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);
				return 0;
			default:
				return ide_dmaproc(func, drive);
		}
	}
	select_dma_or_pio(hwif, 0);	/* force PIO mode for this operation */
	return 1;
}

static void trm290_selectproc (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	select_dma_or_pio(hwif, drive->using_dma);
	OUT_BYTE(drive->select.all, hwif->io_ports[IDE_SELECT_OFFSET]);
}

/*
 * Invoked from ide-dma.c at boot time.
 */
__initfunc(void ide_init_trm290 (byte bus, byte fn, ide_hwif_t *hwif))
{
	hwif->chipset = ide_trm290;
	hwif->selectproc = trm290_selectproc;
	hwif->drives[0].autotune = 2;	/* play it safe */
	hwif->drives[1].autotune = 2;	/* play it safe */
	ide_setup_dma(hwif, hwif->pci_port ? 0x3d74 : 0x3df4, 2);
	hwif->dmaproc = &trm290_dmaproc;
}
