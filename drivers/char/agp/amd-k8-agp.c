/* 
 * Copyright 2001-2003 SuSE Labs.
 * Distributed under the GNU public license, v2.
 * 
 * This is a GART driver for the AMD Opteron/Athlon64 on-CPU northbridge.
 * It also includes support for the AMD 8151 AGP bridge,
 * although it doesn't actually do much, as all the real
 * work is done in the northbridge(s).
 */

/*
 * On x86-64 the AGP driver needs to be initialized early by the IOMMU 
 * code.  When you use this driver as a template for a new K8 AGP bridge
 * driver don't forget to change arch/x86_64/kernel/pci-gart.c too -AK.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

/* Will need to be increased if hammer ever goes >8-way. */
#define MAX_HAMMER_GARTS   8

static int nr_garts;
static struct pci_dev * hammers[MAX_HAMMER_GARTS];

static int gart_iterator;
#define for_each_nb() for(gart_iterator=0;gart_iterator<nr_garts;gart_iterator++)

static void flush_x86_64_tlb(struct pci_dev *dev)
{
	u32 tmp;

	pci_read_config_dword (dev, AMD_X86_64_GARTCACHECTL, &tmp);
	tmp |= 1<<0;
	pci_write_config_dword (dev, AMD_X86_64_GARTCACHECTL, tmp);
}

static void amd_x86_64_tlbflush(struct agp_memory *temp)
{
	for_each_nb()
		flush_x86_64_tlb(hammers[gart_iterator]);
}

static int x86_64_insert_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	long tmp;
	u32 pte;
	u64 addr;

	num_entries = agp_num_entries();

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	/* Make sure we can fit the range in the gatt table. */
	/* FIXME: could wrap */
	if (((unsigned long)pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	/* gatt table should be empty. */
	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge, agp_bridge->gatt_table[j]))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = agp_bridge->driver->mask_memory(mem->memory[i], mem->type);

		tmp = addr;
		BUG_ON(tmp & 0xffffff0000000ffc);
		pte = (tmp & 0x000000ff00000000) >> 28;
		pte |=(tmp & 0x00000000fffff000);
		pte |= 1<<1|1<<0;

		agp_bridge->gatt_table[j] = pte;
	}
	amd_x86_64_tlbflush(mem);
	return 0;
}

/*
 * This hack alters the order element according
 * to the size of a long. It sucks. I totally disown this, even
 * though it does appear to work for the most part.
 */
static struct aper_size_info_32 x86_64_aperture_sizes[7] =
{
	{32,   8192,   3+(sizeof(long)/8), 0 },
	{64,   16384,  4+(sizeof(long)/8), 1<<1 },
	{128,  32768,  5+(sizeof(long)/8), 1<<2 },
	{256,  65536,  6+(sizeof(long)/8), 1<<1 | 1<<2 },
	{512,  131072, 7+(sizeof(long)/8), 1<<3 },
	{1024, 262144, 8+(sizeof(long)/8), 1<<1 | 1<<3},
	{2048, 524288, 9+(sizeof(long)/8), 1<<2 | 1<<3}
};


/*
 * Get the current Aperture size from the x86-64.
 * Note, that there may be multiple x86-64's, but we just return
 * the value from the first one we find. The set_size functions
 * keep the rest coherent anyway. Or at least should do.
 */
static int amd_x86_64_fetch_size(void)
{
	struct pci_dev *dev;
	int i;
	u32 temp;
	struct aper_size_info_32 *values;

	dev = hammers[0];
	if (dev==NULL)
		return 0;

	pci_read_config_dword(dev, AMD_X86_64_GARTAPERTURECTL, &temp);
	temp = (temp & 0xe);
	values = A_SIZE_32(x86_64_aperture_sizes);

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}
	return 0;
}

/*
 * In a multiprocessor x86-64 system, this function gets
 * called once for each CPU.
 */
static u64 amd_x86_64_configure (struct pci_dev *hammer, u64 gatt_table)
{
	u64 aperturebase;
	u32 tmp;
	u64 addr, aper_base;

	/* Address to map to */
	pci_read_config_dword (hammer, AMD_X86_64_GARTAPERTUREBASE, &tmp);
	aperturebase = tmp << 25;
	aper_base = (aperturebase & PCI_BASE_ADDRESS_MEM_MASK);

	/* address of the mappings table */
	addr = (u64) gatt_table;
	addr >>= 12;
	tmp = (u32) addr<<4;
	tmp &= ~0xf;
	pci_write_config_dword (hammer, AMD_X86_64_GARTTABLEBASE, tmp);

	/* Enable GART translation for this hammer. */
	pci_read_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, &tmp);
	tmp &= 0x3f;
	tmp |= 1<<0;
	pci_write_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, tmp);

	/* keep CPU's coherent. */
	flush_x86_64_tlb (hammer);
	
	return aper_base;
}


