/*
 * Common code to handle map devices which are simple ROM
 * (C) 2000 Red Hat. GPL'd.
 * $Id: map_rom.c,v 1.2 2000/07/03 10:01:38 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#include <linux/mtd/map.h>


static int maprom_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static void maprom_nop (struct mtd_info *);

struct mtd_info *map_rom_probe(struct map_info *);
EXPORT_SYMBOL(map_rom_probe);

struct mtd_info *map_rom_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	memset(mtd, 0, sizeof(*mtd));

	map->fldrv_destroy = maprom_nop;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_ROM;
	mtd->size = map->size;
	mtd->read = maprom_read;
	mtd->sync = maprom_nop;
	mtd->flags = MTD_CAP_ROM;
	
	MOD_INC_USE_COUNT;
	return mtd;
}


static int maprom_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = (struct map_info *)mtd->priv;

	map->copy_from(map, buf, from, len);
	*retlen = len;
	return 0;
}

static void maprom_nop(struct mtd_info *mtd)
{
	/* Nothing to see here */
}
