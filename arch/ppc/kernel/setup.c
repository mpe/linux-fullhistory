/*
 * $Id: setup.c,v 1.12 1997/08/13 03:06:17 cort Exp $
 * Common prep/pmac boot and setup code.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/reboot.h>

#include <asm/cuda.h>
#include <asm/residual.h>
#include <asm/io.h>

char saved_command_line[256];
unsigned char aux_device_present;

/* copy of the residual data */
RESIDUAL res;
int _machine;

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
	if (!strncmp(res.VitalProductData.PrintableModel,"IBM",3))
		_machine = _MACH_IBM;
	else
		_machine = _MACH_Motorola;
#endif /* CONFIG_PREP */
	
	if ( _machine == _MACH_Pmac )
	{
		io_base = 0;
	}
	else if ( is_prep ) /* prep */
	{
		io_base = 0x80000000;
		/* make a copy of residual data */
		if ( r3 )
			memcpy( (void *)&res,(void *)(r3+KERNELBASE), sizeof(RESIDUAL) );
		/* take care of initrd if we have one */
		if ( r4 )
		{
			initrd_start = r4 + KERNELBASE;
			initrd_end = r5 + KERNELBASE;
		}
		/* take care of cmd line */
		if ( r6 )
		{
			
			*(char *)(r7+KERNELBASE) = 0;
			strcpy(cmd_line, (char *)(r6+KERNELBASE));
		}
	}
	else
	{
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

	if ( _machine == _MACH_Pmac )
	{
		cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
		for (;;)
			cuda_poll();
	}
	else /* prep */
	{
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
	else /* prep */
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
	else /* prep */
		machine_restart(NULL);

}


__initfunc(unsigned long
bios32_init(unsigned long memory_start, unsigned long memory_end))
{
	return memory_start;
}

/*
 * Will merge more into here later  -- Cort
 */
int get_cpuinfo(char *buffer)
{
	extern int pmac_get_cpuinfo(char *);	
	extern int prep_get_cpuinfo(char *);
	
	if ( _machine == _MACH_Pmac )
		return pmac_get_cpuinfo(buffer);
#ifdef CONFIG_PREP
	else /* prep */
		return prep_get_cpuinfo(buffer);
#endif /* CONFIG_PREP */
}

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	extern void pmac_setup_arch(char **, unsigned long *, unsigned long *);
	extern void prep_setup_arch(char **, unsigned long *, unsigned long *);
	
	if ( _machine == _MACH_Pmac )
		pmac_setup_arch(cmdline_p,memory_start_p,memory_end_p);
#ifdef CONFIG_PREP
	else /* prep */
		prep_setup_arch(cmdline_p,memory_start_p,memory_end_p);
#endif /* CONFIG_PREP */
}


