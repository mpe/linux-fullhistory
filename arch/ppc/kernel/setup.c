/*
 * $Id: setup.c,v 1.48 1998/01/01 10:04:44 paulus Exp $
 * Common prep/pmac/chrp boot and setup code.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/blk.h>

#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/ide.h>
#include <asm/prom.h>

extern char cmd_line[512];
char saved_command_line[256];
unsigned char aux_device_present;

#if !defined(CONFIG_MACH_SPECIFIC)
unsigned long ISA_DMA_THRESHOLD;
unsigned long DMA_MODE_READ, DMA_MODE_WRITE;
int _machine;
#endif /* ! CONFIG_MACH_SPECIFIC */

/* copy of the residual data */
RESIDUAL res;
int _prep_type;
/* if we have openfirmware */
unsigned long have_of;

/*
 * Perhaps we can put the pmac screen_info[] here
 * on pmac as well so we don't need the ifdef's.
 * Until we get multiple-console support in here
 * that is.  -- Cort
 */ 
#if !defined(CONFIG_PMAC_CONSOLE)
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
void pmac_find_display(void)
{
}
#endif

/* cmd is ignored for now... */
void machine_restart(char *cmd)
{
	struct adb_request req;
	unsigned long flags;
	unsigned long i = 10000;
#if 0	
	int err;
#endif	

	switch(_machine)
	{
	case _MACH_Pmac:
		cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
		for (;;)
			cuda_poll();
		break;
	case _MACH_chrp:
#if 0		/* RTAS doesn't seem to work on Longtrail.
		   For now, do it the same way as the PReP. */
		err = call_rtas("system-reboot", 0, 1, NULL);
		printk("RTAS system-reboot returned %d\n", err);
		for (;;);
#endif
	case _MACH_prep:
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
	}
}

void machine_power_off(void)
{
	struct adb_request req;
#if 0	
	int err;
#endif	

	switch (_machine) {
	case _MACH_Pmac:
		cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);
		for (;;)
			cuda_poll();
	case _MACH_chrp:
#if 0		/* RTAS doesn't seem to work on Longtrail.
		   For now, do it the same way as the PReP. */
		err = call_rtas("power-off", 2, 1, NULL, 0, 0);
		printk("RTAS system-reboot returned %d\n", err);
		for (;;);
#endif
	case _MACH_prep:
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
	else /* prep or chrp */
		prep_ide_init_hwif_ports(p,base,irq);
	
}

int get_cpuinfo(char *buffer)
{
	extern int pmac_get_cpuinfo(char *);
	extern int chrp_get_cpuinfo(char *);	
	extern int prep_get_cpuinfo(char *);
	unsigned long len = 0;
	unsigned long bogosum = 0;
	unsigned long i;
#ifdef __SMP__
	extern unsigned long cpu_present_map;	
	extern struct cpuinfo_PPC cpu_data[NR_CPUS];
#define GET_PVR ((long int)(cpu_data[i].pvr))
#define CD(x) (cpu_data[i].x)
#else
#define cpu_present_map 1L
#define smp_num_cpus 1
#define GET_PVR ((long int)_get_PVR())
#define CD(x) (x)
#endif	

	for ( i = 0; i < smp_num_cpus ; i++ )
	{
		if ( ! ( cpu_present_map & (1<<i) ) )
			continue;
		if ( i )
			len += sprintf(len+buffer,"\n");
		len += sprintf(len+buffer,"processor\t: %lu\n",i);
		len += sprintf(len+buffer,"cpu\t\t: ");
	
		switch (GET_PVR >> 16)
		{
		case 1:
			len += sprintf(len+buffer, "601\n");
			break;
		case 3:
			len += sprintf(len+buffer, "603\n");
			break;
		case 4:
			len += sprintf(len+buffer, "604\n");
			break;
		case 6:
			len += sprintf(len+buffer, "603e\n");
			break;
		case 7:
			len += sprintf(len+buffer, "603ev\n");
			break;
		case 8:
			len += sprintf(len+buffer, "750 (Arthur)\n");
			break;
		case 9:
			len += sprintf(len+buffer, "604e\n");
			break;
		case 10:
			len += sprintf(len+buffer, "604ev5 (MachV)\n");
			break;
		default:
			len += sprintf(len+buffer, "unknown (%lu)\n",
				       GET_PVR>>16);
			break;
		}

		/*
		 * Assume here that all clock rates are the same in a
		 * smp system.  -- Cort
		 */
		if ( have_of )
		{
			struct device_node *cpu_node;
			int *fp;
			
			cpu_node = find_type_devices("cpu");
			if ( !cpu_node ) break;
			fp = (int *) get_property(cpu_node, "clock-frequency", NULL);
			if ( !fp ) break;
			len += sprintf(len+buffer, "clock\t\t: %dMHz\n",
				       *fp / 1000000);
		}
		
		/* PREP's without residual data for some reason will give
		   incorrect values here */
		if ( is_prep )
		{
			len += sprintf(len+buffer, "clock\t\t: ");
			if ( res.ResidualLength )
				len += sprintf(len+buffer, "%ldMHz\n",
				       (res.VitalProductData.ProcessorHz > 1024) ?
				       res.VitalProductData.ProcessorHz>>20 :
				       res.VitalProductData.ProcessorHz);
			else
				len += sprintf(len+buffer, "???\n");
		}				
		
		len += sprintf(len+buffer, "revision\t: %ld.%ld\n",
			       (GET_PVR & 0xff00) >> 8, GET_PVR & 0xff);

		len += sprintf(buffer+len, "bogomips\t: %lu.%02lu\n",
			       (CD(loops_per_sec)+2500)/500000,
			       (CD(loops_per_sec)+2500)/5000 % 100);
		bogosum += CD(loops_per_sec);
	}

#ifdef __SMP__
	if ( i )
		len += sprintf(buffer+len, "\n");
	len += sprintf(buffer+len,"total bogomips\t: %lu.%02lu\n",
	       (bogosum+2500)/500000,
	       (bogosum+2500)/5000 % 100);
#endif /* __SMP__ */

	/*
	 * Ooh's and aah's info about zero'd pages in idle task
	 */ 
	{
		extern unsigned int zerocount, zerototal, zeropage_hits,zeropage_calls;
		len += sprintf(buffer+len,"zero pages\t: total %u (%luKb) "
			       "current: %u (%luKb) hits: %u/%u (%u%%)\n",
			       zerototal, (zerototal*PAGE_SIZE)>>10,
			       zerocount, (zerocount*PAGE_SIZE)>>10,
			       zeropage_hits,zeropage_calls,
			       /* : 1 below is so we don't div by zero */
			       (zeropage_hits*100) /
			            ((zeropage_calls)?zeropage_calls:1));
	}

	switch (_machine)
	{
	case _MACH_Pmac:
		len += pmac_get_cpuinfo(buffer+len);
		break;
	case _MACH_prep:
		len += prep_get_cpuinfo(buffer+len);
		break;
	case _MACH_chrp:
		len += chrp_get_cpuinfo(buffer+len);
		break;
	}
	return len;
}

