/*
 * amd76xrom.c
 *
 * Normal mappings of chips in physical memory
 * $Id: amd76xrom.c,v 1.19 2004/11/28 09:40:39 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/flashchip.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/list.h>


#define xstr(s) str(s)
#define str(s) #s
#define MOD_NAME xstr(KBUILD_BASENAME)

#define ADDRESS_NAME_LEN 18

#define ROM_PROBE_STEP_SIZE (64*1024) /* 64KiB */

struct amd76xrom_window {
	void __iomem *virt;
	unsigned long phys;
	unsigned long size;
	struct list_head maps;
	struct resource rsrc;
	struct pci_dev *pdev;
};

struct amd76xrom_map_info {
	struct list_head list;
	struct map_info map;
	struct mtd_info *mtd;
	struct resource rsrc;
	char map_name[sizeof(MOD_NAME) + 2 + ADDRESS_NAME_LEN];
};

static struct amd76xrom_window amd76xrom_window = {
	.maps = LIST_HEAD_INIT(amd76xrom_window.maps),
};

static void amd76xrom_cleanup(struct amd76xrom_window *window)
{
	struct amd76xrom_map_info *map, *scratch;
	u8 byte;

	if (window->pdev) {
		/* Disable writes through the rom window */
		pci_read_config_byte(window->pdev, 0x40, &byte);
		pci_write_config_byte(window->pdev, 0x40, byte & ~1);
	}

	/* Free all of the mtd devices */
	list_for_each_entry_safe(map, scratch, &window->maps, list) {
		if (map->rsrc.parent) {
			release_resource(&map->rsrc);
		}
		del_mtd_device(map->mtd);
		map_destroy(map->mtd);
		list_del(&map->list);
		kfree(map);
	}
	if (window->rsrc.parent) 
		release_resource(&window->rsrc);

	if (window->virt) {
		iounmap(window->virt);
		window->virt = NULL;
		window->phys = 0;
		window->size = 0;
		window->pdev = NULL;
	}
}


static int __devinit amd76xrom_init_one (struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	static char *rom_probe_types[] = { "cfi_probe", "jedec_probe", NULL };
	u8 byte;
	struct amd76xrom_window *window = &amd76xrom_window;
	struct amd76xrom_map_info *map = NULL;
	unsigned long map_top;

	/* Remember the pci dev I find the window in */
	window->pdev = pdev;

	/* Assume the rom window is properly setup, and find it's size */
	pci_read_config_byte(pdev, 0x43, &byte);
	if ((byte & ((1<<7)|(1<<6))) == ((1<<7)|(1<<6))) {
		window->phys = 0xffb00000; /* 5MiB */
	}
	else if ((byte & (1<<7)) == (1<<7)) {
		window->phys = 0xffc00000; /* 4MiB */
	}
	else {
		window->phys = 0xffff0000; /* 64KiB */
	}
	window->size = 0xffffffffUL - window->phys + 1UL;
	
	/*
	 * Try to reserve the window mem region.  If this fails then
	 * it is likely due to a fragment of the window being
	 * "reseved" by the BIOS.  In the case that the
	 * request_mem_region() fails then once the rom size is
	 * discovered we will try to reserve the unreserved fragment.
	 */
	window->rsrc.name = MOD_NAME;
	window->rsrc.start = window->phys;
	window->rsrc.end   = window->phys + window->size - 1;
	window->rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, &window->rsrc)) {
		window->rsrc.parent = NULL;
		printk(KERN_ERR MOD_NAME
			" %s(): Unable to register resource"
			" 0x%.08lx-0x%.08lx - kernel bug?\n",
			__func__,
			window->rsrc.start, window->rsrc.end);
	}

#if 0

	/* Enable the selected rom window */
	pci_read_config_byte(pdev, 0x43, &byte);
	pci_write_config_byte(pdev, 0x43, byte | rwindow->segen_bits);
#endif

	/* Enable writes through the rom window */
	pci_read_config_byte(pdev, 0x40, &byte);
	pci_write_config_byte(pdev, 0x40, byte | 1);
	
	/* FIXME handle registers 0x80 - 0x8C the bios region locks */

	/* For write accesses caches are useless */
	window->virt = ioremap_nocache(window->phys, window->size);
	if (!window->virt) {
		printk(KERN_ERR MOD_NAME ": ioremap(%08lx, %08lx) failed\n",
			window->phys, window->size);
		goto out;
	}

	/* Get the first address to look for an rom chip at */
	map_top = window->phys;
#if 1
	/* The probe sequence run over the firmware hub lock
	 * registers sets them to 0x7 (no access).
	 * Probe at most the last 4M of the address space.
	 */
	if (map_top < 0xffc00000) {
		map_top = 0xffc00000;
	}
