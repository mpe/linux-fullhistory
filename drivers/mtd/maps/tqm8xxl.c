/*
 * Handle mapping of the flash memory access routines 
 * on TQM8xxL based devices.
 *
 * $Id: tqm8xxl.c,v 1.3 2001/10/02 15:05:14 dwmw2 Exp $
 *
 * based on rpxlite.c
 *
 * Copyright(C) 2001 Kirk Lee <kirk@hpc.ee.ntu.edu.tw>
 *
 * This code is GPLed
 * 
 */

/*
 * According to TQM8xxL hardware manual, TQM8xxL series have
 * following flash memory organisations:
 *	| capacity |	| chip type |	| bank0 |	| bank1 |
 *	    2MiB	   512Kx16	  2MiB		   0
 *	    4MiB	   1Mx16	  4MiB		   0
 *	    8MiB	   1Mx16	  4MiB		   4MiB
 * Thus, we choose CONFIG_MTD_CFI_I2 & CONFIG_MTD_CFI_B4 at 
 * kernel configuration.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define FLASH_ADDR 0x40000000
#define FLASH_SIZE 0x00800000
#define FLASH_BANK_MAX 4

// trivial struct to describe partition information
struct mtd_part_def
{
	int nums;
	unsigned char *type;
	struct mtd_partition* mtd_part;
};

//static struct mtd_info *mymtd;
static struct mtd_info* mtd_banks[FLASH_BANK_MAX];
static struct map_info* map_banks[FLASH_BANK_MAX];
static struct mtd_part_def part_banks[FLASH_BANK_MAX];
static unsigned long num_banks;
static unsigned long start_scan_addr;

__u8 tqm8xxl_read8(struct map_info *map, unsigned long ofs)
{
	return *((__u8 *)(map->map_priv_1 + ofs));
}

__u16 tqm8xxl_read16(struct map_info *map, unsigned long ofs)
{
	return *((__u16 *)(map->map_priv_1 + ofs));
}

__u32 tqm8xxl_read32(struct map_info *map, unsigned long ofs)
{
	return *((__u32 *)(map->map_priv_1 + ofs));
}

void tqm8xxl_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy_fromio(to, (void *)(map->map_priv_1 + from), len);
}

void tqm8xxl_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	*((__u8 *)(map->map_priv_1 + adr)) = d;
}

void tqm8xxl_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	*((__u16 *)( map->map_priv_1 + adr)) = d;
}

void tqm8xxl_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	*((__u32 *)(map->map_priv_1 + adr)) = d;
}

void tqm8xxl_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy_toio((void *)(map->map_priv_1 + to), from, len);
}

struct map_info tqm8xxl_map = {
	name: "TQM8xxL",
	//size: WINDOW_SIZE,
	buswidth: 4,
	read8: tqm8xxl_read8,
	read16: tqm8xxl_read16,
	read32: tqm8xxl_read32,
	copy_from: tqm8xxl_copy_from,
	write8: tqm8xxl_write8,
	write16: tqm8xxl_write16,
	write32: tqm8xxl_write32,
	copy_to: tqm8xxl_copy_to
};

/*
 * Here are partition information for all known TQM8xxL series devices.
 * See include/linux/mtd/partitions.h for definition of the mtd_partition
 * structure.
 * 
 * The *_max_flash_size is the maximum possible mapped flash size which
 * is not necessarily the actual flash size.  It must correspond to the 
 * value specified in the mapping definition defined by the
 * "struct map_desc *_io_desc" for the corresponding machine.
 */

#ifdef CONFIG_MTD_PARTITIONS
/* Currently, TQM8xxL has upto 8MiB flash */
static unsigned long tqm8xxl_max_flash_size = 0x00800000;

/* partition definition for first flash bank
 * also ref. to "drivers\char\flash_config.c" 
 */
static struct mtd_partition tqm8xxl_partitions[] = {
	{
	  name: "ppcboot",
	  offset: 0x00000000,
	  size: 0x00020000,           /* 128KB           */
	  mask_flags: MTD_WRITEABLE,  /* force read-only */
	},
	{
	  name: "kernel",             /* default kernel image */
	  offset: 0x00020000,
	  size: 0x000e0000,
	  mask_flags: MTD_WRITEABLE,  /* force read-only */
	},
	{
	  name: "user",
	  offset: 0x00100000,
	  size: 0x00100000,
	},
	{
	  name: "initrd",
	  offset: 0x00200000,
	  size: 0x00200000,
	}
};
/* partition definition for second flahs bank */
static struct mtd_partition tqm8xxl_fs_partitions[] = {
	{
	  name: "cramfs",
	  offset: 0x00000000,
	  size: 0x00200000,
	},
	{
	  name: "jffs",
	  offset: 0x00200000,
	  size: 0x00200000,
	  //size: MTDPART_SIZ_FULL,
	}
};
#endif

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

