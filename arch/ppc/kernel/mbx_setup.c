/*
 * $Id: mbx_setup.c,v 1.5 1998/12/29 18:55:07 cort Exp $
 *
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
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
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/mbx.h>

extern unsigned long loops_per_sec;

unsigned long empty_zero_page[1024];

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern char saved_command_line[256];

extern unsigned long find_available_memory(void);
extern void m8xx_cpm_reset(uint);

/* this really does make things cleaner -- Cort */
void __init powermac_init(void)
{
}

void __init adbdev_init(void)
{
}

void __init mbx_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	ide_ioreg_t port = base;
	int i = 8;

	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
#ifdef ATA_FLASH
	base = (unsigned long) ioremap(PCMCIA_MEM_ADDR, 0x200);
	for (i = 0; i < 8; ++i)
		*p++ = base++;
	*p = ++base;		/* Does not matter */
	if (irq)
		*irq = 13;
#endif
}

__initfunc(void
mbx_setup_arch(unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	int	cpm_page;
	extern char cmd_line[];
	
	cpm_page = *memory_start_p;
	*memory_start_p += PAGE_SIZE;
	
	sprintf(cmd_line,
"%s root=/dev/nfs nfsroot=/sys/mbxroot",
		cmd_line);
	printk("Boot arguments: %s\n", cmd_line);

	/* Reset the Communication Processor Module.
	*/
	m8xx_cpm_reset(cpm_page);

#ifdef notdef
	ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
#endif
	
#ifdef CONFIG_BLK_DEV_INITRD
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

#ifdef notdef
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
#endif
}

void
abort(void)
{
#ifdef CONFIG_XMON
	extern void xmon(void *);
	xmon(0);
#endif
	machine_restart(NULL);
}
