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
#include <linux/init.h>

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
#define DEVID_AEC6210	((ide_pci_devid_t){PCI_VENDOR_ID_ARTOP,   PCI_DEVICE_ID_ARTOP_ATP850UF})
#define DEVID_W82C105	((ide_pci_devid_t){PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105})
#define DEVID_UM8886A	((ide_pci_devid_t){PCI_VENDOR_ID_UMC,     PCI_DEVICE_ID_UMC_UM8886A})
#define DEVID_UM8886BF	((ide_pci_devid_t){PCI_VENDOR_ID_UMC,     PCI_DEVICE_ID_UMC_UM8886BF})
#define DEVID_HPT343	((ide_pci_devid_t){PCI_VENDOR_ID_TTI,     PCI_DEVICE_ID_TTI_HPT343})

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

#ifdef CONFIG_BLK_DEV_CMD646
extern void ide_init_cmd646(ide_hwif_t *);
#define INIT_CMD646	&ide_init_cmd646
#else
#ifdef __sparc_v9__
#define INIT_CMD646	IDE_IGNORE
#else
#define INIT_CMD646	NULL
#endif
#endif

#ifdef CONFIG_BLK_DEV_SL82C105
extern void ide_init_sl82c105(ide_hwif_t *);
#define INIT_W82C105	&ide_init_sl82c105
#else
#define INIT_W82C105	IDE_IGNORE
#endif

#ifdef CONFIG_BLK_DEV_RZ1000
extern void ide_init_rz1000(ide_hwif_t *);
#define INIT_RZ1000	&ide_init_rz1000
#else
#define INIT_RZ1000	IDE_IGNORE
#endif

#ifdef CONFIG_BLK_DEV_VIA82C586
extern void ide_init_via82c586(ide_hwif_t *);
#define	INIT_VIA82C586	&ide_init_via82c586
#else
#define	INIT_VIA82C586	NULL
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
	byte			bootable;
	unsigned int		extra;
} ide_pci_device_t;

static ide_pci_device_t ide_pci_chipsets[] __initdata = {
	{DEVID_PIIXa,	"PIIX",		NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}}, 	ON_BOARD,	0 },
	{DEVID_PIIXb,	"PIIX",		NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}}, 	ON_BOARD,	0 },
	{DEVID_PIIX3,	"PIIX3",	NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}}, 	ON_BOARD,	0 },
	{DEVID_PIIX4,	"PIIX4",	NULL,		{{0x41,0x80,0x80}, {0x43,0x80,0x80}}, 	ON_BOARD,	0 },
	{DEVID_VP_IDE,	"VP_IDE",	INIT_VIA82C586,	{{0x40,0x02,0x02}, {0x40,0x01,0x01}}, 	ON_BOARD,	0 },
	{DEVID_PDC20246,"PDC20246",	NULL,		{{0x50,0x02,0x02}, {0x50,0x04,0x04}}, 	OFF_BOARD,	16 },
	{DEVID_RZ1000,	"RZ1000",	INIT_RZ1000,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_RZ1001,	"RZ1001",	INIT_RZ1000,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_CMD640,	"CMD640",	IDE_IGNORE,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_NS87410,	"NS87410",	NULL,		{{0x43,0x08,0x08}, {0x47,0x08,0x08}}, 	ON_BOARD,	0 },
	{DEVID_SIS5513,	"SIS5513",	NULL,		{{0x4a,0x02,0x02}, {0x4a,0x04,0x04}}, 	ON_BOARD,	0 },
	{DEVID_CMD646,	"CMD646",	INIT_CMD646,	{{0x00,0x00,0x00}, {0x51,0x80,0x80}}, 	ON_BOARD,	0 },
	{DEVID_HT6565,	"HT6565",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_OPTI621,	"OPTI621",	INIT_OPTI621,	{{0x45,0x80,0x00}, {0x40,0x08,0x00}}, 	ON_BOARD,	0 },
	{DEVID_OPTI621X,"OPTI621X",	INIT_OPTI621,	{{0x45,0x80,0x00}, {0x40,0x08,0x00}}, 	ON_BOARD,	0 },
	{DEVID_TRM290,	"TRM290",	INIT_TRM290,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_NS87415,	"NS87415",	INIT_NS87415,	{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_AEC6210,	"AEC6210",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	OFF_BOARD,	0 },
	{DEVID_W82C105,	"W82C105",	INIT_W82C105,	{{0x40,0x01,0x01}, {0x40,0x10,0x10}}, 	ON_BOARD,	0 },
	{DEVID_UM8886A,	"UM8886A",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}},	ON_BOARD,	0 },
	{DEVID_UM8886BF,"UM8886BF",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 },
	{DEVID_HPT343,	"HPT343",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}},	NEVER_BOARD,	16 },
	{IDE_PCI_DEVID_NULL, "PCI_IDE",	NULL,		{{0x00,0x00,0x00}, {0x00,0x00,0x00}}, 	ON_BOARD,	0 }};

