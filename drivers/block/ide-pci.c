/*
 *  linux/drivers/block/ide-pci.c	Version 1.02  December 29, 1997
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 *  This module provides support for automatic detection and
 *  configuration of all PCI IDE interfaces present in a system.  
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bios32.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide.h"

#define DEVID_PIIXa	((ide_pci_devid_t){PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_82371FB_0})
#define DEVID_PIIXb	((ide_pci_devid_t){PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_82371FB_1})
#define DEVID_PIIX3	((ide_pci_devid_t){PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_82371SB_1})
#define DEVID_PIIX4	((ide_pci_devid_t){PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_82371AB})
#define DEVID_VP_IDE	((ide_pci_devid_t){PCI_VENDOR_ID_VIA,     PCI_DEVICE_ID_VIA_82C586_1})
#define DEVID_PDC20246	((ide_pci_devid_t){PCI_VENDOR_ID_PROMISE, PCI_DEVICE_ID_PROMISE_20246})
#define DEVID_RZ1000	((ide_pci_devid_t){PCI_VENDOR_ID_PCTECH,  PCI_DEVICE_ID_PCTECH_RZ1000})
#define DEVID_RZ1001	((ide_pci_devid_t){PCI_VENDOR_ID_PCTECH,  PCI_DEVICE_ID_PCTECH_RZ1001})
#define DEVID_CMD640	((ide_pci_devid_t){PCI_VENDOR_ID_CMD,     PCI_DEVICE_ID_CMD_640})
#define DEVID_CMD646	((ide_pci_devid_t){PCI_VENDOR_ID_CMD,     PCI_DEVICE_ID_CMD_646})
#define DEVID_SIS5513	((ide_pci_devid_t){PCI_VENDOR_ID_SI,      PCI_DEVICE_ID_SI_5513})
#define DEVID_OPTI621	((ide_pci_devid_t){PCI_VENDOR_ID_OPTI,    PCI_DEVICE_ID_OPTI_82C621})
#define DEVID_OPTI621V	((ide_pci_devid_t){PCI_VENDOR_ID_OPTI,    PCI_DEVICE_ID_OPTI_82C558})
#define DEVID_OPTI621X	((ide_pci_devid_t){PCI_VENDOR_ID_OPTI,    0xd568})  /* from datasheets */
#define DEVID_TRM290	((ide_pci_devid_t){PCI_VENDOR_ID_TEKRAM,  PCI_DEVICE_ID_TEKRAM_DC290})
#define DEVID_NS87410	((ide_pci_devid_t){PCI_VENDOR_ID_NS,      PCI_DEVICE_ID_NS_87410})
#define DEVID_NS87415	((ide_pci_devid_t){PCI_VENDOR_ID_NS,      PCI_DEVICE_ID_NS_87415})
#define DEVID_HT6565	((ide_pci_devid_t){PCI_VENDOR_ID_HOLTEK,  PCI_DEVICE_ID_HOLTEK_6565})
#define DEVID_AEC6210	((ide_pci_devid_t){0x1191,                0x0005})

#define IDE_IGNORE	((void *)-1)

#ifdef CONFIG_BLK_DEV_TRM290
extern void ide_init_trm290(ide_hwif_t *);
#define INIT_TRM290	&ide_init_trm290
#else
#define INIT_TRM290	IDE_IGNORE
#endif

#ifdef CONFIG_BLK_DEV_OPTI621
extern void ide_init_opti621(ide_hwif_t *);
#define INIT_OPTI621	&ide_init_opti621
#else
#define INIT_OPTI621	NULL
#endif

#ifdef CONFIG_BLK_DEV_NS87415
extern void ide_init_ns87415(ide_hwif_t *);
#define INIT_NS87415	&ide_init_ns87415
#else
#define INIT_NS87415	IDE_IGNORE
#endif

#ifdef CONFIG_BLK_DEV_RZ1000
extern void ide_init_rz1000(ide_hwif_t *);
#define INIT_RZ1000	&ide_init_rz1000
#else
#define INIT_RZ1000	IDE_IGNORE
#endif

typedef struct ide_pci_enablebit_s {
	byte	reg;	/* byte pci reg holding the enable-bit */
	byte	mask;	/* mask to isolate the enable-bit */
	byte	val;	/* value of masked reg when "enabled" */
} ide_pci_enablebit_t;

typedef struct ide_pci_device_s {
	ide_pci_devid_t		devid;
	const char		*name;
	void 			(*init_hwif)(ide_hwif_t *hwif);
	ide_pci_enablebit_t	enablebits[2];
} ide_pci_device_t;

