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
#ifdef CONFIG_ABSTRACT_CONSOLE
#include <linux/console.h>
#endif

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>

#ifdef CONFIG_SOUND
#include <../drivers/sound/sound_config.h>
#include <../drivers/sound/dev_table.h>
#endif

/* for the mac fs */
kdev_t boot_dev;
/* used in nasty hack for sound - see prep_setup_arch() -- Cort */
long ppc_cs4232_dma, ppc_cs4232_dma2;
unsigned long empty_zero_page[1024];

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_sec;
extern unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

void prep_ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	ide_ioreg_t port = base;
	int i = 8;

	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
}


int
prep_get_cpuinfo(char *buffer)
{
	extern char *Motherboard_map_name;
	extern RESIDUAL res;
	int len, i;
  
#ifdef __SMP__
#define CD(X)		(cpu_data[n].X)  
#else
#define CD(X) (X)
#endif
  
	len = sprintf(buffer,"machine\t\t: PReP %s\n",Motherboard_map_name);
	
	if ( res.ResidualLength == 0 )
		return len;
	
	/* print info about SIMMs */
	len += sprintf(buffer+len,"simms\t\t: ");
	for ( i = 0 ; (res.ActualNumMemories) && (i < MAX_MEMS) ; i++ )
	{
		if ( res.Memories[i].SIMMSize != 0 )
			len += sprintf(buffer+len,"%d:%ldM ",i,
				       (res.Memories[i].SIMMSize > 1024) ?
				       res.Memories[i].SIMMSize>>20 :
				       res.Memories[i].SIMMSize);
	}
	len += sprintf(buffer+len,"\n");

#if 0	
	/* TLB */
	len += sprintf(buffer+len,"tlb\t\t:");
	switch(res.VitalProductData.TLBAttrib)
	{
	case CombinedTLB:
		len += sprintf(buffer+len," %ld entries\n",
			       res.VitalProductData.TLBSize);
		break;
	case SplitTLB:
		len += sprintf(buffer+len," (split I/D) %ld/%ld entries\n",
			       res.VitalProductData.I_TLBSize,
			       res.VitalProductData.D_TLBSize);
		break;
	case NoneTLB:
		len += sprintf(buffer+len," not present\n");
		break;
	}
	/* L1 */
	len += sprintf(buffer+len,"l1\t\t: ");
	switch(res.VitalProductData.CacheAttrib)
	{
	case CombinedCAC:
		len += sprintf(buffer+len,"%ldkB LineSize %ldB\n",
			       res.VitalProductData.CacheSize,
			       res.VitalProductData.CacheLineSize);
		break;
	case SplitCAC:
		len += sprintf(buffer+len,"(split I/D) %ldkB/%ldkB Linesize %ldB/%ldB\n",
			       res.VitalProductData.I_CacheSize,
			       res.VitalProductData.D_CacheSize,
			       res.VitalProductData.D_CacheLineSize,
			       res.VitalProductData.D_CacheLineSize);
		break;
	case NoneCAC:
		len += sprintf(buffer+len,"not present\n");
		break;
	}
#endif

	/* L2 */
	if ( (inb(IBM_EQUIP_PRESENT) & 1) == 0) /* l2 present */
	{
		len += sprintf(buffer+len,"l2\t\t: %dkB %s\n",
			       ((inb(IBM_L2_STATUS) >> 5) & 1) ? 512 : 256,
			       (inb(IBM_SYS_CTL) & 64) ? "enabled" : "disabled");
	}
	else
	{
		len += sprintf(buffer+len,"l2\t\t: not present\n");
	}

	return len;
}

__initfunc(void
prep_setup_arch(unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	extern char cmd_line[];
	unsigned char reg;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_sec = 50000000;
	
	aux_device_present = 0xaa;
	/* Set up floppy in PS/2 mode */
	outb(0x09, SIO_CONFIG_RA);
	reg = inb(SIO_CONFIG_RD);
	reg = (reg & 0x3F) | 0x40;
	outb(reg, SIO_CONFIG_RD);
	outb(reg, SIO_CONFIG_RD);	/* Have to write twice to change! */

	/* we should determine this according to what we find! -- Cort */
	switch ( _prep_type )
	{
	case _PREP_IBM:
		ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
		break;
	case _PREP_Motorola:
		ROOT_DEV = to_kdev_t(0x0801); /* sda1 */
		break;
	}
	
#ifdef CONFIG_BLK_DEV_RAM
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
	/* make the serial port the console */
	/* strcat(cmd_line,"console=ttyS0,9600n8"); */
	/* use the normal console but send output to the serial port, too */
	/*strcat(cmd_line,"console=tty0 console=ttyS0,9600n8");*/
        sprintf(cmd_line,"%s console=tty0 console=ttyS0,9600n8", cmd_line);
	printk("Boot arguments: %s\n", cmd_line);
	
#ifdef CONFIG_CS4232
	/*
	 * setup proper values for the cs4232 driver so we don't have
	 * to recompile for the motorola or ibm workstations sound systems.
	 * This is a really nasty hack, but unless we change the driver
	 * it's the only way to support both addrs from one binary.
	 * -- Cort
	 */
	if ( is_prep )
	{
		extern struct card_info snd_installed_cards[];
		struct card_info  *snd_ptr;

		for ( snd_ptr = snd_installed_cards; 
		      snd_ptr < &snd_installed_cards[num_sound_cards];
		      snd_ptr++ )
		{
			if ( snd_ptr->card_type == SNDCARD_CS4232 )
			{
				if ( _prep_type == _PREP_Motorola )
				{
					snd_ptr->config.io_base = 0x830;
					snd_ptr->config.irq = 10;
					snd_ptr->config.dma = ppc_cs4232_dma = 6;
					snd_ptr->config.dma2 = ppc_cs4232_dma2 = 7;
				}
				if ( _prep_type == _PREP_IBM )
				{
					snd_ptr->config.io_base = 0x530;
					snd_ptr->config.irq =  5;
					snd_ptr->config.dma = ppc_cs4232_dma = 1;
					/* this is wrong - but leave it for now */
					snd_ptr->config.dma2 = ppc_cs4232_dma2 = 7;
				}
			}
		}
	}
#endif /* CONFIG_CS4232 */	
	

	/*print_residual_device_info();*/
        request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");

#ifdef CONFIG_ABSTRACT_CONSOLE
#ifdef CONFIG_VGA_CONSOLE
        conswitchp = &vga_con;
#endif
#endif
}
