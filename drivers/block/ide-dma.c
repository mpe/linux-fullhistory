/*
 *  linux/drivers/block/ide-dma.c	Version 4.01  November 30, 1997
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA functions
 * of various PCI chipsets, including the Intel PIIX (i82371FB for
 * the 430 FX chipset), the PIIX3 (i82371SB for the 430 HX/VX and 
 * 440 chipsets), and the PIIX4 (i82371AB for the 430 TX chipset)
 * ("PIIX" stands for "PCI ISA IDE Xcellerator").
 *
 * Pretty much the same code works for other IDE PCI bus-mastering chipsets.
 *
 * DMA is supported for all IDE devices (disk drives, cdroms, tapes, floppies).
 *
 * By default, DMA support is prepared for use, but is currently enabled only
 * for drives which already have DMA enabled (UltraDMA or mode 2 multi/single),
 * or which are recognized as "good" (see table below).  Drives with only mode0
 * or mode1 (multi/single) DMA should also work with this chipset/driver
 * (eg. MC2112A) but are not enabled by default.
 *
 * Use "hdparm -i" to view modes supported by a given drive.
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
 * problem with some (all?) ACER motherboards/BIOSs.  Hopefully the fix
 * still works here (?).
 *
 * Thanks to "Benoit Poulot-Cazajous" <poulot@chorus.fr> for testing
 * "TX" chipset compatibility and for providing patches for the "TX" chipset.
 *
 * Thanks to Christian Brunner <chb@muc.de> for taking a good first crack
 * at generic DMA -- his patches were referred to when preparing this code.
 *
 * Most importantly, thanks to Robert Bringman <rob@mars.trion.com>
 * for supplying a Promise UDMA board & WD UDMA drive for this work!
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
#include <linux/init.h>
#include <linux/config.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ide.h"

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
 * two or more sectors, we could get by with a limit of 128 entries here for
 * the usual worst case.  Most requests seem to include some contiguous blocks,
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
 * ide_dmaproc() initiates/aborts DMA read/write operations on a drive.
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
static int ide_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	unsigned long dma_base = HWIF(drive)->dma_base;
	unsigned int reading = 0;

	switch (func) {
		case ide_dma_off:
			printk("%s: DMA disabled\n", drive->name);
		case ide_dma_off_quietly:
		case ide_dma_on:
			drive->using_dma = (func == ide_dma_on);
			return 0;
		case ide_dma_abort:
			outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
			return 0;
		case ide_dma_check:
			return config_drive_for_dma (drive);
		case ide_dma_status_bad:
			return ((inb(dma_base+2) & 7) != 4);	/* verify good DMA status */
		case ide_dma_transferred:
			return 0; /* NOT IMPLEMENTED: number of bytes actually transferred */
		case ide_dma_begin:
			outb(inb(dma_base)|1, dma_base);	/* begin DMA */
			return 0;
		default:
			printk("ide_dmaproc: unsupported func: %d\n", func);
			return 1;
		case ide_dma_read:
			reading = (1 << 3);
		case ide_dma_write:
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
}

static int config_drive_for_dma (ide_drive_t *drive)
{
	const char **list;

	struct hd_driveid *id = drive->id;
	if (id && (id->capability & 1)) {
		/* Enable DMA on any drive that has UltraDMA (mode 0/1/2) enabled */
		if (id->field_valid & 4)	/* UltraDMA */
			if  ((id->dma_ultra & (id->dma_ultra >> 8) & 7))
				return ide_dmaproc(ide_dma_on, drive);
		/* Enable DMA on any drive that has mode2 DMA (multi or single) enabled */
		if (id->field_valid & 2)	/* regular DMA */
			if  ((id->dma_mword & 0x404) == 0x404 || (id->dma_1word & 0x404) == 0x404)
				return ide_dmaproc(ide_dma_on, drive);
		/* Consult the list of known "good" drives */
		list = good_dma_drives;
		while (*list) {
			if (!strcmp(*list++,id->model))
				return ide_dmaproc(ide_dma_on, drive);
		}
	}
	return ide_dmaproc(ide_dma_off_quietly, drive);
}