/*
 * This allows offboard ide-pci cards the enable a BIOS, verify interrupt
 * settings of split-mirror pci-config space, place chipset into init-mode,
 * and/or preserve an interrupt if the card is not native ide support.
 */
__initfunc(static unsigned int ide_special_settings (struct pci_dev *dev, const char *name))
{
	switch(dev->device) {
		case PCI_DEVICE_ID_ARTOP_ATP850UF:
		case PCI_DEVICE_ID_PROMISE_20246:
			if (dev->rom_address) {
				pci_write_config_byte(dev, PCI_ROM_ADDRESS,
					dev->rom_address | PCI_ROM_ADDRESS_ENABLE);
				printk(KERN_INFO "%s: ROM enabled at 0x%08lx\n", name, dev->rom_address);
			}
			
			if ((dev->class >> 8) == PCI_CLASS_STORAGE_RAID) {
				unsigned char irq1 = 0, irq2 = 0;

				pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq1);
				pci_read_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, &irq2);	/* 0xbc */
				if (irq1 != irq2) {
					printk("%s: IRQ1 %d IRQ2 %d\n",
						name, irq1, irq2);
					pci_write_config_byte(dev, (PCI_INTERRUPT_LINE)|0x80, irq1);	/* 0xbc */
				}
			}
			return dev->irq;
		case PCI_DEVICE_ID_TTI_HPT343:
			return dev->irq;
		default:
			break;
	}
	return 0;
}

/*
 * Match a PCI IDE port against an entry in ide_hwifs[],
 * based on io_base port if possible.
 */
__initfunc(static ide_hwif_t *ide_match_hwif (unsigned long io_base, byte bootable, const char *name))
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
			printk("%s: port 0x%04lx already claimed by %s\n", name, io_base, hwif->name);
			return NULL;	/* already claimed */
		}
	}
	/*
	 * Okay, there is no hwif matching our io_base,
	 * so we'll just claim an unassigned slot.
	 * Give preference to claiming other slots before claiming ide0/ide1,
	 * just in case there's another interface yet-to-be-scanned
	 * which uses ports 1f0/170 (the ide0/ide1 defaults).
	 *
	 * Unless there is a bootable card that does not use the standard
	 * ports 1f0/170 (the ide0/ide1 defaults). The (bootable) flag.
	 */
	if (bootable) {
		for (h = 0; h < MAX_HWIFS; ++h) {
			hwif = &ide_hwifs[h];
			if (hwif->chipset == ide_unknown)
				return hwif;	/* pick an unused entry */
		}
	} else {
		for (h = 2; h < MAX_HWIFS; ++h) {
			hwif = ide_hwifs + h;
			if (hwif->chipset == ide_unknown)
				return hwif;	/* pick an unused entry */
		}
	}
	for (h = 0; h < 2; ++h) {
		hwif = ide_hwifs + h;
		if (hwif->chipset == ide_unknown)
			return hwif;	/* pick an unused entry */
	}
	printk("%s: too many IDE interfaces, no room in table\n", name);
	return NULL;
}

