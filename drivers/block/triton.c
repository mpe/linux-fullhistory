/*
 *  linux/drivers/block/triton.c	Version 1.01  Aug 28, 1995
 *
 *  Copyright (c) 1995  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the Bus Master IDE DMA function
 * of the Intel PCI Triton chipset (82371FB).
 *
 * DMA is currently supported only for hard disk drives (not cdroms).
 *
 * Support for cdroms will likely be added at a later date,
 * after broader experience has been obtained with hard disks.
 *
 * Up to four drives may be enabled for DMA, and the Triton chipset will
 * (hopefully) arbitrate the PCI bus among them.  Note that the 82371FB chip
 * provides a single "line buffer" for the BM IDE function, so performance of
 * multiple (two) drives doing DMA simultaneously will suffer somewhat,
 * as they contest for that resource bottleneck.  This is handled transparently
 * inside the 82371FB chip.
 *
 * By default, DMA support is prepared for use, but is currently enabled only
 * for drives which support multi-word DMA mode2 (mword2), or which are
 * recognized as "good" (see table below).  Drives with only mode0 or mode1
 * (single or multi) DMA should also work with this chipset/driver (eg. MC2112A)
 * but are not enabled by default.  Use "hdparm -i" to view modes supported
 * by a given drive.
 *
 * The hdparm-2.4 (or later) utility can be used for manually enabling/disabling
 * DMA support, but must be (re-)compiled against this kernel version or later.
 *
 * To enable DMA, use "hdparm -d1 /dev/hd?" on a per-drive basis after booting.
 * If problems arise, ide.c will disable DMA operation after a few retries.
 * This error recovery mechanism works and has been extremely well exercised.
 *
 * IDE drives, depending on their vintage, may support several different modes
 * of DMA operation.  The boot-time modes are indicated with a "*" in
 * the "hdparm -i" listing, and can be changed with *knowledgeable* use of
 * the "hdparm -X" feature.  There is seldom a need to do this, as drives
 * normally power-up with their "best" PIO/DMA modes enabled.
 *
 * Testing was done with an ASUS P55TP4XE/100 system and the following drives:
 *
 *   Quantum Fireball 1080A (1Gig w/83kB buffer), DMA mode2, PIO mode4.
 *	- DMA mode2 works fine (7.4MB/sec), despite the tiny on-drive buffer.
 *	- This drive also does PIO mode4, at about the same speed as DMA mode2.
 *
 *   Micropolis MC2112A (1Gig w/508kB buffer), drive pre-dates EIDE and ATA2.
 *	- DMA works fine (2.2MB/sec), probably due to the large on-drive buffer.
 *	- This older drive can also be tweaked for fastPIO (3.7MB/sec) by using
 *	  maximum clock settings (5,4) and setting all flags except prefetch.
 *
 *   Western Digital AC31000H (1Gig w/128kB buffer), DMA mode1, PIO mode3.
 *	- DMA does not work reliably.  The drive appears to be somewhat tardy
 *	  in deasserting DMARQ at the end of a sector.  This is evident in
 *	  the observation that WRITEs work most of the time, depending on
 *	  cache-buffer occupancy, but multi-sector reads seldom work.
 *
 * Drives like the AC31000H could likely be made to work if all DMA were done
 * one sector at a time, but that would likely negate any advantage over PIO.
 *
 * If you have any drive models to add, email your results to:  mlord@bnr.ca
 * Keep an eye on /var/adm/messages for "DMA disabled" messages.
 */
#define _TRITON_C
#include <linux/config.h>
#ifndef CONFIG_BLK_DEV_TRITON
#define CONFIG_BLK_DEV_TRITON y
#endif
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/io.h>

#include "ide.h"

/*
 * good_dma_drives() lists the model names (from "hdparm -i")
 * of drives which do not support mword2 DMA but which are
 * known to work fine with this interface under Linux.
 */
const char *good_dma_drives[] = {"Micropolis 2112A"};

/*
 * Our Physical Region Descriptor (PRD) table should be large enough
 * to handle the biggest I/O request we are likely to see.  Since requests
 * can have no more than 256 sectors, and since the typical blocksize is
 * two sectors, we can get by with a limit of 128 entries here for the
 * usual worst case.  Most requests seem to include some contiguous blocks,
 * further reducing the number of table entries required.
 *
 * Note that the driver reverts to PIO mode for individual requests that exceed
 * this limit (possible with 512 byte blocksizes, eg. MSDOS f/s), so handling
 * 100% of all crazy scenarios here is not necessary.
 */
#define PRD_ENTRIES	128	/* max memory area count per DMA */