#define DEVID_PIIX	(PCI_VENDOR_ID_INTEL  |(PCI_DEVICE_ID_INTEL_82371_1   <<16))
#define DEVID_PIIX3	(PCI_VENDOR_ID_INTEL  |(PCI_DEVICE_ID_INTEL_82371SB_1 <<16))
#define DEVID_PIIX4	(PCI_VENDOR_ID_INTEL  |(PCI_DEVICE_ID_INTEL_82371AB   <<16))
#define DEVID_VP_IDE 	(PCI_VENDOR_ID_VIA    |(PCI_DEVICE_ID_VIA_82C586_1    <<16))
#define DEVID_PDC2046	(PCI_VENDOR_ID_PROMISE|(PCI_DEVICE_ID_PROMISE_20246   <<16))
#define DEVID_RZ1000	(PCI_VENDOR_ID_PCTECH |(PCI_DEVICE_ID_PCTECH_RZ1000   <<16))
#define DEVID_RZ1001	(PCI_VENDOR_ID_PCTECH |(PCI_DEVICE_ID_PCTECH_RZ1001   <<16))
#define DEVID_CMD640	(PCI_VENDOR_ID_CMD    |(PCI_DEVICE_ID_CMD_640         <<16))
#define DEVID_CMD646	(PCI_VENDOR_ID_CMD    |(PCI_DEVICE_ID_CMD_646         <<16))
#define DEVID_SIS5513	(PCI_VENDOR_ID_SI     |(PCI_DEVICE_ID_SI_5513         <<16))
#define DEVID_OPTI	(PCI_VENDOR_ID_OPTI   |(PCI_DEVICE_ID_OPTI_82C621     <<16))
#define DEVID_OPTI2	(PCI_VENDOR_ID_OPTI   |(0xd568 /* from datasheets */  <<16))

#ifdef CONFIG_BLK_DEV_OPTI621
extern void ide_init_opti621(byte, byte, ide_hwif_t *);
#define INIT_OPTI (&ide_init_opti621)
#else
#define INIT_OPTI (NULL)
#endif

typedef struct ide_pci_enablebit_s {
	byte	reg;	/* byte pci reg holding the enable-bit */
	byte	mask;	/* mask to isolate the enable-bit */
	byte	val;	/* value of masked reg when "enabled" */
} ide_pci_enablebit_t;

typedef struct ide_pci_device_s {
	unsigned int		id;
	const char		*name;
	void 			(*init_hwif)(byte bus, byte fn, ide_hwif_t *hwif);
	ide_pci_enablebit_t	enablebits[2];
} ide_pci_device_t;

static ide_pci_device_t ide_pci_chipsets[] = {
	{DEVID_PIIX,	"PIIX",		NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_PIIX3,	"PIIX3",	NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_PIIX4,	"PIIX4",	NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_VP_IDE,	"VP_IDE",	NULL,		{{0x40,0x02,0x02}, {0x40,0x01,0x01}} },
	{DEVID_PDC2046,	"PDC2046",	NULL,		{{0x50,0x02,0x02}, {0x50,0x04,0x04}} },
	{DEVID_RZ1000,	NULL,		NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_RZ1001,	NULL,		NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_CMD640,	NULL,		NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_OPTI,	"OPTI",		INIT_OPTI,	{{0x45,0x80,0x00}, {0x40,0x08,0x00}} },
	{DEVID_OPTI2,	"OPTI2",	INIT_OPTI,	{{0x45,0x80,0x00}, {0x40,0x08,0x00}} },
	{DEVID_SIS5513,	"SIS5513",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_CMD646,	"CMD646",	NULL,		{{0x00,0x00,0x00}, {0x51,0x80,0x80}} },
	{0,		"PCI_IDE",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} }};

__initfunc(static ide_pci_device_t *lookup_devid(unsigned int devid))
{
	ide_pci_device_t *d = ide_pci_chipsets;
	while (d->id && d->id != devid)
		++d;
	return d;
}

__initfunc(static void ide_setup_dma (ide_hwif_t *hwif, unsigned short dmabase))
{
	static unsigned long dmatable = 0;
	static unsigned leftover = 0;

	printk("    %s: BM-DMA at 0x%04x-0x%04x", hwif->name, dmabase, dmabase+7);
	if (check_region(dmabase, 8)) {
		printk(" -- ERROR, PORTS ALREADY IN USE");
	} else {
		request_region(dmabase, 8, hwif->name);
		hwif->dma_base = dmabase;
		if (leftover < (PRD_ENTRIES * PRD_BYTES)) {
			/*
			 * The BM-DMA uses full 32bit addr, so we can
			 * safely use __get_free_page() here instead
			 * of __get_dma_pages() -- no ISA limitations.
			 */
			dmatable = __get_free_pages(GFP_KERNEL,1,0);
			leftover = dmatable ? PAGE_SIZE : 0;
		}
		if (dmatable) {
			printk(", PRD table at %08lx", dmatable);
			hwif->dmatable = (unsigned long *) dmatable;
			dmatable += (PRD_ENTRIES * PRD_BYTES);
			leftover -= (PRD_ENTRIES * PRD_BYTES);
			outl(virt_to_bus(hwif->dmatable), dmabase + 4);
			hwif->dmaproc = &ide_dmaproc;
		}
	}
	printk("\n");
}

