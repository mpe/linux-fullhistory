/*
 *  linux/arch/sh/kernel/setup.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <asm/processor.h>
#include <linux/console.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>

/*
 * Machine setup..
 */

struct sh_cpuinfo boot_cpu_data = { 0, 0, 0, 0, };
extern int _text, _etext, _edata, _end, _stext, __bss_start;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern int root_mountflags;

#define COMMAND_LINE_SIZE 1024
static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

extern unsigned char *root_fs_image;

struct resource standard_io_resources[] = {
	{ "dma1", 0x00, 0x1f },
	{ "pic1", 0x20, 0x3f },
	{ "timer", 0x40, 0x5f },
	{ "keyboard", 0x60, 0x6f },
	{ "dma page reg", 0x80, 0x8f },
	{ "pic2", 0xa0, 0xbf },
	{ "dma2", 0xc0, 0xdf },
	{ "fpu", 0xf0, 0xff }
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))


/* System RAM - interrupted by the 640kB-1M hole */
#define code_resource (ram_resources[3])
#define data_resource (ram_resources[4])
static struct resource ram_resources[] = {
	{ "System RAM", 0x000000, 0x09ffff, IORESOURCE_BUSY },
	{ "System RAM", 0x100000, 0x100000, IORESOURCE_BUSY },
	{ "Video RAM area", 0x0a0000, 0x0bffff },
	{ "Kernel code", 0x100000, 0 },
	{ "Kernel data", 0, 0 }
};

/* System ROM resources */
#define MAXROMS 6
static struct resource rom_resources[MAXROMS] = {
	{ "System ROM", 0xF0000, 0xFFFFF, IORESOURCE_BUSY },
	{ "Video ROM", 0xc0000, 0xc7fff }
};


void __init setup_arch(char **cmdline_p,
		       unsigned long * memory_start_p,
		       unsigned long * memory_end_p)
{
	*cmdline_p = command_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = 0x8c400000; /* For my board. */
	ram_resources[1].end = *memory_end_p-1;

	init_mm.start_code = (unsigned long)&_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_bus(&_text);
	code_resource.end = virt_to_bus(&_etext)-1;
	data_resource.start = virt_to_bus(&_etext);
	data_resource.end = virt_to_bus(&_edata)-1;

 	ROOT_DEV = MKDEV(FLOPPY_MAJOR, 0);

	initrd_below_start_ok = 1;
	initrd_start = (long)&root_fs_image;
	initrd_end = (long)&__bss_start;
	mount_initrd = 1;


#if 0
	/* Request the standard RAM and ROM resources - they eat up PCI memory space */
	request_resource(&iomem_resource, ram_resources+0);
	request_resource(&iomem_resource, ram_resources+1);
	request_resource(&iomem_resource, ram_resources+2);
	request_resource(ram_resources+1, &code_resource);
	request_resource(ram_resources+1, &data_resource);
#endif

#if 0
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources+i);
#endif

#if 0
	rd_image_start = (long)root_fs_image;
	rd_prompt = 0;
	rd_doload = 1;
#endif

#if 0
 	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
#endif

#ifdef CONFIG_BLK_DEV_INITRD
#if 0
	if (LOADER_TYPE) {
		initrd_start = INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
		initrd_end = initrd_start+INITRD_SIZE;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    initrd_end,memory_end);
			initrd_start = 0;
		}
	}
#endif

#endif
}

/*
 *	Get CPU information for use by the procfs.
 */

int get_cpuinfo(char *buffer)
{
	char *p = buffer;

#ifdef CONFIG_CPU_SH3
	p += sprintf(p,"cpu family\t: SH3\n"
		       "cache size\t: 8K-byte\n");
#elif CONFIG_CPU_SH4
	p += sprintf(p,"cpu family\t: SH4\n"
		       "cache size\t: ??K-byte\n");
#endif
	p += sprintf(p, "bogomips\t: %lu.%02lu\n\n",
		     (loops_per_sec+2500)/500000,
		     ((loops_per_sec+2500)/5000) % 100);

	return p - buffer;
}
