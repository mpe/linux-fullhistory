/*
 * $Id: rpxlite.c,v 1.2 2000/07/04 12:16:26 dwmw2 Exp $
 *
 * Handle the strange 16-in-32-bit mapping on the RPXLite board
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>


#define WINDOW_ADDR 0x8000000
#define WINDOW_SIZE 0x2000000

#define MAP_TO_ADR(x) ( ( ( x & ~1 ) << 1 ) | (x&1) )

static struct mtd_info *mymtd;

__u8 rpxlite_read8(struct map_info *map, unsigned long ofs)
{
	return readb(map->map_priv_1 + MAP_TO_ADR(ofs));
}

__u16 rpxlite_read16(struct map_info *map, unsigned long ofs)
{
	return readw(map->map_priv_1 + MAP_TO_ADR(ofs));
}

__u32 rpxlite_read32(struct map_info *map, unsigned long ofs)
{
	return readl(map->map_priv_1 + MAP_TO_ADR(ofs));
}

void rpxlite_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	if (from & 1) {
		*(__u8 *)to = readb(map->map_priv_1 + MAP_TO_ADR(from));
		from++;
		len--;
	}
	/* Can't do this if it's not aligned */
	if (!((unsigned long)to & 1)) {
		unsigned long fromadr = MAP_TO_ADR(from);

		while (len > 1) {
			*(__u16 *)to = readw(map->map_priv_1 + fromadr);
			to += 2;
			fromadr += 4;
			from += 2;
			len -= 2;
		}
	}
	while(len) {
		*(__u8 *)to = readb(map->map_priv_1 + MAP_TO_ADR(from));
		to++;
		from++;
		len--;
	}
}

void rpxlite_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	writeb(d, map->map_priv_1 + MAP_TO_ADR(adr));
}

void rpxlite_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	writew(d, map->map_priv_1 + MAP_TO_ADR(adr));
}

void rpxlite_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	writel(d, map->map_priv_1 + MAP_TO_ADR(adr));
}

void rpxlite_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	if (to & 1) {
		writeb(*(__u8 *)from, map->map_priv_1 + MAP_TO_ADR(to));
		from++;
		len--;
	}
	/* Can't do this if it's not aligned */
	if (!((unsigned long)from & 1)) {
		unsigned long toadr = map->map_priv_1 + MAP_TO_ADR(to);

		while (len > 1) {
			writew(*(__u16 *)from, toadr);
			from += 2;
			toadr += 4;
			to += 2;
			len -= 2;
		}
	}
	while(len) {
		writeb(*(__u8 *)from, map->map_priv_1 + MAP_TO_ADR(to));
		to++;
		from++;
		len--;
	}
}

struct map_info rpxlite_map = {
	"RPXLITE",
	WINDOW_SIZE,
	2,
	rpxlite_read8,
	rpxlite_read16,
	rpxlite_read32,
	rpxlite_copy_from,
	rpxlite_write8,
	rpxlite_write16,
	rpxlite_write32,
	rpxlite_copy_to,
	0,
	0
};

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_rpxlite init_module
#define cleanup_rpxlite cleanup_module
#endif
#endif

int __init init_rpxlite(void)
{
       	printk(KERN_NOTICE "rpxlite flash device: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);
	rpxlite_map.map_priv_1 = (unsigned long)ioremap(WINDOW_ADDR, WINDOW_SIZE * 2);

	if (!rpxlite_map.map_priv_1) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	mymtd = do_cfi_probe(&rpxlite_map);
	if (mymtd) {
#ifdef MODULE
		mymtd->module = &__this_module;
#endif
		add_mtd_device(mymtd);
		return 0;
	}

	return -ENXIO;
}

static void __exit cleanup_rpxlite(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (rpxlite_map.map_priv_1) {
		iounmap((void *)rpxlite_map.map_priv_1);
		rpxlite_map.map_priv_1 = 0;
	}
}