/* The next two functions were stolen from cmd640.c, with
   a few modifications  */

__initfunc(static void write_pcicfg_dword (byte fn, unsigned short reg, long val))
{
  unsigned long flags;

  save_flags(flags);
  cli();
  outl_p((reg & 0xfc) | ((fn * 0x100) + 0x80000000), 0xcf8);
  outl_p(val, (reg & 3) | 0xcfc);
  restore_flags(flags);
}

__initfunc(static long read_pcicfg_dword (byte fn, unsigned short reg))
{
  long b;
  unsigned long flags;

  save_flags(flags);
  cli();
  outl_p((reg & 0xfc) | ((fn * 0x100) + 0x80000000), 0xcf8);
  b = inl_p((reg & 3) | 0xcfc);
  restore_flags(flags);
  return b;
}

/*
 * Search for an (apparently) unused block of I/O space
 * of "size" bytes in length.
 */
__initfunc(static short find_free_region (unsigned short size))
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
 * Fetch the Bus-Master I/O Base-Address (BMIBA) from PCI space:
 */
__initfunc(static unsigned int ide_get_or_set_bmiba (byte bus, byte fn, const char *name))
{
	unsigned int bmiba = 0;
	unsigned short base;
	int rc;

	if ((rc = pcibios_read_config_dword(bus, fn, 0x20, &bmiba))) {
		printk("%s: failed to read BMIBA\n", name);
	} else if ((bmiba &= 0xfff0) == 0) {
	        printk("%s: BMIBA is invalid (0x%04x, BIOS problem)\n", name, bmiba);
		base = find_free_region(16);
	        if (base) {
			printk("%s: setting BMIBA to 0x%04x\n", name, base);
			pcibios_write_config_dword(bus, fn, 0x20, base | 1);
			pcibios_read_config_dword(bus, fn, 0x20, &bmiba);
			bmiba &= 0xfff0;
			if (bmiba != base) {
				if (bus == 0) {
					printk("%s: operation failed, bypassing BIOS to try again\n", name);
					write_pcicfg_dword(fn, 0x20, base | 1);
					bmiba = read_pcicfg_dword(fn, 0x20) & 0xfff0;
				}
				if (bmiba != base) {
					printk("%s: operation failed, DMA disabled\n", name);
					bmiba = 0;
				}
			}
		}
	}
	return bmiba;
}

/*
 * Match a PCI IDE port against an entry in ide_hwifs[],
 * based on io_base port if possible.
 */
__initfunc(ide_hwif_t *ide_match_hwif (unsigned int io_base))
{
	int h;
	ide_hwif_t *hwif;

	/*
	 * Look for a hwif with matching io_base specified using
	 * parameters to ide_setup().
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (hwif->chipset == ide_generic)
				return hwif; /* a perfect match */
		}
	}
	/*
	 * Look for a hwif with matching io_base default value.
	 * If chipset is "ide_unknown", then claim that hwif slot.
	 * Otherwise, some other chipset has already claimed it..  :(
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		hwif = &ide_hwifs[h];
		if (hwif->io_ports[IDE_DATA_OFFSET] == io_base) {
			if (hwif->chipset == ide_unknown)
				return hwif; /* match */
			return NULL;	/* already claimed */
		}
	}
	/*
	 * Okay, there is no hwif matching our io_base,
	 * so we'll just claim an unassigned slot.
	 * Give preference to claiming ide2/ide3 before ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned
	 * which uses ports 1f0/170 (the ide0/ide1 defaults).
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		int hwifs[] = {2,3,1,0}; /* assign 3rd/4th before 1st/2nd */
		hwif = &ide_hwifs[hwifs[h]];
		if (hwif->chipset == ide_unknown)
			return hwif;	/* pick an unused entry */
	}
	return NULL;
}

/*
 * ide_setup_pci_device() looks at the primary/secondary interfaces
 * on a PCI IDE device and, if they are enabled, prepares the IDE driver
 * for use with them.  This generic code works for most PCI chipsets.
 *
 * One thing that is not standardized is the location of the
 * primary/secondary interface "enable/disable" bits.  For chipsets that
 * we "know" about, this information is in the ide_pci_device_t struct;
 * for all other chipsets, we just assume both interfaces are enabled.
 */