/*
 * dma_intr() is the handler for disk read/write DMA interrupts
 */
static void dma_intr (ide_drive_t *drive)
{
	byte stat, dma_stat;
	int i;
	struct request *rq = HWGROUP(drive)->rq;
	unsigned short dma_base = HWIF(drive)->dma_base;

	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(inb(dma_base)&~1, dma_base);	/* stop DMA operation */
	stat = GET_STAT();			/* get drive status */
	if (OK_STAT(stat,DRIVE_READY,drive->bad_wstat|DRQ_STAT)) {
		if ((dma_stat & 7) == 4) {	/* verify good DMA status */
			rq = HWGROUP(drive)->rq;
			for (i = rq->nr_sectors; i > 0;) {
				i -= rq->current_nr_sectors;
				ide_end_request(1, HWGROUP(drive));
			}
			IDE_DO_REQUEST;
			return;
		}
		printk("%s: bad DMA status: 0x%02x\n", drive->name, dma_stat);
	}
	sti();
	if (!ide_error(drive, "dma_intr", stat))
		IDE_DO_REQUEST;
}

/*
 * build_dmatable() prepares a dma request.
 * Returns 0 if all went okay, returns 1 otherwise.
 */
static int build_dmatable (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	struct buffer_head *bh = rq->bh;
	unsigned long size, addr, *table = HWIF(drive)->dmatable;
	unsigned int count = 0;

	do {
		/*
		 * Determine addr and size of next buffer area.  We assume that
		 * individual virtual buffers are always composed linearly in
		 * physical memory.  For example, we assume that any 8kB buffer
		 * is always composed of two adjacent physical 4kB pages rather
		 * than two possibly non-adjacent physical 4kB pages.
		 */
		if (bh == NULL) {  /* paging requests have (rq->bh == NULL) */
			addr = virt_to_bus (rq->buffer);
			size = rq->nr_sectors << 9;
		} else {
			/* group sequential buffers into one large buffer */
			addr = virt_to_bus (bh->b_data);
			size = bh->b_size;
			while ((bh = bh->b_reqnext) != NULL) {
				if ((addr + size) != virt_to_bus (bh->b_data))
					break;
				size += bh->b_size;
			}
		}

		/*
		 * Fill in the dma table, without crossing any 64kB boundaries.
		 * We assume 16-bit alignment of all blocks.
		 */
		while (size) {
			if (++count >= PRD_ENTRIES) {
				printk("%s: DMA table too small\n", drive->name);
				return 1; /* revert to PIO for this request */
			} else {
				unsigned long bcount = 0x10000 - (addr & 0xffff);
				if (bcount > size)
					bcount = size;
				*table++ = addr;
				*table++ = bcount;
				addr += bcount;
				size -= bcount;
			}
		}
	} while (bh != NULL);
	if (count) {
		*--table |= 0x80000000;	/* set End-Of-Table (EOT) bit */
		return 0;
	}
	printk("%s: empty DMA table?\n", drive->name);
	return 1;	/* let the PIO routines handle this weirdness */
}

/*
 * triton_dmaproc() initiates/aborts DMA read/write operations on a drive.
 *
 * The caller is assumed to have selected the drive and programmed the drive's
 * sector address using CHS or LBA.  All that remains is to prepare for DMA
 * and then issue the actual read/write DMA/PIO command to the drive.
 *
 * Returns 0 if all went well.
 * Returns 1 if DMA read/write could not be started, in which case
 * the caller should revert to PIO for the current request.
 */
static int triton_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	const char **list;
	unsigned long dma_base = HWIF(drive)->dma_base;

	if (func == ide_dma_abort) {
		outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
		return 0;
	}
	if (func == ide_dma_check) {
		struct hd_driveid *id = drive->id;
		if (id && (id->capability & 1)) {
			/* Enable DMA on any drive that supports mword2 DMA */
			if ((id->field_valid & 2) && (id->dma_mword & 0x404) == 0x404) {
				drive->using_dma = 1;
				return 0;		/* DMA enabled */
			}
			/* Consult the list of known "good" drives */
			list = good_dma_drives;
			while (*list) {
				if (!strcmp(*list++,id->model)) {
					drive->using_dma = 1;
					return 0;	/* DMA enabled */
				}
			}
		}
		return 1;	/* DMA not enabled */
	}
	if (build_dmatable (drive))
		return 1;
	outl(virt_to_bus (HWIF(drive)->dmatable), dma_base + 4); /* PRD table */
	outb((!func) << 3, dma_base);			/* specify r/w */
	outb(0x26, dma_base+2);				/* clear status bits */
	ide_set_handler (drive, &dma_intr);		/* issue cmd to drive */
	OUT_BYTE(func ? WIN_WRITEDMA : WIN_READDMA, IDE_COMMAND_REG);
	outb(inb(dma_base)|1, dma_base);			/* begin DMA */
	return 0;
}

