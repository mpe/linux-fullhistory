/*
 * $Id: quirks.c,v 1.5 1998/05/02 19:24:14 mj Exp $
 *
 * PCI Chipset-Specific Quirks
 *
 * Extracted from pci.c and rewritten by Martin Mares
 *
 * This is the right place for all special fixups for on-board
 * devices not depending on system architecture -- for example
 * bus bridges.
 */

#include <linux/config.h>
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
			printk("Not supported.\n");
		} else {
			pci_read_config_byte(dev, bmap->addr, &val);
			if ((val & bmap->mask) == bmap->value)
				printk("%s.\n", bridge_optimization[i].on);
			else {
				printk("%s", bridge_optimization[i].off);
				pci_write_config_byte(dev,
						      bmap->addr,
						      (val & (0xff - bmap->mask)) + bmap->value);
				printk(" -> %s.\n", bridge_optimization[i].on);
			}
		}
	}
}

#endif


/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
__initfunc(static void quirk_passive_release(struct pci_dev *dev, int arg))
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	while ((d = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			printk("PIIX3: Enabling Passive Release\n");
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}

/*  The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a workaround
    but VIA don't answer queries. If you happen to have good contacts at VIA
    ask them for me please -- Alan 
    
    This appears to be BIOS not version dependent. So presumably there is a 
    chipset level fix */
    

int isa_dma_bridge_buggy = 0;		/* Exported */
    
__initfunc(static void quirk_isa_dma_hangs(struct pci_dev *dev, int arg))
{
	if(!isa_dma_bridge_buggy)
	{
		isa_dma_bridge_buggy=1;
		printk(KERN_INFO "Activating ISA DMA hang workarounds.\n");
	}
}


typedef void (*quirk_handler)(struct pci_dev *, int);

/*
 * Mapping from quirk handler functions to names.
 */

struct quirk_name {
	quirk_handler handler;
	char *name;
};

static struct quirk_name quirk_names[] __initdata = {
#ifdef CONFIG_PCI_OPTIMIZE
	{ quirk_bridge,		"Bridge optimization" },
#endif
	{ quirk_passive_release,"Passive release enable" },
	{ quirk_isa_dma_hangs,	"Work around ISA DMA hangs" },
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
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82430,	quirk_bridge,	0x00 },
#endif
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release,	0x00 },
	/*
	 * Its not totally clear which chipsets are the problematic ones
	 * This is the 82C586 variants. At the moment the 596 is an unknown
	 * quantity 
	 */
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs,	0x00 },
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
