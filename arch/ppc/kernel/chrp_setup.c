/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
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
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pgtable.h>

/* for the mac fs */
kdev_t boot_dev;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_sec;

unsigned long empty_zero_page[1024];
extern unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif


extern char saved_command_line[256];

long TotalMemory;

int
chrp_get_cpuinfo(char *buffer)
{
	int pvr = _get_PVR();
	int len;
	char *model;

	switch (pvr>>16)
	{
	case 1:
		model = "601";
		break;
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
	case 9:
		model = "604e";
		break;
	default:
		model = "unknown";
		break;
	}

	len = sprintf(buffer, "PowerPC %s rev %d.%d\n", model,
		      (pvr & 0xff00) >> 8, pvr & 0xff);

	len += sprintf(buffer+len, "bogomips\t: %lu.%02lu\n",
		       (loops_per_sec+2500)/500000,
		       ((loops_per_sec+2500)/5000) % 100);

#if 0
	/*
	 * Ooh's and aah's info about zero'd pages in idle task
	 */
	{
		extern unsigned int zerocount, zerototal, zeropage_hits,zeropage_calls;
		len += sprintf(buffer+len,"zero pages\t: total %u (%uKb) "
			       "current: %u (%uKb) hits: %u/%u (%lu%%)\n",
			       zerototal, (zerototal*PAGE_SIZE)>>10,
			       zerocount, (zerocount*PAGE_SIZE)>>10,
			       zeropage_hits,zeropage_calls,
			       /* : 1 below is so we don't div by zero */
			       (zeropage_hits*100) /
				    ((zeropage_calls)?zeropage_calls:1));
	}
#endif
	return len;
}

__initfunc(void
chrp_setup_arch(char **cmdline_p, unsigned long * memory_start_p,
	   unsigned long * memory_end_p))
{
	extern char cmd_line[];
	extern char _etext[], _edata[], _end[];
	extern int panic_timeout;

	/* Save unparsed command line copy for /proc/cmdline */
	strcpy( saved_command_line, cmd_line );
	*cmdline_p = cmd_line;

	*memory_start_p = (unsigned long) Hash+Hash_size;
	(unsigned long *)*memory_end_p = (unsigned long *)(TotalMemory+KERNELBASE);

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_sec = 50000000;
	
	/* reboot on panic */	
	panic_timeout = 180;
	
	init_task.mm->start_code = PAGE_OFFSET;
	init_task.mm->end_code = (unsigned long) _etext;
	init_task.mm->end_data = (unsigned long) _edata;
	init_task.mm->brk = (unsigned long) _end;	
	
	aux_device_present = 0xaa;
	
	switch ( _machine )
	{
	case _MACH_chrp:
		ROOT_DEV = to_kdev_t(0x0801); /* sda1 */
		break;
	}
	
#ifdef CONFIG_BLK_DEV_RAM
#if 0
	ROOT_DEV = to_kdev_t(0x0200); /* floppy */
	rd_prompt = 1;
	rd_doload = 1;
	rd_image_start = 0;
#endif
	/* initrd_start and size are setup by boot/head.S and kernel/head.S */
	if ( initrd_start )
	{
		if (initrd_end > *memory_end_p)
		{
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,*memory_end_p);
			initrd_start = 0;
		}
	}
#endif

	printk("Boot arguments: %s\n", cmd_line);
	
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}
