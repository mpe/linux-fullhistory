/*
 *  linux/drivers/block/triton.c	Version 2.00  March 9, 1997
 *
 *  Copyright (c) 1995-1997  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA function
 * of the Intel PCI Triton chipset families, which use the PIIX (i82371FB,
 * for the 430 FX chipset), and the enhanced PIIX3 (i82371SB for the 430 HX/VX
 * and 440 chipsets).
 *
 * "PIIX" stands for "PCI ISA IDE Xcellerator".
 *
 * Pretty much the same code could work for other IDE PCI bus-mastering chipsets.
 * Look for DMA support for this someday in the not too distant future.
 *
 * DMA is supported for all IDE devices (disk drives, cdroms, tapes, floppies).
 *
 * Up to four drives may be enabled for DMA, and the PIIX/PIIX3 chips
 * will arbitrate the PCI bus among them.  Note that the PIIX/PIIX3
 * provides a single "line buffer" for the BM IDE function, so performance of
 * multiple (two) drives doing DMA simultaneously will suffer somewhat,
 * as they contest for that resource bottleneck.  This is handled transparently
 * inside the PIIX/PIIX3.
 *
 * By default, DMA support is prepared for use, but is currently enabled only
 * for drives which support DMA mode2 (multi/single word), or which are
 * recognized as "good" (see table below).  Drives with only mode0 or mode1
 * (multi/single word) DMA should also work with this chipset/driver (eg. MC2112A)
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
 * Testing has been done with a rather extensive number of drives,
 * with Quantum & Western Digital models generally outperforming the pack,
 * and Fujitsu & Conner (and some Seagate which are really Conner) drives
 * showing more lackluster throughput.
 *
 * Keep an eye on /var/adm/messages for "DMA disabled" messages.
 *
 * Some people have reported trouble with Intel Zappa motherboards.
 * This can be fixed by upgrading the AMI BIOS to version 1.00.04.BS0,
 * available from ftp://ftp.intel.com/pub/bios/10004bs0.exe
 * (thanks to Glen Morrell <glen@spin.Stanford.edu> for researching this).
 *
 * Thanks to "Christopher J. Reimer" <reimer@doe.carleton.ca> for fixing the
 * problem with some (all?) ACER motherboards/BIOSs.
 *
 * And, yes, Intel Zappa boards really *do* use both PIIX IDE ports.
 */
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
#include <asm/dma.h>

#include "ide.h"
#include "ide_modes.h"

#define DISPLAY_PIIX_TIMINGS	/* define this to display timings */

/*
 * good_dma_drives() lists the model names (from "hdparm -i")
 * of drives which do not support mode2 DMA but which are
 * known to work fine with this interface under Linux.
 */
const char *good_dma_drives[] = {"Micropolis 2112A",
				 "CONNER CTMA 4000",
				 NULL};

/*
 * Our Physical Region Descriptor (PRD) table should be large enough
 * to handle the biggest I/O request we are likely to see.  Since requests
 * can have no more than 256 sectors, and since the typical blocksize is
 * two sectors, we could get by with a limit of 128 entries here for the
 * usual worst case.  Most requests seem to include some contiguous blocks,
 * further reducing the number of table entries required.
 *
 * The driver reverts to PIO mode for individual requests that exceed
 * this limit (possible with 512 byte blocksizes, eg. MSDOS f/s), so handling
 * 100% of all crazy scenarios here is not necessary.
 *
 * As it turns out though, we must allocate a full 4KB page for this,
 * so the two PRD tables (ide0 & ide1) will each get half of that,
 * allowing each to have about 256 entries (8 bytes each) from this.
 */
#define PRD_BYTES	8
#define PRD_ENTRIES	(PAGE_SIZE / (2 * PRD_BYTES))

/*
 * Interface to access piix registers
 */
static unsigned int piix_key;

#define PIIX_FLAGS_FAST_PIO	1
#define PIIX_FLAGS_USE_IORDY	2
#define PIIX_FLAGS_PREFETCH	4
#define PIIX_FLAGS_FAST_DMA	8

typedef struct {
	unsigned d0_flags	:4;
	unsigned d1_flags	:4;
	unsigned recovery	:2;
	unsigned reserved	:2;
	unsigned sample		:2;
	unsigned sidetim_enabled:1;
	unsigned ports_enabled	:1;
} piix_timing_t;

