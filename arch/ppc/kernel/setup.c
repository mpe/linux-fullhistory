/*
 * $Id: setup.c,v 1.16 1997/08/27 22:06:54 cort Exp $
 * Common prep/pmac boot and setup code.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/openpic.h>

#include <asm/cuda.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/ide.h>

char saved_command_line[256];
unsigned char aux_device_present;

/* copy of the residual data */
RESIDUAL res;
int _machine;

/*
 * Perhaps we can put the pmac screen_info[] here
 * on pmac as well so we don't need the ifdef's.
 * Until we get multiple-console support in here
 * that is.  -- Cort
 */ 
#if defined(CONFIG_CHRP) || defined(CONFIG_PREP )
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

/*
 * I really need to add multiple-console support... -- Cort
 */
int pmac_display_supported(char *name)
{
	return 0;
}
int sd_find_target(void *a, int b)
{
	return 0;
}
void pmac_find_display(void)
{
}

#endif

/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init() )
 */
unsigned long identify_machine(unsigned long r3, unsigned long r4, unsigned long r5,
			       unsigned long r6, unsigned long r7)
{
	extern unsigned long initrd_start, initrd_end;
	extern char cmd_line[256];
#ifdef CONFIG_PMAC /* cheat for now - perhaps a check for OF could tell us */
	_machine = _MACH_Pmac;
#endif /* CONFIG_PMAC */
#ifdef CONFIG_PREP
	/* make a copy of residual data */
	if ( r3 )
		memcpy( (void *)&res,(void *)(r3+KERNELBASE), sizeof(RESIDUAL) );
	if (!strncmp(res.VitalProductData.PrintableModel,"IBM",3))
		_machine = _MACH_IBM;
	else
		_machine = _MACH_Motorola;
#endif /* CONFIG_PREP */
#ifdef CONFIG_CHRP 
	_machine = _MACH_chrp;
#endif /* CONFIG_CHRP */

	switch (_machine)
	{
	case _MACH_Pmac:
		io_base = 0;
		pci_dram_offset = 0;
		break;
	case _MACH_IBM:
	case _MACH_Motorola:
		io_base = 0x80000000;
		pci_dram_offset = 0x80000000;
#ifdef CONFIG_BLK_DEV_RAM
		/* take care of initrd if we have one */
		if ( r4 )
		{
			initrd_start = r4 + KERNELBASE;
			initrd_end = r5 + KERNELBASE;
		}
#endif /* CONFIG_BLK_DEV_RAM */
		/* take care of cmd line */
		if ( r6 )
		{
			
			*(char *)(r7+KERNELBASE) = 0;
			strcpy(cmd_line, (char *)(r6+KERNELBASE));
		}
		break;
	case _MACH_chrp:
		/* LongTrail */
		io_base = 0xf8000000;
		pci_dram_offset = 0;
		/* take care of initrd if we have one */
		if ( r4 ) {
			initrd_start = r4 + KERNELBASE;
			initrd_end = r5 + KERNELBASE;
		}
		/* take care of cmd line */
		if ( r6 ) {
			*(char *)(r7+KERNELBASE) = 0;
			strcpy(cmd_line, (char *)(r6+KERNELBASE));
		}
		break;
	default:
		printk("Unknown machine type in identify_machine!\n");
	}
	return 0;
}

/* cmd is ignored for now... */
void machine_restart(char *cmd)
{
	struct cuda_request req;
	unsigned long flags;
	unsigned long i = 10000;

	switch(_machine)
	{
	case _MACH_Pmac:
		cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
		for (;;)
			cuda_poll();
		break;
	case _MACH_IBM:
	case _MACH_Motorola:
		_disable_interrupts();
		
		/* set exception prefix high - to the prom */
		save_flags( flags );
		restore_flags( flags|MSR_IP );
		
		/* make sure bit 0 (reset) is a 0 */
		outb( inb(0x92) & ~1L , 0x92 );
		/* signal a reset to system control port A - soft reset */
		outb( inb(0x92) | 1 , 0x92 );
		
		while ( i != 0 ) i++;
		panic("restart failed\n");
		break;

	case _MACH_chrp:
		openpic_init_processor(1<<0);
		break;
	}
}

void machine_power_off(void)
{
	struct cuda_request req;

	if ( _machine == _MACH_Pmac )
	{
		cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);
		for (;;)
			cuda_poll();
	}
	else /* prep or chrp */
	{
		machine_restart(NULL);
	}
}

void machine_halt(void)
{
	if ( _machine == _MACH_Pmac )
	{
#if 0
		prom_exit();		/* doesn't work because prom is trashed */
#else
		machine_power_off();	/* for now */
#endif
	}
	else /* prep or chrp */
		machine_restart(NULL);

}

void ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	if ( _machine == _MACH_Pmac )
		pmac_ide_init_hwif_ports(p,base,irq);
	else /* prep */
		prep_ide_init_hwif_ports(p,base,irq);
	
}

/*
 * Will merge more into here later  -- Cort
 */
int get_cpuinfo(char *buffer)
{
	extern int pmac_get_cpuinfo(char *);
	extern int chrp_get_cpuinfo(char *);	
	extern int prep_get_cpuinfo(char *);
	

	switch (_machine)
	{
	case _MACH_Pmac:
		return pmac_get_cpuinfo(buffer);
		break;
	case _MACH_Motorola:
	case _MACH_IBM:
		return prep_get_cpuinfo(buffer);
		break;
	case _MACH_chrp:
		return chrp_get_cpuinfo(buffer);
		break;
	}
	printk("Unknown machine %d in get_cpuinfo()\n",_machine);
	return 0;
}


__initfunc(unsigned long
bios32_init(unsigned long memory_start, unsigned long memory_end))
{
	return memory_start;
}

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	extern void pmac_setup_arch(char **, unsigned long *, unsigned long *);
	extern void chrp_setup_arch(char **, unsigned long *, unsigned long *);
	extern void prep_setup_arch(char **, unsigned long *, unsigned long *);
	
	switch (_machine)
	{
	case _MACH_Pmac:
		pmac_setup_arch(cmdline_p,memory_start_p,memory_end_p);
		break;
	case _MACH_Motorola:
	case _MACH_IBM:
		prep_setup_arch(cmdline_p,memory_start_p,memory_end_p);
		break;
	case _MACH_chrp:
		return chrp_setup_arch(cmdline_p,memory_start_p,memory_end_p);
		break;
	}
	printk("Unknown machine %d in setup_arch()\n",_machine);
}



