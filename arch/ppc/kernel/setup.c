/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 */

/*
 * bootup setup stuff..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#include <asm/pgtable.h>

extern unsigned long *end_of_DRAM;
extern PTE *Hash;
extern unsigned long Hash_size, Hash_mask;

char sda_root[] = "root=/dev/sda1";
extern int root_mountflags;

unsigned char aux_device_present;
#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a EGA+ colour display.
 */
 /* this is changed only in minor ways from the original
        -- Cort
 */
struct screen_info screen_info = {
	0, 25,			/* orig-x, orig-y */
	{ 0, 0 },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
};


unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

unsigned long find_end_of_memory(void)
{
	unsigned char dram_size = inb(0x0804);
	unsigned long total;
	extern BAT BAT2;
printk("DRAM Size = %x\n", dram_size);
printk("Config registers = %x/%x/%x/%x\n", inb(0x0800), inb(0x0801), inb(0x0802), inb(0x0803));
	switch (dram_size & 0x07)
	{
		case 0:
			total = 0x08000000;  /* 128M */
			break;
		case 1:
			total = 0x02000000;  /* 32M */
			break;
		case 2:
			total = 0x00800000;  /* 8M */
			break;
		case 3:
			total = 0x00400000;  /* 4M - can't happen! */
			break;
		case 4:
			total = 0x10000000;  /* 256M */
			break;
		case 5:
			total = 0x04000000;  /* 64M */
			break;
		case 6:
			total = 0x01000000;  /* 16M */
			break;
		case 7:
			total = 0x04000000;  /* Can't happen */
			break;
	}
	switch ((dram_size>>4) & 0x07)
	{
		case 0:
			total += 0x08000000;  /* 128M */
			break;
		case 1:
			total += 0x02000000;  /* 32M */
			break;
		case 2:
			total += 0x00800000;  /* 8M */
			break;
		case 3:
			total += 0x00000000;  /* Module not present */
			break;
		case 4:
			total += 0x10000000;  /* 256M */
			break;
		case 5:
			total += 0x04000000;  /* 64M */
			break;
		case 6:
			total += 0x01000000;  /* 16M */
			break;
		case 7:
			total += 0x00000000;  /* Module not present */
			break;
	}
/* TEMP */ total = 0x01000000;
/* _cnpause();	*/
/* CAUTION!! This can be done more elegantly! */	
	if (total < 0x01000000)
	{
		Hash_size = HASH_TABLE_SIZE_64K;
		Hash_mask = HASH_TABLE_MASK_64K;
	} else
	{
		Hash_size = HASH_TABLE_SIZE_128K;
		Hash_mask = HASH_TABLE_MASK_128K;
	}
	switch(total)
	{
	  case 0x01000000:
/*	    BAT2[0][1] = BL_16M;*/
	    break;
	  default:
	    printk("WARNING: setup.c: find_end_of_memory() unknown total ram size %x\n", total);
	    break;
	}
	
	Hash = (PTE *)((total-Hash_size)+KERNELBASE);
	bzero(Hash, Hash_size);
	return ((unsigned long)Hash);
}

int size_memory;

/* #define DEFAULT_ROOT_DEVICE 0x0200	/* fd0 */
#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 */

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	extern int _end;
	extern char cmd_line[];
	unsigned char reg;

	/* Set up floppy in PS/2 mode */
	outb(0x09, SIO_CONFIG_RA);
	reg = inb(SIO_CONFIG_RD);
	reg = (reg & 0x3F) | 0x40;
	outb(reg, SIO_CONFIG_RD);
	outb(reg, SIO_CONFIG_RD);	/* Have to write twice to change! */
	ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);
        /*ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255);*/	/* nfs */
	aux_device_present = 0xaa;
  /*nfsaddrs=myip:serverip:gateip:netmaskip:clientname*/
  strcpy(cmd_line,
    "nfsaddrs=129.138.6.13:129.138.6.90:129.138.6.1:255.255.255.0:pandora");
  /*  strcpy(cmd_line,"root=/dev/sda1");*/
	*cmdline_p = cmd_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = (unsigned long *)end_of_DRAM;
	size_memory = *memory_end_p - KERNELBASE;  /* Relative size of memory */

#ifdef CONFIG_BLK_DEV_RAM
  rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
  rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
  rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
  rd_prompt = 0;
  rd_doload = 0;
  rd_image_start = 0;
#endif  	
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}
#if 0
extern char builtin_ramdisk_image;
extern long builtin_ramdisk_size;

void
builtin_ramdisk_init(void)
{
	if ((ROOT_DEV == to_kdev_t(DEFAULT_ROOT_DEVICE)) && (builtin_ramdisk_size != 0))
	{
		rd_preloaded_init(&builtin_ramdisk_image, builtin_ramdisk_size);
	} else
	{  /* Not ramdisk - assume root needs to be mounted read only */
		root_mountflags |= MS_RDONLY;
	}
}
#endif
#define MAJOR(n) (((n)&0xFF00)>>8)
#define MINOR(n) ((n)&0x00FF)

int
get_cpuinfo(char *buffer)
{
	int pvr = _get_PVR();
	char *model;
	switch (pvr>>16)
	{
		case 3:
			model = "603";
			break;
		case 4:
			model = "604";
			break;
		case 6:
			model = "603e";
			break;
		case 7:
			model = "603ev";
			break;
		default:
			model = "unknown";
			break;
	}
	return sprintf(buffer, "PowerPC %s rev %d.%d\n", model, MAJOR(pvr), MINOR(pvr));
}