typedef struct {
	unsigned pri_recovery	:2;
	unsigned pri_sample	:2;
	unsigned sec_recovery	:2;
	unsigned sec_sample	:2;
} piix_sidetim_t;


/*
 * We currently can handle only one PIIX chip here
 */
static piix_pci_bus = 0;
static piix_pci_fn  = 0;

static int config_drive_for_dma (ide_drive_t *);

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
			return;
		}
		printk("%s: bad DMA status: 0x%02x\n", drive->name, dma_stat);
	}
	sti();
	ide_error(drive, "dma_intr", stat);
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
				*table++ = bcount & 0xffff;
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
 * piix_dmaproc() initiates/aborts DMA read/write operations on a drive.
 *
 * The caller is assumed to have selected the drive and programmed the drive's
 * sector address using CHS or LBA.  All that remains is to prepare for DMA
 * and then issue the actual read/write DMA/PIO command to the drive.
 *
 * For ATAPI devices, we just prepare for DMA and return. The caller should
 * then issue the packet command to the drive and call us again with
 * ide_dma_begin afterwards.
 *
 * Returns 0 if all went well.
 * Returns 1 if DMA read/write could not be started, in which case
 * the caller should revert to PIO for the current request.
 */
static int piix_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	unsigned long dma_base = HWIF(drive)->dma_base;
	unsigned int reading = (1 << 3);
	piix_timing_t timing;
	unsigned short reg;
	byte dflags;

	switch (func) {
		case ide_dma_off:
			printk("%s: DMA disabled\n", drive->name);
		case ide_dma_on:
			drive->using_dma = (func == ide_dma_on);
			reg = (HWIF(drive)->io_ports[IDE_DATA_OFFSET] == 0x170) ? 0x42 : 0x40;
			if (pcibios_read_config_word(piix_pci_bus, piix_pci_fn, reg, (short *)&timing)) {
				printk("%s: pcibios read failed\n", HWIF(drive)->name);
				return 1;
			}
			dflags = drive->select.b.unit ? timing.d1_flags : timing.d0_flags;
			if (dflags & PIIX_FLAGS_FAST_PIO) {
				if (func == ide_dma_on && drive->media == ide_disk)
					dflags |= PIIX_FLAGS_FAST_DMA;
				else
					dflags &= ~PIIX_FLAGS_FAST_DMA;
				if (drive->select.b.unit == 0)
					timing.d0_flags = dflags;
				else
					timing.d1_flags = dflags;
				if (pcibios_write_config_word(piix_pci_bus, piix_pci_fn, reg, *(short *)&timing)) {
					printk("%s: pcibios write failed\n", HWIF(drive)->name);
					return 1;
				}
			}
			return 0;
		case ide_dma_abort:
			outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
			return 0;
		case ide_dma_check:
			return config_drive_for_dma (drive);
		case ide_dma_write:
			reading = 0;
		case ide_dma_read:
			break;
		case ide_dma_status_bad:
			return ((inb(dma_base+2) & 7) != 4);	/* verify good DMA status */
		case ide_dma_transferred:
#if 0
			return (number of bytes actually transferred);
#else
			return (0);
#endif
		case ide_dma_begin:
			outb(inb(dma_base)|1, dma_base);	/* begin DMA */
			return 0;
		default:
			printk("piix_dmaproc: unsupported func: %d\n", func);
			return 1;
	}
	if (build_dmatable (drive))
		return 1;
	outl(virt_to_bus (HWIF(drive)->dmatable), dma_base + 4); /* PRD table */
	outb(reading, dma_base);			/* specify r/w */
	outb(inb(dma_base+2)|0x06, dma_base+2);		/* clear status bits */
	if (drive->media != ide_disk)
		return 0;
	ide_set_handler(drive, &dma_intr, WAIT_CMD);	/* issue cmd to drive */
	OUT_BYTE(reading ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);
	outb(inb(dma_base)|1, dma_base);		/* begin DMA */
	return 0;
}

static int config_drive_for_dma (ide_drive_t *drive)
{
	const char **list;

	struct hd_driveid *id = drive->id;
	if (id && (id->capability & 1)) {
		/* Enable DMA on any drive that supports mode2 (multi/single word) DMA */
		if (id->field_valid & 2)
			if  ((id->dma_mword & 0x404) == 0x404 || (id->dma_1word & 0x404) == 0x404)
				return piix_dmaproc(ide_dma_on, drive);
		/* Consult the list of known "good" drives */
		list = good_dma_drives;
		while (*list) {
			if (!strcmp(*list++,id->model))
				return piix_dmaproc(ide_dma_on, drive);
		}
	}
	return piix_dmaproc(ide_dma_off, drive);
}