static struct aper_size_info_32 amd_8151_sizes[7] =
{
	{2048, 524288, 9, 0x00000000 },	/* 0 0 0 0 0 0 */
	{1024, 262144, 8, 0x00000400 },	/* 1 0 0 0 0 0 */
	{512,  131072, 7, 0x00000600 },	/* 1 1 0 0 0 0 */
	{256,  65536,  6, 0x00000700 },	/* 1 1 1 0 0 0 */
	{128,  32768,  5, 0x00000720 },	/* 1 1 1 1 0 0 */
	{64,   16384,  4, 0x00000730 },	/* 1 1 1 1 1 0 */
	{32,   8192,   3, 0x00000738 } 	/* 1 1 1 1 1 1 */
};

static int amd_8151_configure(void)
{
	unsigned long gatt_bus = virt_to_phys(agp_bridge->gatt_table_real);

	/* Configure AGP regs in each x86-64 host bridge. */
	for_each_nb() {
		agp_bridge->gart_bus_addr =
				amd_x86_64_configure(hammers[gart_iterator],gatt_bus);
	}
	return 0;
}


static void amd_8151_cleanup(void)
{
	u32 tmp;

	for_each_nb() {
		/* disable gart translation */
		pci_read_config_dword (hammers[gart_iterator], AMD_X86_64_GARTAPERTURECTL, &tmp);
		tmp &= ~(AMD_X86_64_GARTEN);
		pci_write_config_dword (hammers[gart_iterator], AMD_X86_64_GARTAPERTURECTL, tmp);
	}
}


static struct gatt_mask amd_8151_masks[] =
{
	{ .mask = 1, .type = 0 }
};

struct agp_bridge_driver amd_8151_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= amd_8151_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= amd_8151_configure,
	.fetch_size		= amd_x86_64_fetch_size,
	.cleanup		= amd_8151_cleanup,
	.tlb_flush		= amd_x86_64_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= amd_8151_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= x86_64_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
};

static int __init agp_amdk8_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	struct pci_dev *loop_dev = NULL;
	u8 rev_id;
	u8 cap_ptr;
	int i = 0;
	char *revstring="  ";

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	printk(KERN_INFO PFX "Detected Opteron/Athlon64 on-CPU GART\n");

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    pdev->device == PCI_DEVICE_ID_AMD_8151_0) {

		pci_read_config_byte(pdev, PCI_REVISION_ID, &rev_id);
		switch (rev_id) {
		case 0x01:	revstring="A0";
				break;
		case 0x02:	revstring="A1";
				break;
		case 0x11:	revstring="B0";
				break;
		case 0x12:	revstring="B1";
				break;
		case 0x13:	revstring="B2";
				break;
		default:	revstring="??";
				break;
		}
		printk (KERN_INFO PFX "Detected AMD 8151 AGP Bridge rev %s\n", revstring);
		/*
		 * Work around errata.
		 * Chips before B2 stepping incorrectly reporting v3.5
		 */
		if (rev_id < 0x13) {
			printk (KERN_INFO PFX "Correcting AGP revision (reports 3.5, is really 3.0)\n");
			bridge->major_version = 3;
			bridge->minor_version = 0;
		}
	}

	bridge->driver = &amd_8151_driver;
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev, bridge->capndx+PCI_AGP_STATUS, &bridge->mode);

	/* cache pci_devs of northbridges. */
	while ((loop_dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, loop_dev)) != NULL) {
		if (loop_dev->bus->number == 0 &&
		    PCI_FUNC(loop_dev->devfn) == 3 &&
		    PCI_SLOT(loop_dev->devfn) >=24 &&
		    PCI_SLOT(loop_dev->devfn) <=31) {
			hammers[i++] = loop_dev;
			nr_garts = i;
			if (i == MAX_HAMMER_GARTS)
				goto out_free;
		}
	}

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
out_free:
	agp_put_bridge(bridge);
	return -ENOMEM;
}

static void __devexit agp_amdk8_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_amdk8_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AMD,
	.device		= PCI_DEVICE_ID_AMD_8151_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_K8T400M_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SI,
	.device		= PCI_DEVICE_ID_SI_755,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_amdk8_pci_table);

static struct pci_driver agp_amdk8_pci_driver = {
	.name		= "agpgart-amd-k8",
	.id_table	= agp_amdk8_pci_table,
	.probe		= agp_amdk8_probe,
	.remove		= agp_amdk8_remove,
};

/* Not static due to IOMMU code calling it early. */
int __init agp_amdk8_init(void)
{
	return pci_module_init(&agp_amdk8_pci_driver);
}

static void __exit agp_amdk8_cleanup(void)
{
	pci_unregister_driver(&agp_amdk8_pci_driver);
}

/* On x86-64 the PCI driver needs to initialize this driver early
   for the IOMMU, so it has to be called via a backdoor. */
#ifndef CONFIG_GART_IOMMU
module_init(agp_amdk8_init);
module_exit(agp_amdk8_cleanup);
#endif

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