int __init init_tqm_mtd(void)
{
	int idx = 0, ret = 0;
	unsigned long flash_addr, flash_size, mtd_size = 0;
	/* pointer to TQM8xxL board info data */
	bd_t *bd = (bd_t *)__res;

	flash_addr = bd->bi_flashstart;
	flash_size = bd->bi_flashsize;
	//request maximum flash size address spzce
	start_scan_addr = (unsigned long)ioremap(flash_addr, flash_size);
	if (!start_scan_addr) {
		//printk("%s:Failed to ioremap address:0x%x\n", __FUNCTION__, FLASH_ADDR);
		printk("%s:Failed to ioremap address:0x%x\n", __FUNCTION__, flash_addr);
		return -EIO;
	}
	for(idx = 0 ; idx < FLASH_BANK_MAX ; idx++)
	{
		if(mtd_size >= flash_size)
			break;
		
		printk("%s: chip probing count %d\n", __FUNCTION__, idx);
		
		map_banks[idx] = (struct map_info *)kmalloc(sizeof(struct map_info), GFP_KERNEL);
		if(map_banks[idx] == NULL)
		{
			//return -ENOMEM;
			ret = -ENOMEM;
			goto error_mem;
		}
		memset((void *)map_banks[idx], 0, sizeof(struct map_info));
		map_banks[idx]->name = (char *)kmalloc(16, GFP_KERNEL);
		if(map_banks[idx]->name == NULL)
		{
			//return -ENOMEM;
			ret = -ENOMEM;
			goto error_mem;
		}
		memset((void *)map_banks[idx]->name, 0, 16);
		
		sprintf(map_banks[idx]->name, "TQM8xxL%d", idx);
		map_banks[idx]->buswidth = 4;
		map_banks[idx]->read8 = tqm8xxl_read8;
		map_banks[idx]->read16 = tqm8xxl_read16;
		map_banks[idx]->read32 = tqm8xxl_read32;
		map_banks[idx]->copy_from = tqm8xxl_copy_from;
		map_banks[idx]->write8 = tqm8xxl_write8;
		map_banks[idx]->write16 = tqm8xxl_write16;
		map_banks[idx]->write32 = tqm8xxl_write32;
		map_banks[idx]->copy_to = tqm8xxl_copy_to;
		map_banks[idx]->map_priv_1 = 
		start_scan_addr + ((idx > 0) ? 
		(mtd_banks[idx-1] ? mtd_banks[idx-1]->size : 0) : 0);
		//start to probe flash chips
		mtd_banks[idx] = do_map_probe("cfi_probe", map_banks[idx]);
		if(mtd_banks[idx])
		{
			mtd_banks[idx]->module = THIS_MODULE;
			mtd_size += mtd_banks[idx]->size;
			num_banks++;
			printk("%s: bank%d, name:%s, size:%dbytes \n", __FUNCTION__, num_banks, 
			mtd_banks[idx]->name, mtd_banks[idx]->size);
		}
	}

	/* no supported flash chips found */
	if(!num_banks)
	{
		printk("TQM8xxL: No support flash chips found!\n");
		ret = -ENXIO;
		goto error_mem;
	}

#ifdef CONFIG_MTD_PARTITIONS
	/*
	 * Select Static partition definitions
	 */
	part_banks[0].mtd_part = tqm8xxl_partitions;
	part_banks[0].type = "Static image";
	part_banks[0].nums = NB_OF(tqm8xxl_partitions);
	part_banks[1].mtd_part = tqm8xxl_fs_partitions;
	part_banks[1].type = "Static file system";
	part_banks[1].nums = NB_OF(tqm8xxl_fs_partitions);
	for(idx = 0; idx < num_banks ; idx++)
	{
		if (part_banks[idx].nums == 0) {
			printk(KERN_NOTICE "TQM flash%d: no partition info available, registering whole flash at once\n", idx);
			add_mtd_device(mtd_banks[idx]);
		} else {
			printk(KERN_NOTICE "TQM flash%d: Using %s partition definition\n",
					idx, part_banks[idx].type);
			add_mtd_partitions(mtd_banks[idx], part_banks[idx].mtd_part, 
								part_banks[idx].nums);
		}
	}
#else
	printk(KERN_NOTICE "TQM flash: registering %d whole flash banks at once\n", num_banks);
	for(idx = 0 ; idx < num_banks ; idx++)
		add_mtd_device(mtd_banks[idx]);
#endif
	return 0;
error_mem:
	for(idx = 0 ; idx < FLASH_BANK_MAX ; idx++)
	{
		if(map_banks[idx] != NULL)
		{
			if(map_banks[idx]->name != NULL)
			{
				kfree(map_banks[idx]->name);
				map_banks[idx]->name = NULL;
			}
			kfree(map_banks[idx]);
			map_banks[idx] = NULL;
		}
	}
	//return -ENOMEM;
error:
	iounmap((void *)start_scan_addr);
	//return -ENXIO;
	return ret;
}

static void __exit cleanup_tqm_mtd(void)
{
	unsigned int idx = 0;
	for(idx = 0 ; idx < num_banks ; idx++)
	{
		/* destroy mtd_info previously allocated */
		if (mtd_banks[idx]) {
			del_mtd_partitions(mtd_banks[idx]);
			map_destroy(mtd_banks[idx]);
		}
		/* release map_info not used anymore */
		kfree(map_banks[idx]->name);
		kfree(map_banks[idx]);
	}
	if (start_scan_addr) {
		iounmap((void *)start_scan_addr);
		start_scan_addr = 0;
	}
}

module_init(init_tqm_mtd);
module_exit(cleanup_tqm_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kirk Lee <kirk@hpc.ee.ntu.edu.tw>");
MODULE_DESCRIPTION("MTD map driver for TQM8xxL boards");
