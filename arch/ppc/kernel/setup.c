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

#include <asm/pgtable.h>
extern PTE *Hash;
extern unsigned long Hash_size, Hash_mask;

char sda_root[] = "root=/dev/sda1";

unsigned char aux_device_present;
int get_cpuinfo(char *buffer)
{
}
/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a EGA+ colour display.
 */
struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	0, 0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25			/* orig-video-lines */
};

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

unsigned long find_end_of_memory(void)
{
	unsigned char dram_size = inb(0x0804);
	unsigned long total;
_printk("DRAM Size = %x\n", dram_size);
_printk("Config registers = %x/%x/%x\n", inb(0x0800), inb(0x0801), inb(0x0802));
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
_printk("register total = %08X\n", total);	
/* TEMP */ total = 0x01000000;
/*_cnpause();	*/
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
	Hash = (PTE *)((total-Hash_size)+KERNELBASE);
	bzero(Hash, Hash_size);
	return ((unsigned long)Hash);
}

int size_memory;

#define DEFAULT_ROOT_DEVICE 0x0200	/* fd0 */

#define COMMAND_LINE_SIZE 512	/* Should match head.S */
char saved_command_line[COMMAND_LINE_SIZE];

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{

  extern int _end;
	extern char cmd_line[];

	ROOT_DEV = DEFAULT_ROOT_DEVICE;
	aux_device_present = 0xaa;
	strcpy(saved_command_line, cmd_line);
	*cmdline_p = cmd_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = (unsigned long *)Hash;
	size_memory = *memory_end_p - KERNELBASE;  /* Relative size of memory */

/*	_printk("setup_arch() done!  memory_start = %08X memory_end = %08X\n"
		,*memory_start_p,*memory_end_p);*/

}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}

extern char builtin_ramdisk_image;
extern long builtin_ramdisk_size;

void
builtin_ramdisk_init(void)
{
	if (ROOT_DEV == DEFAULT_ROOT_DEVICE)
	{
		rd_preloaded_init(&builtin_ramdisk_image, builtin_ramdisk_size);
	}
}