static ide_pci_device_t ide_pci_chipsets[] __initdata = {
	{DEVID_PIIXa,	"PIIX",		NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_PIIXb,	"PIIX",		NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_PIIX3,	"PIIX3",	NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_PIIX4,	"PIIX4",	NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}} },
	{DEVID_VP_IDE,	"VP_IDE",	NULL,		{{0x40,0x02,0x02}, {0x40,0x01,0x01}} },
	{DEVID_PDC20246,"PDC20246",	NULL,		{{0x50,0x02,0x02}, {0x50,0x04,0x04}} },
	{DEVID_RZ1000,	"RZ1000",	INIT_RZ1000,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_RZ1001,	"RZ1001",	INIT_RZ1000,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_CMD640,	"CMD640",	IDE_IGNORE,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_NS87410,	"NS87410",	NULL,		{{0x43,0x08,0x08}, {0x47,0x08,0x08}} },
	{DEVID_SIS5513,	"SIS5513",	NULL,		{{0x4a,0x02,0x02}, {0x4a,0x04,0x04}} },
	{DEVID_CMD646,	"CMD646",	NULL,		{{0x00,0x00,0x00}, {0x51,0x80,0x80}} },
	{DEVID_HT6565,	"HT6565",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_OPTI621,	"OPTI621",	INIT_OPTI621,	{{0x45,0x80,0x00}, {0x40,0x08,0x00}} },
	{DEVID_OPTI621X,"OPTI621X",	INIT_OPTI621,	{{0x45,0x80,0x00}, {0x40,0x08,0x00}} },
	{DEVID_TRM290,	"TRM290",	INIT_TRM290,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_NS87415,	"NS87415",	INIT_NS87415,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{DEVID_AEC6210,	"AEC6210",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} },
	{IDE_PCI_DEVID_NULL, "PCI_IDE",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}} }};

/*
 * Search for an (apparently) unused block of I/O space
 * of "size" bytes in length.  Ideally we ought to do a pass
 * through pcicfg space to eliminate ports already allocated
 * by the BIOS, to avoid conflicts later in the init cycle,
 * but we don't.	FIXME
 */
unsigned long ide_find_free_region (unsigned short size) /* __init */
{
	static unsigned short base = 0x5800;	/* it works for me */
	unsigned short i;

	for (; base > 0; base -= 0x200) {
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
 * Match a PCI IDE port against an entry in ide_hwifs[],
 * based on io_base port if possible.
 */
__initfunc(static ide_hwif_t *ide_match_hwif (unsigned int io_base, const char *name))
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
			printk("%s: port 0x%04x already claimed by %s\n", name, io_base, hwif->name);
			return NULL;	/* already claimed */
		}
	}
	/*
	 * Okay, there is no hwif matching our io_base,
	 * so we'll just claim an unassigned slot.
	 * Give preference to claiming other slots before claiming ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned
	 * which uses ports 1f0/170 (the ide0/ide1 defaults).
	 */
	for (h = 0; h < MAX_HWIFS; ++h) {
		int hwifs[] = {2,3,1,0}; /* assign 3rd/4th before 1st/2nd */
		hwif = &ide_hwifs[hwifs[h]];
		if (hwif->chipset == ide_unknown)
			return hwif;	/* pick an unused entry */
	}
	printk("%s: too many IDE interfaces, no room in table\n", name);
	return NULL;
}

