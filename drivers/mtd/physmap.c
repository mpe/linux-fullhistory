/*
 * $Id: physmap.c,v 1.1 2000/07/04 08:58:10 dwmw2 Exp $
 *
 * Normal mappings of chips in physical memory
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>


#define WINDOW_ADDR 0x8000000
#define WINDOW_SIZE 0x4000000

static struct mtd_info *mymtd;

__u8 physmap_read8(struct map_info *map, unsigned long ofs)
{
	return readb(map->map_priv_1 + ofs);
}

__u16 physmap_read16(struct map_info *map, unsigned long ofs)
{
	return readw(map->map_priv_1 + ofs);
}

__u32 physmap_read32(struct map_info *map, unsigned long ofs)
{
	return readl(map->map_priv_1 + ofs);
}

void physmap_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy_fromio(to, map->map_priv_1 + from, len);
}

void physmap_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	writeb(d, map->map_priv_1 + adr);
}

void physmap_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	writew(d, map->map_priv_1 + adr);
}

void physmap_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	writel(d, map->map_priv_1 + adr);
}

void physmap_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy_toio(map->map_priv_1 + to, from, len);
}

struct map_info physmap_map = {
	"Physically mapped flash",
	WINDOW_SIZE,
	2,
	physmap_read8,
	physmap_read16,
	physmap_read32,
	physmap_copy_from,
	physmap_write8,
	physmap_write16,
	physmap_write32,
	physmap_copy_to,
	0,
	0
};

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_physmap init_module
#define cleanup_physmap cleanup_module
#endif
#endif

int __init init_physmap(void)
{
       	printk(KERN_NOTICE "physmap flash device: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);
	physmap_map.map_priv_1 = (unsigned long)ioremap(WINDOW_SIZE, WINDOW_ADDR);

	if (!physmap_map.map_priv_1) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	mymtd = do_cfi_probe(&physmap_map);
	if (mymtd) {
#ifdef MODULE
		mymtd->module = &__this_module;
#endif
		add_mtd_device(mymtd);
		return 0;
	}

	return -ENXIO;
}

static void __exit cleanup_physmap(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (physmap_map.map_priv_1) {
		iounmap((void *)physmap_map.map_priv_1);
		physmap_map.map_priv_1 = 0;
	}
}
