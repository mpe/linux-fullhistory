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
#include <linux/module.h>
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
#include <linux/console.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/cache.h>

#if defined(CONFIG_SOUND) || defined(CONFIG_SOUND_MODULE)
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

__prep
int
prep_get_cpuinfo(char *buffer)
{
	extern char *Motherboard_map_name;
	int len, i;
  
#ifdef __SMP__
#define CD(X)		(cpu_data[n].X)  
#else
#define CD(X) (X)
#endif
  
	len = sprintf(buffer,"machine\t\t: PReP %s\n",Motherboard_map_name);

	
	switch ( _prep_type )
	{
	case _PREP_IBM:
		if ((*(unsigned char *)0x8000080c) & (1<<6))
			len += sprintf(buffer+len,"Upgrade CPU\n");
		len += sprintf(buffer+len,"L2\t\t: ");
		if ((*(unsigned char *)0x8000080c) & (1<<7))
		{
			len += sprintf(buffer+len,"not present\n");
			goto no_l2;
		}
		len += sprintf(buffer+len,"%sKb,",
			       (((*(unsigned char *)0x8000080d)>>2)&1)?"512":"256");
		len += sprintf(buffer+len,"%sync\n",
			       ((*(unsigned char *)0x8000080d)>>7) ? "":"a");
		break;
	case _PREP_Motorola:
		len += sprintf(buffer+len,"L2\t\t: ");
		switch(*((unsigned char *)CACHECRBA) & L2CACHE_MASK)
		{
		case L2CACHE_512KB:
			len += sprintf(buffer+len,"512Kb");
			break;
		case L2CACHE_256KB:
			len += sprintf(buffer+len,"256Kb");
			break;
		case L2CACHE_1MB:
			len += sprintf(buffer+len,"1MB");
			break;
		case L2CACHE_NONE:
			len += sprintf(buffer+len,"none\n");
			goto no_l2;
			break;
		default:
			len += sprintf(buffer+len, "%x\n",
				       *((unsigned char *)CACHECRBA));
		}
		
		len += sprintf(buffer+len,",parity %s",
			       (*((unsigned char *)CACHECRBA) & L2CACHE_PARITY) ?
			       "enabled" : "disabled");
		
		len += sprintf(buffer+len, " SRAM:");
		
		switch ( ((*((unsigned char *)CACHECRBA) & 0xf0) >> 4) & ~(0x3) )
		{
		case 1: len += sprintf(buffer+len,
				       "synchronous,parity,flow-through\n");
			break;
		case 2: len += sprintf(buffer+len,"asynchronous,no parity\n");
			break;
		case 3: len += sprintf(buffer+len,"asynchronous,parity\n");
			break;
		default:len += sprintf(buffer+len,
				       "synchronous,pipelined,no parity\n");
			break;
		}
		break;
	}
	
	
no_l2:	
	if ( res->ResidualLength == 0 )
		return len;
	
	/* print info about SIMMs */
	len += sprintf(buffer+len,"simms\t\t: ");
	for ( i = 0 ; (res->ActualNumMemories) && (i < MAX_MEMS) ; i++ )
	{
		if ( res->Memories[i].SIMMSize != 0 )
			len += sprintf(buffer+len,"%d:%ldM ",i,
				       (res->Memories[i].SIMMSize > 1024) ?
				       res->Memories[i].SIMMSize>>20 :
				       res->Memories[i].SIMMSize);
	}
	len += sprintf(buffer+len,"\n");

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

	/* Enable L2.  Assume we don't need to flush -- Cort*/
	*(unsigned char *)(0x8000081c) = *(unsigned char *)(0x8000081c)|3;
	
	printk("Boot arguments: %s\n", cmd_line);
	
#ifdef CONFIG_SOUND_CS4232
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
#endif /* CONFIG_SOUND_CS4232 */	

	/*print_residual_device_info();*/
        request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");

#ifdef CONFIG_VGA_CONSOLE
        conswitchp = &vga_con;
#endif
}

__initfunc(void prep_ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq))
{
	ide_ioreg_t port = base;
	int i = 8;

	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
}

#ifdef CONFIG_SOUND_MODULE
EXPORT_SYMBOL(ppc_cs4232_dma);
EXPORT_SYMBOL(ppc_cs4232_dma2);
#endif