__initfunc(static void ide_setup_pci_device (byte bus, byte fn, unsigned int bmiba, ide_pci_device_t *d))
{
	unsigned int port, at_least_one_hwif_enabled = 0;
	unsigned short base = 0, ctl = 0;
	byte tmp = 0, pciirq = 0;
	ide_hwif_t *hwif;

	if (pcibios_read_config_byte(bus, fn, 0x3c, &pciirq))
		pciirq = 0;	/* probe later if not set */
	for (port = 0; port <= 1; ++port) {
		ide_pci_enablebit_t *e = &(d->enablebits[port]);
		if (e->reg) {
			if (pcibios_read_config_byte(bus, fn, e->reg, &tmp)) {
				printk("%s: unable to read pci reg 0x%x\n", d->name, e->reg);
			} else if ((tmp & e->mask) != e->val)
				continue;	/* port not enabled */
		}
		if (pcibios_read_config_word(bus, fn, 0x14+(port*8), &ctl))
			ctl = 0;
		if ((ctl &= 0xfffc) == 0)
			ctl = 0x3f4 ^ (port << 7);
		if (pcibios_read_config_word(bus, fn, 0x10+(port*8), &base))
			base = 0;
		if ((base &= 0xfff8) == 0)
			base = 0x1F0 ^ (port << 7);
		if ((hwif = ide_match_hwif(base)) == NULL) {
			printk("%s: no room in hwif table for port %d\n", d->name, port);
			continue;
		}
		hwif->chipset = ide_pci;
		if (hwif->io_ports[IDE_DATA_OFFSET] != base) {
			ide_init_hwif_ports(hwif->io_ports, base, NULL);
			hwif->io_ports[IDE_CONTROL_OFFSET] = ctl + 2;
		}
		if (!hwif->irq)
			hwif->irq = port ? 0 : pciirq;	/* always probe for secondary irq */
		if (bmiba) {
			if ((inb(bmiba+2) & 0x80)) {	/* simplex DMA only? */
				printk("%s: simplex device:  DMA disabled\n", d->name);
			} else {	/* supports simultaneous DMA on both channels */
				ide_setup_dma(hwif, bmiba + (8 * port));
			}
		}
		if (d->init_hwif)  /* Call chipset-specific routine for each enabled hwif */
			d->init_hwif(bus, fn, hwif);
		at_least_one_hwif_enabled = 1;
	}
	if (!at_least_one_hwif_enabled)
		printk("%s: neither IDE port is enabled\n", d->name);
}

/*
 * ide_scan_pci_device() examines all functions of a PCI device,
 * looking for IDE interfaces and/or devices in ide_pci_chipsets[].
 */
__initfunc(static inline void ide_scan_pci_device (unsigned int bus, unsigned int fn))
{
	unsigned int devid, ccode;
	unsigned short pcicmd;
	ide_pci_device_t *d;
	byte hedt;

	if (pcibios_read_config_byte(bus, fn, 0x0e, &hedt))
		hedt = 0;
	do {
		if (pcibios_read_config_dword(bus, fn, 0x00, &devid)
		 || devid == 0xffffffff
		 || pcibios_read_config_dword(bus, fn, 0x08, &ccode))
			return;
		d = lookup_devid(devid);
		if (d->name == NULL)	/* some chips (cmd640 & rz1000) are handled elsewhere */
			continue;
		if (d->id || (ccode >> 16) == PCI_CLASS_STORAGE_IDE) {
			printk("%s: %sIDE device on PCI bus %d function %d\n", d->name, d->id ? "" : "unknown ", bus, fn);
			/*
	 		* See if IDE ports are enabled
	 		*/
			if (pcibios_read_config_word(bus, fn, 0x04, &pcicmd)) {
				printk("%s: error accessing PCICMD\n", d->name);
			} else if ((pcicmd & 1) == 0) {
				printk("%s: device is disabled (BIOS)\n", d->name);
			} else {
				unsigned int bmiba = 0;
				/*
		 		 * Check for Bus-Master DMA capability
		 		 */
				if (!(pcicmd & 4) || !(bmiba = ide_get_or_set_bmiba(bus, fn, d->name))) {
					if ((ccode >> 16) == PCI_CLASS_STORAGE_RAID || (ccode && 0x8000))
						printk("%s: Bus-Master DMA is disabled (BIOS)\n", d->name);
				}
				ide_setup_pci_device(bus, fn, bmiba, d);
			}
		}
	} while (hedt == 0x80 && (++fn & 7));
}

/*
 * ide_scan_pcibus() gets invoked at boot time from ide.c
 */
__initfunc(void ide_scan_pcibus (void))
{
	unsigned int bus, dev;

	if (!pcibios_present())
		return;
	for (bus = 0; bus <= 255; ++bus) {
		for (dev = 0; dev <= 31; ++dev) {
			ide_scan_pci_device(bus, dev << 3);
		}
	}
}