__initfunc(static int ide_setup_pci_baseregs (byte bus, byte fn, const char *name))
{
	unsigned int base, readback;
	byte reg, progif = 0;

	/*
	 * Place both IDE interfaces into PCI "native" mode:
	 */
	if (pcibios_read_config_byte(bus, fn, 0x09, &progif) || (progif & 5) != 5) {
		if ((progif & 0xa) != 0xa) {
			printk("%s: device not capable of full native PCI mode\n", name);
			return 1;
		}
		printk("%s: placing both ports into native PCI mode\n", name);
		(void) pcibios_write_config_byte(bus, fn, 0x09, progif|5);
		if (pcibios_read_config_byte(bus, fn, 0x09, &progif) || (progif & 5) != 5) {
			printk("%s: rewrite of PROGIF failed, wanted 0x%04x, got 0x%04x\n", name, progif|5, progif);
			return 1;
		}
	}
	/*
	 * Setup base registers for IDE command/control spaces for each interface:
	 */
	if (!(base = ide_find_free_region(32)))
		return 1;
	for (reg = 0x10; reg <= 0x1c; reg += 4, base += 8) {
		(void) pcibios_write_config_dword(bus, fn, reg, base|1);
		if (pcibios_read_config_dword(bus, fn, reg, &readback) || (readback &= ~1) != base) {
			printk("%s: readback failed for basereg 0x%02x: wrote 0x%04x, read 0x%x04\n", name, reg, base, readback);
			return 1;
		}
	}
	return 0;
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
__initfunc(static void ide_setup_pci_device (byte bus, byte fn, unsigned int ccode, ide_pci_device_t *d))
{
	unsigned int port, at_least_one_hwif_enabled = 0, no_autodma = 0;
	unsigned short pcicmd = 0, tried_config = 0;
	byte tmp = 0, progif = 0, pciirq = 0;
	ide_hwif_t *hwif, *mate = NULL;

check_if_enabled:
	if (pcibios_read_config_word(bus, fn, 0x04, &pcicmd)
	 || pcibios_read_config_byte(bus, fn, 0x09, &progif)
	 || pcibios_read_config_byte(bus, fn, 0x3c, &pciirq))
	{
		printk("%s: error accessing PCI regs\n", d->name);
		return;
	}
	if (!(pcicmd & 1)) {	/* is device disabled? */
		/*
		 * PnP BIOS was *supposed* to have set this device up for us,
		 * but we can do it ourselves, so long as the BIOS has assigned an IRQ
		 *  (or possibly the device is using a "legacy header" for IRQs).
		 * Maybe the user deliberately *disabled* the device,
		 * but we'll eventually ignore it again if no drives respond.
		 */
		if (tried_config++
		 || ide_setup_pci_baseregs(bus, fn, d->name)
		 || pcibios_write_config_word(bus, fn, 0x04, pcicmd|1))
		{
			printk("%s: device disabled (BIOS)\n", d->name);
			return;
		}
		no_autodma = 1;	/* default DMA off if we had to configure it here */
		goto check_if_enabled;
	}
	if (tried_config)
		printk("%s: device enabled (Linux)\n", d->name);
	/*
	 * Can we trust the reported IRQ?
	 */
	if ((ccode >> 16) != PCI_CLASS_STORAGE_IDE || (progif & 5) != 5) {
		printk("%s: not 100%% native mode: will probe irqs later\n", d->name);
		pciirq = 0;
	} else if (tried_config) {
		printk("%s: will probe irqs later\n", d->name);
		pciirq = 0;
	} else if (!pciirq || pciirq >= NR_IRQS) {
		printk("%s: bad irq from BIOS (%d): will probe later\n", d->name, pciirq);
		pciirq = 0;
	} else {
		printk("%s: 100%% native mode on irq %d\n", d->name, pciirq);
	}
	/*
	 * Set up the IDE ports
	 */
	for (port = 0; port <= 1; ++port) {
		unsigned int base = 0, ctl = 0;
		ide_pci_enablebit_t *e = &(d->enablebits[port]);
		if (e->reg && (pcibios_read_config_byte(bus, fn, e->reg, &tmp) || (tmp & e->mask) != e->val))
			continue;	/* port not enabled */
		if (pcibios_read_config_dword(bus, fn, 0x14+(port*8), &ctl) || (ctl &= ~3) == 0)
			ctl = port ? 0x374 : 0x3f4;	/* use default value */
		if (pcibios_read_config_dword(bus, fn, 0x10+(port*8), &base) || (base &= ~7) == 0)
			base = port ? 0x170 : 0x1f0;	/* use default value */
		if ((hwif = ide_match_hwif(base, d->name)) == NULL)
			continue;	/* no room in ide_hwifs[] */
		if (hwif->io_ports[IDE_DATA_OFFSET] != base) {
			ide_init_hwif_ports(hwif->io_ports, base, NULL);
			hwif->io_ports[IDE_CONTROL_OFFSET] = ctl + 2;
		}
		hwif->chipset = ide_pci;
		hwif->pci_bus = bus;
		hwif->pci_fn = fn;
		hwif->pci_devid = d->devid;
		hwif->channel = port;
		if (!hwif->irq)
			hwif->irq = pciirq;
		if (mate) {
			hwif->mate = mate;
			mate->mate = hwif;
		}
		if (no_autodma)
			hwif->no_autodma = 1;
#ifdef CONFIG_BLK_DEV_IDEDMA
		if (IDE_PCI_DEVID_EQ(d->devid, DEVID_PDC20246) || ((ccode >> 16) == PCI_CLASS_STORAGE_IDE && (ccode & 0x8000))) {
			unsigned int extra = (!mate && IDE_PCI_DEVID_EQ(d->devid, DEVID_PDC20246)) ? 16 : 0;
			unsigned long dma_base = ide_get_or_set_dma_base(hwif, extra, d->name);
			if (dma_base && !(pcicmd & 4)) {
				/*
 	 			 * Set up BM-DMA capability (PnP BIOS should have done this)
 	 			 */
				hwif->no_autodma = 1;	/* default DMA off if we had to configure it here */
				(void) pcibios_write_config_word(bus, fn, 0x04, (pcicmd|4));
				if (pcibios_read_config_word(bus, fn, 0x04, &pcicmd) || !(pcicmd & 4)) {
					printk("%s: %s error updating PCICMD\n", hwif->name, d->name);
					dma_base = 0;
				}
			}
			if (dma_base)
				ide_setup_dma(hwif, dma_base, 8);
			else
				printk("%s: %s Bus-Master DMA disabled (BIOS), pcicmd=0x%04x, ccode=0x%04x, dma_base=0x%04lx\n",
				 hwif->name, d->name, pcicmd, ccode, dma_base);
		}
#endif	/* CONFIG_BLK_DEV_IDEDMA */
		if (d->init_hwif)  /* Call chipset-specific routine for each enabled hwif */
			d->init_hwif(hwif);
		mate = hwif;
		at_least_one_hwif_enabled = 1;
	}
	if (!at_least_one_hwif_enabled)
		printk("%s: neither IDE port enabled (BIOS)\n", d->name);
}

/*
 * ide_scan_pci_device() examines all functions of a PCI device,
 * looking for IDE interfaces and/or devices in ide_pci_chipsets[].
 * We cannot use pcibios_find_class() cuz it doesn't work in all systems.
 */
static inline void ide_scan_pci_device (unsigned int bus, unsigned int fn)
{
	unsigned int		ccode;
	ide_pci_devid_t		devid;
	ide_pci_device_t	*d;
	byte			hedt;

	if (pcibios_read_config_byte(bus, fn, 0x0e, &hedt))
		hedt = 0;
	do {
		if (pcibios_read_config_word(bus, fn, 0x00, &devid.vid)
		 || devid.vid == 0xffff
		 || pcibios_read_config_word(bus, fn, 0x02, &devid.did)
		 || IDE_PCI_DEVID_EQ(devid, IDE_PCI_DEVID_NULL)
		 || pcibios_read_config_dword(bus, fn, 0x08, &ccode))
			return;
		/* 
		 * workaround Intel Advanced/ZP with bios <= 1.04;
		 * these appear in some Dell Dimension XPS's 
		 */
		if (!hedt && IDE_PCI_DEVID_EQ(devid, DEVID_PIIXa))
		        hedt = 0x80;

		for (d = ide_pci_chipsets; d->devid.vid && !IDE_PCI_DEVID_EQ(d->devid, devid); ++d);
		if (d->init_hwif == IDE_IGNORE)
			printk("%s: ignored by ide_scan_pci_device() (uses own driver)\n", d->name);
		else if (IDE_PCI_DEVID_EQ(d->devid, DEVID_OPTI621V) && !(fn & 1))
			continue;	/* OPTI Viper-M uses same devid for functions 0 and 1 */
		else if (!IDE_PCI_DEVID_EQ(d->devid, IDE_PCI_DEVID_NULL) || (ccode >> 16) == PCI_CLASS_STORAGE_IDE) {
			if (IDE_PCI_DEVID_EQ(d->devid, IDE_PCI_DEVID_NULL))
				printk("%s: unknown IDE controller on PCI bus %d function %d, VID=%04x, DID=%04x\n",
					d->name, bus, fn, devid.vid, devid.did);
			else
				printk("%s: IDE controller on PCI bus %d function %d\n", d->name, bus, fn);
			ide_setup_pci_device(bus, fn, ccode, d);
		}
	} while (hedt == 0x80 && (++fn & 7));
}

/*
 * ide_scan_pcibus() gets invoked at boot time from ide.c
 *
 * Loops over all PCI devices on all PCI buses, invoking ide_scan_pci_device().
 * We cannot use pcibios_find_class() cuz it doesn't work in all systems.
 */
void ide_scan_pcibus (void) /* __init */
{
	unsigned int bus, dev;

	if (!pcibios_present())
		return;
	for (bus = 0; bus <= 255; ++bus) {
		for (dev = 0; dev < 256; dev += 8) {
			ide_scan_pci_device(bus, dev);
		}
	}
}