__initfunc(static int ide_setup_pci_baseregs (struct pci_dev *dev, const char *name))
{
	byte reg, progif = 0;

	/*
	 * Place both IDE interfaces into PCI "native" mode:
	 */
	if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) || (progif & 5) != 5) {
		if ((progif & 0xa) != 0xa) {
			printk("%s: device not capable of full native PCI mode\n", name);
			return 1;
		}
		printk("%s: placing both ports into native PCI mode\n", name);
		(void) pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) || (progif & 5) != 5) {
			printk("%s: rewrite of PROGIF failed, wanted 0x%04x, got 0x%04x\n", name, progif|5, progif);
			return 1;
		}
	}
	/*
	 * Setup base registers for IDE command/control spaces for each interface:
	 */
	for (reg = 0; reg < 4; reg++)
		if (!dev->base_address[reg]) {
			printk("%s: Missing I/O address #%d, please report to <mj@ucw.cz>\n", name, reg);
			return 1;
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
__initfunc(static void ide_setup_pci_device (struct pci_dev *dev, ide_pci_device_t *d))
{
	unsigned int port, at_least_one_hwif_enabled = 0, autodma = 0, pciirq = 0;
	unsigned short pcicmd = 0, tried_config = 0;
	byte tmp = 0;
	ide_hwif_t *hwif, *mate = NULL;

#ifdef CONFIG_IDEDMA_AUTO
	autodma = 1;
#endif
check_if_enabled:
	if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd)) {
		printk("%s: error accessing PCI regs\n", d->name);
		return;
	}
	if (!(pcicmd & PCI_COMMAND_IO)) {	/* is device disabled? */
		/*
		 * PnP BIOS was *supposed* to have set this device up for us,
		 * but we can do it ourselves, so long as the BIOS has assigned an IRQ
		 *  (or possibly the device is using a "legacy header" for IRQs).
		 * Maybe the user deliberately *disabled* the device,
		 * but we'll eventually ignore it again if no drives respond.
		 */
		if (tried_config++
		 || ide_setup_pci_baseregs(dev, d->name)
		 || pci_write_config_word(dev, PCI_COMMAND, pcicmd | PCI_COMMAND_IO)) {
			printk("%s: device disabled (BIOS)\n", d->name);
			return;
		}
		autodma = 0;	/* default DMA off if we had to configure it here */
		goto check_if_enabled;
	}
	if (tried_config)
		printk("%s: device enabled (Linux)\n", d->name);
	/*
	 * Can we trust the reported IRQ?
	 */
	pciirq = dev->irq;
	if ((dev->class & ~(0xfa)) != ((PCI_CLASS_STORAGE_IDE << 8) | 5)) {
		printk("%s: not 100%% native mode: will probe irqs later\n", d->name);
		pciirq = ide_special_settings(dev, d->name);
	} else if (tried_config) {
		printk("%s: will probe irqs later\n", d->name);
		pciirq = 0;
	} else if (!pciirq) {
		printk("%s: bad irq (%d): will probe later\n", d->name, pciirq);
		pciirq = 0;
	} else {
#ifdef __sparc__
		printk("%s: 100%% native mode on irq %s\n",
		       d->name, __irq_itoa(pciirq));
#else
		printk("%s: 100%% native mode on irq %d\n", d->name, pciirq);
#endif
	}
	/*
	 * Set up the IDE ports
	 */
	for (port = 0; port <= 1; ++port) {
		unsigned long base = 0, ctl = 0;
		ide_pci_enablebit_t *e = &(d->enablebits[port]);
		if (e->reg && (pci_read_config_byte(dev, e->reg, &tmp) || (tmp & e->mask) != e->val))
			continue;	/* port not enabled */
		if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE || (dev->class & (port ? 4 : 1)) != 0) {
			ctl  = dev->base_address[(2*port)+1] & PCI_BASE_ADDRESS_IO_MASK;
			base = dev->base_address[2*port] & ~7;
		}
		if ((ctl && !base) || (base && !ctl)) {
			printk("%s: inconsistent baseregs (BIOS) for port %d, skipping\n", d->name, port);
			continue;
		}
		if (!ctl)
			ctl = port ? 0x374 : 0x3f4;	/* use default value */
		if (!base)
			base = port ? 0x170 : 0x1f0;	/* use default value */
		if ((hwif = ide_match_hwif(base, d->bootable, d->name)) == NULL)
			continue;	/* no room in ide_hwifs[] */
		if (hwif->io_ports[IDE_DATA_OFFSET] != base) {
			ide_init_hwif_ports(hwif->io_ports, base, NULL);
			hwif->io_ports[IDE_CONTROL_OFFSET] = ctl + 2;
			hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET];
		}
		hwif->chipset = ide_pci;
		hwif->pci_dev = dev;
		hwif->pci_devid = d->devid;
		hwif->channel = port;
		if (!hwif->irq)
			hwif->irq = pciirq;
		if (mate) {
			hwif->mate = mate;
			mate->mate = hwif;
			if (IDE_PCI_DEVID_EQ(d->devid, DEVID_AEC6210)) {
				hwif->serialized = 1;
				mate->serialized = 1;
			}
		}
		if (IDE_PCI_DEVID_EQ(d->devid, DEVID_UM8886A) ||
		    IDE_PCI_DEVID_EQ(d->devid, DEVID_UM8886BF))
			hwif->irq = hwif->channel ? 15 : 14;