/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init() )
 */
__initfunc(unsigned long
identify_machine(unsigned long r3, unsigned long r4, unsigned long r5,
		 unsigned long r6, unsigned long r7))
{
	extern unsigned long initrd_start, initrd_end;
	extern setup_pci_ptrs(void);
	unsigned long boot_sdr1;
	ihandle prom_root;
	unsigned char type[16], model[16];

	asm("mfspr %0,25\n\t" :"=r" (boot_sdr1));

	/* 
	 * if we have a sdr1 then we have openfirmware 
	 * and can ask it what machine we are (chrp/pmac/prep).
	 * otherwise we're definitely prep. -- Cort
	 */
	if ( !boot_sdr1 )
	{
		/* we know for certain we're prep if no OF */
		have_of = 0;
		/* make a copy of residual data */
		if ( r3 )
			memcpy((void *)&res,(void *)(r3+KERNELBASE),
			       sizeof(RESIDUAL));
#ifndef CONFIG_MACH_SPECIFIC
		_machine = _MACH_prep;
#endif /* CONFIG_MACH_SPECIFIC */
	}
	else
	{
		/*
		 * init prom here, then ask the openfirmware
		 * what machine we are (prep/chrp/pmac).  We don't use
		 * OF on prep just yet.  -- Cort
		 */
#ifndef CONFIG_PREP		/* don't use OF on prep yet */
		have_of = 1;
		/* prom_init has already been called from __start */
		finish_device_tree();

		/*
		 * If we were booted via quik, r3 points to the physical
		 * address of the command-line parameters.
		 * If we were booted from an xcoff image (i.e. netbooted or
		 * booted from floppy), we get the command line from the
		 * bootargs property of the /chosen node.
		 * If an initial ramdisk is present, r3 and r4
		 * are used for initrd_start and initrd_size,
		 * otherwise they contain 0xdeadbeef.  
		 */
		cmd_line[0] = 0;
		if (r3 >= 0x4000 && r3 < 0x800000 && r4 == 0) {
			strncpy(cmd_line, (char *)r3 + KERNELBASE,
				sizeof(cmd_line));
		} else {
			struct device_node *chosen;
			char *p;

#ifdef CONFIG_BLK_DEV_INITRD
			if (r3 - KERNELBASE < 0x800000
			    && r4 != 0 && r4 != 0xdeadbeef) {
				initrd_start = r3;
				initrd_end = r3 + r4;
				ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
			}
#endif
			chosen = find_path_device("/chosen");
			if (chosen != NULL) {
				p = get_property(chosen, "bootargs", NULL);
				if (p != NULL)
					strncpy(cmd_line, p, sizeof(cmd_line));
			}
		}
		cmd_line[sizeof(cmd_line) - 1] = 0;
#endif /* CONFIG_PREP */

#ifndef CONFIG_MACH_SPECIFIC
#if 0
		prom_root = call_prom("finddevice", 1, 1, "/");		
		call_prom("getprop", 4, 1, prom_root, "device_type", &type,
			  (void *) sizeof(type));
		call_prom("getprop", 4, 1, prom_root, "model", &type,
			  (void *) sizeof(model));
		if ( !strncmp("chrp", type,4) )
		{
			_machine = _MACH_chrp;
		}
		else 
		{
		        /*if ( !strncmp("Power Macintosh", type,15) )*/
			_machine = _MACH_Pmac;
                }
#else

#ifdef CONFIG_CHRP		
		_machine = _MACH_chrp;
#endif /* CONFIG_CHRP */
#ifdef CONFIG_PMAC		
		_machine = _MACH_Pmac;
#endif /* CONFIG_PMAC */
#ifdef CONFIG_PREP
		_machine = _MACH_Prep;
#endif /* CONFIG_PREP */
#endif /* #if */		
#endif /* CONFIG_MACH_SPECIFIC */
	}

	/* so that pmac/chrp can use pci to find its console -- Cort */
	setup_pci_ptrs();
	
	switch (_machine)
	{
	case _MACH_Pmac:
#if !defined(CONFIG_MACH_SPECIFIC)
		isa_io_base = PMAC_ISA_IO_BASE;
		isa_mem_base = PMAC_ISA_MEM_BASE;
		pci_dram_offset = PMAC_PCI_DRAM_OFFSET;
		ISA_DMA_THRESHOLD = ~0L;
		DMA_MODE_READ = 1;
		DMA_MODE_WRITE = 2;
#endif /* ! CONFIG_MACH_SPECIFIC */
		break;
	case _MACH_prep:
#if !defined(CONFIG_MACH_SPECIFIC)
		isa_io_base = PREP_ISA_IO_BASE;
		isa_mem_base = PREP_ISA_MEM_BASE;
		pci_dram_offset = PREP_PCI_DRAM_OFFSET;
		ISA_DMA_THRESHOLD = 0x00ffffff;
		DMA_MODE_READ = 0x44;
		DMA_MODE_WRITE = 0x48;
#endif /* ! CONFIG_MACH_SPECIFIC */
		/* figure out what kind of prep workstation we are */
		if ( res.ResidualLength != 0 )
		{
			if ( !strncmp(res.VitalProductData.PrintableModel,"IBM",3) )
				_prep_type = 0x00;
			else
				_prep_type = 0x01;
		}
		else /* assume motorola if no residual (netboot?) */
			_prep_type = _PREP_Motorola;

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
#if !defined(CONFIG_MACH_SPECIFIC)
		isa_io_base = CHRP_ISA_IO_BASE;
		isa_mem_base = CHRP_ISA_MEM_BASE;
		pci_dram_offset = CHRP_PCI_DRAM_OFFSET;
		ISA_DMA_THRESHOLD = ~0L;
		DMA_MODE_READ = 0x44;
		DMA_MODE_WRITE = 0x48;
#endif /* ! CONFIG_MACH_SPECIFIC */
		break;
	default:
		printk("Unknown machine type in identify_machine!\n");
	}
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
	extern void pmac_setup_arch(unsigned long *, unsigned long *);
	extern void chrp_setup_arch(unsigned long *, unsigned long *);
	extern void prep_setup_arch(unsigned long *, unsigned long *);
	extern int panic_timeout;
	extern char _etext[], _edata[];
	extern char *klimit;
	extern unsigned long find_available_memory(void);
	extern unsigned long *end_of_DRAM;

	/* reboot on panic */	
	panic_timeout = 180;
	
	init_task.mm->start_code = PAGE_OFFSET;
	init_task.mm->end_code = (unsigned long) _etext;
	init_task.mm->end_data = (unsigned long) _edata;
	init_task.mm->brk = (unsigned long) klimit;	

	/* Save unparsed command line copy for /proc/cmdline */
	strcpy(saved_command_line, cmd_line);
	*cmdline_p = cmd_line;

	*memory_start_p = find_available_memory();
	*memory_end_p = (unsigned long) end_of_DRAM;

	switch (_machine) {
	case _MACH_Pmac:
		pmac_setup_arch(memory_start_p, memory_end_p);
		break;
	case _MACH_prep:
		prep_setup_arch(memory_start_p, memory_end_p);
		break;
	case _MACH_chrp:
		chrp_setup_arch(memory_start_p, memory_end_p);
		break;
	default:
		printk("Unknown machine %d in setup_arch()\n", _machine);
	}
}