/*
 * print_triton_drive_flags() displays the currently programmed options
 * in the Triton chipset for a given drive.
 *
 *	If fastDMA  is "no", then slow ISA timings are used for DMA data xfers.
 *	If fastPIO  is "no", then slow ISA timings are used for PIO data xfers.
 *	If IORDY    is "no", then IORDY is assumed to always be asserted.
 *	If PreFetch is "no", then data pre-fetch/post are not used.
 *
 * When "fastPIO" and/or "fastDMA" are "yes", then faster PCI timings and
 * back-to-back 16-bit data transfers are enabled, using the sample_CLKs
 * and recovery_CLKs (PCI clock cycles) timing parameters for that interface.
 */
static void print_triton_drive_flags (unsigned int unit, byte flags)
{
	printk("         %s ", unit ? "slave :" : "master:");
	printk( "fastDMA=%s",	(flags&9)	? "on " : "off");
	printk(" PreFetch=%s",	(flags&4)	? "on " : "off");
	printk(" IORDY=%s",	(flags&2)	? "on " : "off");
	printk(" fastPIO=%s\n",	((flags&9)==1)	? "on " : "off");
}

/*
 * ide_init_triton() prepares the IDE driver for DMA operation.
 * This routine is called once, from ide.c during driver initialization,
 * for each triton chipset which is found (unlikely to be more than one).
 */
void ide_init_triton (byte bus, byte fn)
{
	int rc = 0, h;
	unsigned short bmiba, pcicmd;
	unsigned int timings;
	extern ide_hwif_t ide_hwifs[];

	++fn;	/* IDE interface is 2nd function on this device */
	/*
	 * See if IDE and BM-DMA features are enabled:
	 */
	if ((rc = pcibios_read_config_word(bus, fn, 0x04, &pcicmd)))
		goto quit;
	if ((pcicmd & 5) != 5) {
		if ((pcicmd & 1) == 0)
			printk("ide: Triton IDE ports are not enabled\n");
		else
			printk("ide: Triton BM-DMA feature is not enabled\n");
		goto quit;
	}
#if 0
	(void) pcibios_write_config_word(bus, fn, 0x42, 0x8037); /* for my MC2112A */
#endif
	/*
	 * See if ide port(s) are enabled
	 */
	if ((rc = pcibios_read_config_dword(bus, fn, 0x40, &timings)))
		goto quit;
	if (!(timings & 0x80008000)) {
		printk("ide: Triton IDE ports are not enabled\n");
		goto quit;
	}
	printk("ide: Triton BM-IDE on PCI bus %d function %d\n", bus, fn);

	/*
	 * Get the bmiba base address
	 */
	if ((rc = pcibios_read_config_word(bus, fn, 0x20, &bmiba)))
		goto quit;
	bmiba &= 0xfff0;	/* extract port base address */

	/*
	 * Save the dma_base port addr for each interface
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		ide_hwif_t *hwif = &ide_hwifs[h];
		unsigned short base, time;
		if (hwif->io_base == 0x1f0 && (timings & 0x8000)) {
			time = timings & 0xffff;
			base = bmiba;
		} else if (hwif->io_base == 0x170 && (timings & 0x80000000)) {
			time = timings >> 16;
			base = bmiba + 8;
		} else
			continue;
		printk("    %s: BusMaster DMA at 0x%04x-0x%04x", hwif->name, base, base+5);
		if (check_region(base, 6)) {
			printk(" -- ERROR, PORTS ALREADY IN USE");
		} else {
			unsigned long *table;
			request_region(base, 8, hwif->name);
			hwif->dma_base  = base;
			table = ide_alloc(2 * PRD_ENTRIES * sizeof(long), 4096);
			hwif->dmatable = table;
			outl((unsigned long) table, base + 4);
			hwif->dmaproc  = &triton_dmaproc;
		}
		printk("\n    %s timing: (0x%04x) sample_CLKs=%d, recovery_CLKs=%d\n",
		 hwif->name, time, ((~time>>12)&3)+2, ((~time>>8)&3)+1);
		print_triton_drive_flags (0, time & 0xf);
		print_triton_drive_flags (1, (time >> 4) & 0xf);
	}

quit: if (rc) printk("ide: pcibios access failed - %s\n", pcibios_strerror(rc));
}