#ifdef DISPLAY_PIIX_TIMINGS
/*
 * print_piix_drive_flags() displays the currently programmed options
 * in the PIIX/PIIX3 for a given drive.
 */
static void print_piix_drive_flags (const char *unit, byte dflags)
{
	printk("         %s ", unit);
	printk( "fastDMA=%s",	(dflags & PIIX_FLAGS_FAST_PIO)	? "yes" : "no ");
	printk(" PreFetch=%s",	(dflags & PIIX_FLAGS_PREFETCH)	? "on " : "off");
	printk(" IORDY=%s",	(dflags & PIIX_FLAGS_USE_IORDY)	? "on " : "off");
	printk(" fastPIO=%s\n",	((dflags & (PIIX_FLAGS_FAST_PIO|PIIX_FLAGS_FAST_DMA)) == PIIX_FLAGS_FAST_PIO) ? "on " : "off");
}
#endif /* DISPLAY_PIIX_TIMINGS */

static void init_piix_dma (ide_hwif_t *hwif, unsigned short base)
{
	static unsigned long dmatable = 0;

	printk("    %s: BM-DMA at 0x%04x-0x%04x", hwif->name, base, base+7);
	if (check_region(base, 8)) {
		printk(" -- ERROR, PORTS ALREADY IN USE");
	} else {
		request_region(base, 8, "IDE DMA");
		hwif->dma_base = base;
		if (!dmatable) {
			/*
			 * The BM-DMA uses a full 32-bits, so we can
			 * safely use __get_free_page() here instead
			 * of __get_dma_pages() -- no ISA limitations.
			 */
			dmatable = __get_free_page(GFP_KERNEL);
		}
		if (dmatable) {
			hwif->dmatable = (unsigned long *) dmatable;
			dmatable += (PRD_ENTRIES * PRD_BYTES);
			outl(virt_to_bus(hwif->dmatable), base + 4);
			hwif->dmaproc  = &piix_dmaproc;
		}
	}
	printk("\n");
}

/* The next two functions were stolen from cmd640.c, with
   a few modifications  */

static void put_piix_reg (unsigned short reg, long val)
{
  unsigned long flags;

  save_flags(flags);
  cli();
  outl_p((reg & 0xfc) | piix_key, 0xcf8);
  outl_p(val, (reg & 3) | 0xcfc);
  restore_flags(flags);
}

static long get_piix_reg (unsigned short reg)
{
  long b;
  unsigned long flags;

  save_flags(flags);
  cli();
  outl_p((reg & 0xfc) | piix_key, 0xcf8);
  b = inl_p((reg & 3) | 0xcfc);
  restore_flags(flags);
  return b;
}

/*
 * Search for an (apparently) unused block of I/O space
 * of "size" bytes in length.
 */
static short find_free_region (unsigned short size)
{
	unsigned short i, base = 0xe800;
	for (base = 0xe800; base > 0; base -= 0x800) {
		if (!check_region(base,size)) {
			for (i = 0; i < size; i++) {
				if (inb(base+i) != 0xff)
					goto next;
			}
			return base;	/* success */
		}
	next:
	}
	return 0;	/* failure */
}

/*
 * ide_init_triton() prepares the IDE driver for DMA operation.
 * This routine is called once, from ide.c during driver initialization,
 * for each triton chipset which is found (unlikely to be more than one).
 */
