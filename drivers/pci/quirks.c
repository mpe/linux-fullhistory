/*
 * $Id: quirks.c,v 1.3 1998/02/06 19:51:42 mj Exp $
 *
 * PCI Chipset-Specific Quirks
 *
 * Extracted from pci.c and rewritten by Martin Mares
 *
 * This is the right place for all special fixups for on-board
 * devices not depending on system architecture -- for example
 * bus bridges. The only thing implemented in this release is
 * the bridge optimization, but others might appear later.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#undef DEBUG

#ifdef CONFIG_PCI_OPTIMIZE

/*
 *	The PCI Bridge Optimization -- Some BIOS'es are too lazy
 *	and are unable to turn on several features which can burst
 *	system performance.
 */

/*
 * An item of this structure has the following meaning:
 * for each optimization, the register address, the mask
 * and value to write to turn it on.
 */
struct optimization_type {
	const char	*type;
	const char	*off;
	const char	*on;
} bridge_optimization[] __initdata = {
	{"Cache L2",			"write through",	"write back"},
	{"CPU-PCI posted write",	"off",			"on"},
	{"CPU-Memory posted write",	"off",			"on"},
	{"PCI-Memory posted write",	"off",			"on"},
	{"PCI burst",			"off",			"on"}
};

#define NUM_OPTIMIZATIONS \
	(sizeof(bridge_optimization) / sizeof(bridge_optimization[0]))

struct bridge_mapping_type {
	unsigned char	addr;	/* config space address */
	unsigned char	mask;
	unsigned char	value;
} bridge_mapping[] = {
	/*
	 * Intel Neptune/Mercury/Saturn:
	 *	If the internal cache is write back,
	 *	the L2 cache must be write through!
	 *	I've to check out how to control that
	 *	for the moment, we won't touch the cache
	 */
	{0x0	,0x02	,0x02	},
	{0x53	,0x02	,0x02	},
	{0x53	,0x01	,0x01	},
	{0x54	,0x01	,0x01	},
	{0x54	,0x02	,0x02	},

	/*
	 * UMC 8891A Pentium chipset:
	 *	Why did you think UMC was cheaper ??
	 */
	{0x50	,0x10	,0x00	},
	{0x51	,0x40	,0x40	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
};

__initfunc(static void quirk_bridge(struct pci_dev *dev, int pos))
{
	struct bridge_mapping_type *bmap;
	unsigned char val;
	int i;

	pos *= NUM_OPTIMIZATIONS;
	for (i = 0; i < NUM_OPTIMIZATIONS; i++) {
		printk("    %s: ", bridge_optimization[i].type);
		bmap = &bridge_mapping[pos + i];
		if (!bmap->addr) {
			printk("Not supported.");
		} else {
			pcibios_read_config_byte(dev->bus->number, dev->devfn, bmap->addr, &val);
			if ((val & bmap->mask) == bmap->value)
				printk("%s.", bridge_optimization[i].on);
			else {
				printk("%s.", bridge_optimization[i].off);
				pcibios_write_config_byte(dev->bus->number, dev->devfn,
							  bmap->addr,
							  (val & (0xff - bmap->mask))
							  + bmap->value);
				printk("Changed!  Now %s.", bridge_optimization[i].on);
			}
		}
		printk("\n");
	}
}

#endif


/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
__initfunc(static void quirk_passive_release(struct pci_dev *dev, int arg))
{
	struct pci_dev *piix3;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	for (piix3 = pci_devices; ; piix3 = piix3->next) {
		if (!piix3)
			return;

		if (piix3->vendor == PCI_VENDOR_ID_INTEL
		    && piix3->device == PCI_DEVICE_ID_INTEL_82371SB_0)
			break;
	}	

	pcibios_read_config_byte(piix3->bus->number, piix3->devfn, 0x82, &dlc);

	if (!(dlc & 1<<1)) {
		printk("PIIX3: Enabling Passive Release\n");
		dlc |= 1<<1;
		pcibios_write_config_byte(piix3->bus->number, piix3->devfn, 
					  0x82, dlc);
	}
}


typedef void (*quirk_handler)(struct pci_dev *, int);

/*
 * Mpping from quirk handler functions to names.
 */

struct quirk_name {
	quirk_handler handler;
	char *name;
};

static struct quirk_name quirk_names[] __initdata = {
#ifdef CONFIG_PCI_OPTIMIZE
	{ quirk_bridge,		"Bridge optimization" },
#endif
	{ quirk_passive_release, "Passive release enable" },
};


static inline char *get_quirk_name(quirk_handler handler)
{
	int i;

	for (i = 0; i < sizeof(quirk_names)/sizeof(quirk_names[0]); i++)
		if (handler == quirk_names[i].handler)
			return quirk_names[i].name;

	return NULL;
}
  

/*
 * Mapping from PCI vendor/device ID pairs to quirk function types and arguments
 */

struct quirk_info {
	unsigned short vendor, device;
	quirk_handler handler;
	unsigned short arg;
};

static struct quirk_info quirk_list[] __initdata = {
#ifdef CONFIG_PCI_OPTIMIZE
	{ PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_BRD,		quirk_bridge,	0x00 },
	{ PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8891A,	quirk_bridge,	0x01 },
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82424,	quirk_bridge,	0x00 },
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82434,	quirk_bridge,	0x00 },
#endif
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release,	0x00 },
};

__initfunc(void pci_quirks_init(void))
{
	struct pci_dev *d;
	int i;

#ifdef DEBUG
	printk("PCI: pci_quirks_init\n");
#endif
	for(d=pci_devices; d; d=d->next) {
		for(i=0; i<sizeof(quirk_list)/sizeof(quirk_list[0]); i++) {
			struct quirk_info *q = quirk_list + i;
			if (q->vendor == d->vendor && q->device == d->device) {
				printk("PCI: %02x:%02x [%04x/%04x]: %s (%02x)\n",
				       d->bus->number, d->devfn, d->vendor, d->device,
				       get_quirk_name(q->handler), q->arg);
				q->handler(d, q->arg);
			}
		}
	}
}
