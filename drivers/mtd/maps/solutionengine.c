/*
 * $Id: solutionengine.c,v 1.3 2001/10/02 15:05:14 dwmw2 Exp $
 *
 * Flash and EPROM on Hitachi Solution Engine and similar boards.
 *
 * (C) 2001 Red Hat, Inc.
 *
 * GPL'd
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>


extern int parse_redboot_partitions(struct mtd_info *master, struct mtd_partition **pparts);

__u32 soleng_read32(struct map_info *map, unsigned long ofs)
{
	return __raw_readl(map->map_priv_1 + ofs);
}

void soleng_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	__raw_writel(d, map->map_priv_1 + adr);
	mb();
}

void soleng_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy_fromio(to, map->map_priv_1 + from, len);
}


static struct mtd_info *flash_mtd;
static struct mtd_info *eprom_mtd;

static struct mtd_partition *parsed_parts;

struct map_info soleng_eprom_map = {
	name: "Solution Engine EPROM",
	size: 0x400000,
	buswidth: 4,
	copy_from: soleng_copy_from,
};

struct map_info soleng_flash_map = {
	name: "Solution Engine FLASH",
	size: 0x400000,
	buswidth: 4,
	read32: soleng_read32,
	copy_from: soleng_copy_from,
	write32: soleng_write32,
};

static int __init init_soleng_maps(void)
{
	int nr_parts;

	/* First probe at offset 0 */
	soleng_flash_map.map_priv_1 = P2SEGADDR(0);
	soleng_eprom_map.map_priv_1 = P1SEGADDR(0x400000);

	printk(KERN_NOTICE "Probing for flash chips at 0x000000:\n");
	flash_mtd = do_map_probe("cfi_probe", &soleng_flash_map);
	if (!flash_mtd) {
		/* Not there. Try swapping */
		printk(KERN_NOTICE "Probing for flash chips at 0x400000:\n");
		soleng_flash_map.map_priv_1 = P2SEGADDR(0x400000);
		soleng_eprom_map.map_priv_1 = P1SEGADDR(0);
		flash_mtd = do_map_probe("cfi_probe", &soleng_flash_map);
		if (!flash_mtd) {
			/* Eep. */
			printk(KERN_NOTICE "Flash chips not detected at either possible location.\n");
			return -ENXIO;
		}
	}
	printk(KERN_NOTICE "Solution Engine: Flash at 0x%08lx, EPROM at 0x%08lx\n",
	       soleng_flash_map.map_priv_1 & 0x1fffffff,
	       soleng_eprom_map.map_priv_1 & 0x1fffffff);
	flash_mtd->module = THIS_MODULE;

	eprom_mtd = do_map_probe("map_rom", &soleng_eprom_map);
	if (eprom_mtd) {
		eprom_mtd->module = THIS_MODULE;
		add_mtd_device(eprom_mtd);
	}

	nr_parts = parse_redboot_partitions(flash_mtd, &parsed_parts);

	if (nr_parts)
		add_mtd_partitions(flash_mtd, parsed_parts, nr_parts);
	else
		add_mtd_device(flash_mtd);

	return 0;
}

static void __exit cleanup_soleng_maps(void)
{
	if (eprom_mtd) {
		del_mtd_device(eprom_mtd);
		map_destroy(eprom_mtd);
	}

	if (parsed_parts)
		del_mtd_partitions(flash_mtd);
	else
		del_mtd_device(flash_mtd);
	map_destroy(flash_mtd);
}

module_init(init_soleng_maps);
module_exit(cleanup_soleng_maps);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("MTD map driver for Hitachi SolutionEngine (and similar) boards");

