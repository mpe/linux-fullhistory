/*
 * $Id: lubbock-flash.c,v 1.9 2003/06/23 11:48:18 dwmw2 Exp $
 *
 * Map driver for the Lubbock developer platform.
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/hardware.h>


#define ROM_ADDR	0x00000000
#define FLASH_ADDR	0x04000000

#define WINDOW_SIZE 	64*1024*1024

static struct map_info lubbock_maps[2] = { {
	.size =		WINDOW_SIZE,
	.phys =		0x00000000,
}, {
	.size =		WINDOW_SIZE,
	.phys =		0x04000000,
} };

static struct mtd_partition lubbock_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x00100000,
		.offset =	0x00040000,
	},{
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00140000
	}
};

static struct mtd_info *mymtds[2];
static struct mtd_partition *parsed_parts[2];
static int nr_parsed_parts[2];

static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };

static int __init init_lubbock(void)
{
	int flashboot = (CONF_SWITCHES & 1);
	int ret = 0, i;

	lubbock_maps[0].buswidth = lubbock_maps[1].buswidth = 
		(BOOT_DEF & 1) ? 2 : 4;

	/* Compensate for the nROMBT switch which swaps the flash banks */
	printk(KERN_NOTICE "Lubbock configured to boot from %s (bank %d)\n",
	       flashboot?"Flash":"ROM", flashboot);

	lubbock_maps[flashboot^1].name = "Lubbock Application Flash";
	lubbock_maps[flashboot].name = "Lubbock Boot ROM";

	for (i = 0; i < 2; i++) {
		lubbock_maps[i].virt = (unsigned long)__ioremap(lubbock_maps[i].phys, WINDOW_SIZE, 0);
		if (!lubbock_maps[i].virt) {
			printk(KERN_WARNING "Failed to ioremap %s\n", lubbock_maps[i].name);
			if (!ret)
				ret = -ENOMEM;
			continue;
		}
		simple_map_init(&lubbock_maps[i]);

		printk(KERN_NOTICE "Probing %s at physical address 0x%08lx (%d-bit buswidth)\n",
		       lubbock_maps[i].name, lubbock_maps[i].phys, 
		       lubbock_maps[i].buswidth * 8);

		mymtds[i] = do_map_probe("cfi_probe", &lubbock_maps[i]);
		
		if (!mymtds[i]) {
			iounmap((void *)lubbock_maps[i].virt);
			if (!ret)
				ret = -EIO;
			continue;
		}
		mymtds[i]->owner = THIS_MODULE;

		int ret = parse_mtd_partitions(mymtds[i], probes,
					       &parsed_parts[i], 0);

		if (ret > 0)
			nr_parsed_parts[i] = ret;
	}

	if (!mymtds[0] && !mymtds[1])
		return ret;
	
	for (i = 0; i < 2; i++) {
		if (!mymtds[i]) {
			printk(KERN_WARNING "%s is absent. Skipping\n", lubbock_maps[i].name);
		} else if (nr_parsed_parts[i]) {
			add_mtd_partitions(mymtds[i], parsed_parts[i], nr_parsed_parts[i]);
		} else if (!i) {
			printk("Using static partitions on %s\n", lubbock_maps[i].name);
			add_mtd_partitions(mymtds[i], lubbock_partitions, ARRAY_SIZE(lubbock_partitions));
		} else {
			printk("Registering %s as whole device\n", lubbock_maps[i].name);
			add_mtd_device(mymtds[i]);
		}
	}
	return 0;
}

static void __exit cleanup_lubbock(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (!mymtds[i])
			continue;

		if (nr_parsed_parts[i] || !i)
			del_mtd_partitions(mymtds[i]);
		else
			del_mtd_device(mymtds[i]);			

		map_destroy(mymtds[i]);
		iounmap((void *)lubbock_maps[i].virt);

		if (parsed_parts[i])
			kfree(parsed_parts[i]);
	}
}

module_init(init_lubbock);
module_exit(cleanup_lubbock);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@cam.org>");
MODULE_DESCRIPTION("MTD map driver for Intel Lubbock");
