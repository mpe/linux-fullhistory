/*
 *	pnc2000.c - mapper for Photron PNC-2000 board.
 *
 * Copyright (C) 2000 Crossnet Co. <info@crossnet.co.jp>
 *
 * This code is GPL
 *
 * $Id: pnc2000.c,v 1.1 2000/07/12 09:34:32 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>


#define WINDOW_ADDR 0xbf000000
#define WINDOW_SIZE 0x00400000

/* 
 * MAP DRIVER STUFF
 */

__u8 pnc_read8(struct map_info *map, unsigned long ofs)
{
  return *(__u8 *)(WINDOW_ADDR + ofs);
}

__u16 pnc_read16(struct map_info *map, unsigned long ofs)
{
  return *(__u16 *)(WINDOW_ADDR + ofs);
}

__u32 pnc_read32(struct map_info *map, unsigned long ofs)
{
  return *(volatile unsigned int *)(WINDOW_ADDR + ofs);
}

void pnc_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
  memcpy(to, (void *)(WINDOW_ADDR + from), len);
}

void pnc_write8(struct map_info *map, __u8 d, unsigned long adr)
{
  *(__u8 *)(WINDOW_ADDR + adr) = d;
}

void pnc_write16(struct map_info *map, __u16 d, unsigned long adr)
{
  *(__u16 *)(WINDOW_ADDR + adr) = d;
}

void pnc_write32(struct map_info *map, __u32 d, unsigned long adr)
{
  *(__u32 *)(WINDOW_ADDR + adr) = d;
}

void pnc_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
  memcpy((void *)(WINDOW_ADDR + to), from, len);
}

struct map_info pnc_map = {
  "PNC-2000",
  WINDOW_SIZE,
  4,
  pnc_read8,
  pnc_read16,
  pnc_read32,
  pnc_copy_from,
  pnc_write8,
  pnc_write16,
  pnc_write32,
  pnc_copy_to,
  0,
  0
};


/*
 * MTD 'PARTITIONING' STUFF 
 */

/* 
 * This is the _real_ MTD device for which all the others are just
 * auto-relocating aliases.
 */
static struct mtd_info *mymtd;

/* 
 * MTD methods which simply translate the effective address and pass through
 * to the _real_ device.
 */

static int pnc_mtd_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	return mymtd->read(mymtd, from + (unsigned long)mtd->priv, len, retlen, buf);
}

static int pnc_mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	return mymtd->write(mymtd, to + (unsigned long)mtd->priv, len, retlen, buf);
}

static int pnc_mtd_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	instr->addr += (unsigned long)mtd->priv;
	return mymtd->erase(mymtd, instr);
}

static void pnc_mtd_sync (struct mtd_info *mtd)
{
	mymtd->sync(mymtd);
}

static int pnc_mtd_suspend (struct mtd_info *mtd)
{
	return mymtd->suspend(mymtd);
}

static void pnc_mtd_resume (struct mtd_info *mtd)
{
	mymtd->resume(mymtd);
}


static struct mtd_info pnc_mtds[3] = {  /* boot, kernel, fs */
	{
		type: MTD_NORFLASH,
		flags: MTD_CAP_NORFLASH,
		size: 0x20000,
		erasesize: 0x20000,
		name: "PNC-2000 boot firmware",
		module: THIS_MODULE,
		erase: pnc_mtd_erase,
		read: pnc_mtd_read,
		write: pnc_mtd_write,
		suspend: pnc_mtd_suspend,
		resume: pnc_mtd_resume,
		sync: pnc_mtd_sync,
		priv: (void *)0
	},
	{
		type: MTD_NORFLASH,
		flags: MTD_CAP_NORFLASH,
		size: 0x1a0000,
		erasesize: 0x20000,
		name: "PNC-2000 kernel",
		module: THIS_MODULE,
		erase: pnc_mtd_erase,
		read: pnc_mtd_read,
		write: pnc_mtd_write,
		suspend: pnc_mtd_suspend,
		resume: pnc_mtd_resume,
		sync: pnc_mtd_sync,
		priv: (void *)0x20000
	},
	{
		type: MTD_NORFLASH,
		flags: MTD_CAP_NORFLASH,
		size: 0x240000,
		erasesize: 0x20000,
		name: "PNC-2000 filesystem",
		module: THIS_MODULE,
		erase: pnc_mtd_erase,
		read: pnc_mtd_read,
		write: pnc_mtd_write,
		suspend: pnc_mtd_suspend,
		resume: pnc_mtd_resume,
		sync: pnc_mtd_sync,
		priv: (void *)0x1c0000
	}
};

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_pnc init_module
#define cleanup_pnc cleanup_module
#endif
#endif

int __init init_pnc(void)
{
       	printk(KERN_NOTICE "Photron PNC-2000 flash mapping: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);

	mymtd = do_cfi_probe(&pnc_map);
	if (mymtd) {
		mymtd->module = THIS_MODULE;
		
		add_mtd_device(&pnc_mtds[0]); /* boot */
		add_mtd_device(&pnc_mtds[1]); /* kernel */
		add_mtd_device(&pnc_mtds[2]); /* file system */
		return 0;
	}

	return -ENXIO;
}

static void __exit cleanup_pnc(void)
{
	if (mymtd) {
		del_mtd_device(&pnc_mtds[2]);
		del_mtd_device(&pnc_mtds[1]);
		del_mtd_device(&pnc_mtds[0]);
		map_destroy(mymtd);
	}
}