#endif
	/* Loop  through and look for rom chips */
	while((map_top - 1) < 0xffffffffUL) {
		struct cfi_private *cfi;
		unsigned long offset;
		int i;

		if (!map) {
			map = kmalloc(sizeof(*map), GFP_KERNEL);
		}
		if (!map) {
			printk(KERN_ERR MOD_NAME ": kmalloc failed");
			goto out;
		}
		memset(map, 0, sizeof(*map));
		INIT_LIST_HEAD(&map->list);
		map->map.name = map->map_name;
		map->map.phys = map_top;
		offset = map_top - window->phys;
		map->map.virt = (void __iomem *)
			(((unsigned long)(window->virt)) + offset);
		map->map.size = 0xffffffffUL - map_top + 1UL;
		/* Set the name of the map to the address I am trying */
		sprintf(map->map_name, "%s @%08lx",
			MOD_NAME, map->map.phys);

		/* There is no generic VPP support */
		for(map->map.bankwidth = 32; map->map.bankwidth; 
			map->map.bankwidth >>= 1)
		{
			char **probe_type;
			/* Skip bankwidths that are not supported */
			if (!map_bankwidth_supported(map->map.bankwidth))
				continue;

			/* Setup the map methods */
			simple_map_init(&map->map);

			/* Try all of the probe methods */
			probe_type = rom_probe_types;
			for(; *probe_type; probe_type++) {
				map->mtd = do_map_probe(*probe_type, &map->map);
				if (map->mtd)
					goto found;
			}
		}
		map_top += ROM_PROBE_STEP_SIZE;
		continue;
	found:
		/* Trim the size if we are larger than the map */
		if (map->mtd->size > map->map.size) {
			printk(KERN_WARNING MOD_NAME
				" rom(%u) larger than window(%lu). fixing...\n",
				map->mtd->size, map->map.size);
			map->mtd->size = map->map.size;
		}
		if (window->rsrc.parent) {
			/*
			 * Registering the MTD device in iomem may not be possible
			 * if there is a BIOS "reserved" and BUSY range.  If this
			 * fails then continue anyway.
			 */
			map->rsrc.name  = map->map_name;
			map->rsrc.start = map->map.phys;
			map->rsrc.end   = map->map.phys + map->mtd->size - 1;
			map->rsrc.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
			if (request_resource(&window->rsrc, &map->rsrc)) {
				printk(KERN_ERR MOD_NAME
					": cannot reserve MTD resource\n");
				map->rsrc.parent = NULL;
			}
		}

		/* Make the whole region visible in the map */
		map->map.virt = window->virt;
		map->map.phys = window->phys;
		cfi = map->map.fldrv_priv;
		for(i = 0; i < cfi->numchips; i++) {
			cfi->chips[i].start += offset;
		}
		
		/* Now that the mtd devices is complete claim and export it */
		map->mtd->owner = THIS_MODULE;
		if (add_mtd_device(map->mtd)) {
			map_destroy(map->mtd);
			map->mtd = NULL;
			goto out;
		}


		/* Calculate the new value of map_top */
		map_top += map->mtd->size;

		/* File away the map structure */
		list_add(&map->list, &window->maps);
		map = NULL;
	}

 out:
	/* Free any left over map structures */
	if (map) {
		kfree(map);
	}
	/* See if I have any map structures */
	if (list_empty(&window->maps)) {
		amd76xrom_cleanup(window);
		return -ENODEV;
	}
	return 0;
}


static void __devexit amd76xrom_remove_one (struct pci_dev *pdev)
{
	struct amd76xrom_window *window = &amd76xrom_window;

	amd76xrom_cleanup(window);
}

static struct pci_device_id amd76xrom_pci_tbl[] = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7410,  
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7440,  
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_AMD, 0x7468 }, /* amd8111 support */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, amd76xrom_pci_tbl);

#if 0
static struct pci_driver amd76xrom_driver = {
	.name =		MOD_NAME,
	.id_table =	amd76xrom_pci_tbl,
	.probe =	amd76xrom_init_one,
	.remove =	amd76xrom_remove_one,
};
#endif

static int __init init_amd76xrom(void)
{
	struct pci_dev *pdev;
	struct pci_device_id *id;
	pdev = NULL;
	for(id = amd76xrom_pci_tbl; id->vendor; id++) {
		pdev = pci_find_device(id->vendor, id->device, NULL);
		if (pdev) {
			break;
		}
	}
	if (pdev) {
		return amd76xrom_init_one(pdev, &amd76xrom_pci_tbl[0]);
	}
	return -ENXIO;
#if 0
	return pci_module_init(&amd76xrom_driver);
#endif
}

static void __exit cleanup_amd76xrom(void)
{
	amd76xrom_remove_one(amd76xrom_window.pdev);
}

module_init(init_amd76xrom);
module_exit(cleanup_amd76xrom);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biederman <ebiederman@lnxi.com>");
MODULE_DESCRIPTION("MTD map driver for BIOS chips on the AMD76X southbridge");