#ifdef CONFIG_BLK_DEV_IDEDMA
		if (IDE_PCI_DEVID_EQ(d->devid, DEVID_SIS5513))
			autodma = 0;
		if (autodma)
			hwif->autodma = 1;
		if (IDE_PCI_DEVID_EQ(d->devid, DEVID_PDC20246) ||
		    IDE_PCI_DEVID_EQ(d->devid, DEVID_AEC6210) ||
		    IDE_PCI_DEVID_EQ(d->devid, DEVID_HPT343) ||
		    ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE && (dev->class & 0x80))) {
			unsigned long dma_base = ide_get_or_set_dma_base(hwif, (!mate && d->extra) ? d->extra : 0, d->name);
			if (dma_base && !(pcicmd & PCI_COMMAND_MASTER)) {
				/*
 	 			 * Set up BM-DMA capability (PnP BIOS should have done this)
 	 			 */
				hwif->autodma = 0;	/* default DMA off if we had to configure it here */
				(void) pci_write_config_word(dev, PCI_COMMAND, pcicmd | PCI_COMMAND_MASTER);
				if (pci_read_config_word(dev, PCI_COMMAND, &pcicmd) || !(pcicmd & PCI_COMMAND_MASTER)) {
					printk("%s: %s error updating PCICMD\n", hwif->name, d->name);
					dma_base = 0;
				}
			}
			if (dma_base)
				ide_setup_dma(hwif, dma_base, 8);
			else
				printk("%s: %s Bus-Master DMA disabled (BIOS)\n", hwif->name, d->name);
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
 * ide_scan_pcibus() gets invoked at boot time from ide.c.
 * It finds all PCI IDE controllers and calls ide_setup_pci_device for them.
 */
__initfunc(void ide_scan_pcibus (void))
{
	struct pci_dev		*dev;
	ide_pci_devid_t		devid;
	ide_pci_device_t	*d;

	if (!pci_present())
		return;
	for(dev = pci_devices; dev; dev=dev->next) {
		devid.vid = dev->vendor;
		devid.did = dev->device;
		for (d = ide_pci_chipsets; d->devid.vid && !IDE_PCI_DEVID_EQ(d->devid, devid); ++d);
		if (d->init_hwif == IDE_IGNORE)
			printk("%s: ignored by ide_scan_pci_device() (uses own driver)\n", d->name);
		else if (IDE_PCI_DEVID_EQ(d->devid, DEVID_OPTI621V) && !(PCI_FUNC(dev->devfn) & 1))
			continue;	/* OPTI Viper-M uses same devid for functions 0 and 1 */
		else if (!IDE_PCI_DEVID_EQ(d->devid, IDE_PCI_DEVID_NULL) || (dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
			if (IDE_PCI_DEVID_EQ(d->devid, IDE_PCI_DEVID_NULL))
				printk("%s: unknown IDE controller on PCI bus %02x device %02x, VID=%04x, DID=%04x\n",
					d->name, dev->bus->number, dev->devfn, devid.vid, devid.did);
			else
				printk("%s: IDE controller on PCI bus %02x dev %02x\n", d->name, dev->bus->number, dev->devfn);
			ide_setup_pci_device(dev, d);
		}
	}
}