void ide_init_triton (byte bus, byte fn)
{
	int rc = 0, h;
	int dma_enabled = 0;
	unsigned short pcicmd, devid;
	unsigned int bmiba;
	const char *chipset = "ide";
	piix_timing_t timings[2];

	if (pcibios_read_config_word(piix_pci_bus, piix_pci_fn, 0x02, &devid))
		goto quit;
	chipset = (devid == PCI_DEVICE_ID_INTEL_82371SB_1) ? "PIIX3" : "PIIX";

	printk("%s: bus-master IDE device on PCI bus %d function %d\n", chipset, bus, fn);

	/*
	 * See if IDE ports are enabled
	 */
	if ((rc = pcibios_read_config_word(bus, fn, 0x04, &pcicmd)))
		goto quit;
	if ((pcicmd & 1) == 0)  {
		printk("%s: IDE ports are not enabled (BIOS)\n", chipset);
		goto quit;
	}
	if ((rc = pcibios_read_config_word(bus, fn, 0x40, (short *)&timings[0])))
		goto quit;
	if ((rc = pcibios_read_config_word(bus, fn, 0x42, (short *)&timings[1])))
		goto quit;
	if ((!timings[0].ports_enabled) || (!timings[1].ports_enabled)) {
		printk("%s: neither IDE port is enabled\n", chipset);
		goto quit;
	}

	piix_pci_bus = bus;
	piix_pci_fn  = fn;

	/*
	 * See if Bus-Mastered DMA is enabled
	 */
	if ((pcicmd & 4) == 0) {
		printk("%s: bus-master DMA feature is not enabled (BIOS)\n", chipset);
	} else {
		/*
		 * Get the bmiba base address
		 */
		if ((rc = pcibios_read_config_dword(bus, fn, 0x20, &bmiba)))
			goto quit;
		bmiba &= 0xfff0;	/* extract port base address */
		if (bmiba) {
			dma_enabled = 1;
		} else {
			unsigned short base;
		        printk("%s: bus-master base address is invalid (0x%04x, BIOS problem)\n", chipset, bmiba);
			base = find_free_region(16);
		        if (base) {
				printk("%s: bypassing BIOS; setting bus-master base address to 0x%04x\n", chipset, base);
				piix_key = 0x80000000 + (fn * 0x100);
				put_piix_reg(0x04,get_piix_reg(0x04)&~5);
				put_piix_reg(0x20,(get_piix_reg(0x20)&0xFFFF000F)|base|1);
				put_piix_reg(0x04,get_piix_reg(0x04)|5);
				bmiba = get_piix_reg(0x20)&0x0000FFF0;
				if (bmiba == base && (get_piix_reg(0x04) & 5) == 5)
					dma_enabled = 1;
				else
					printk("%s: operation failed\n", chipset);
			}
			if (!dma_enabled)
				printk("%s: DMA is disabled (BIOS)\n", chipset);
		}
	}

	/*
	 * Save the dma_base port addr for each interface
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		unsigned int pri_sec;
		piix_timing_t timing;
		ide_hwif_t *hwif = &ide_hwifs[h];
		switch (hwif->io_ports[IDE_DATA_OFFSET]) {
			case 0x1f0:	pri_sec = 0; break;
			case 0x170:	pri_sec = 1; break;
			default:	continue;
		}
		timing = timings[pri_sec];
		if (!timing.ports_enabled)	/* interface disabled? */
			continue;
		hwif->chipset = ide_triton;
		if (dma_enabled)
			init_piix_dma(hwif, bmiba + (pri_sec ? 8 : 0));
#ifdef DISPLAY_PIIX_TIMINGS
		/*
		 * Display drive timings/modes
		 */
		{
			const char *slave;
			piix_sidetim_t sidetim;
			byte sample   = 5 - timing.sample;
			byte recovery = 4 - timing.recovery;
			if (devid == PCI_DEVICE_ID_INTEL_82371SB_1
			 && timing.sidetim_enabled
			 && !pcibios_read_config_byte(piix_pci_bus, piix_pci_fn, 0x44, (byte *) &sidetim))
				slave = "";		/* PIIX3 */
			else
				slave = "/slave";	/* PIIX, or PIIX3 in compatibility mode */
			printk("    %s master%s: sample_CLKs=%d, recovery_CLKs=%d\n", hwif->name, slave, sample, recovery);
			print_piix_drive_flags ("master:", timing.d0_flags);
			if (!*slave) {
				if (pri_sec == 0) {
					sample   = 5 - sidetim.pri_sample;
					recovery = 4 - sidetim.pri_recovery;
				} else {
					sample   = 5 - sidetim.sec_sample;
					recovery = 4 - sidetim.sec_recovery;
				}
				printk("         slave : sample_CLKs=%d, recovery_CLKs=%d\n", sample, recovery);
			}
			print_piix_drive_flags ("slave :", timing.d1_flags);
		}
#endif /* DISPLAY_PIIX_TIMINGS */
	}

quit: if (rc) printk("%s: pcibios access failed - %s\n", chipset, pcibios_strerror(rc));
}
