/*
 * $Id: setup.c,v 1.77 1998/05/04 07:24:38 geert Exp $
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
#include <asm/pmu.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/ide.h>
#include <asm/prom.h>
#include <asm/processor.h>
#ifdef CONFIG_MBX
#include <asm/mbx.h>
#endif
/* ifdef APUS specific stuff until the merge is completed. -jskov */
#ifdef CONFIG_APUS
#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/amigappc.h>
extern unsigned long m68k_machtype;
extern void amiga_reset (void);
extern struct mem_info m68k_ramdisk;
extern int m68k_parse_bootinfo(const struct bi_record *);
extern char _end[];
#endif

extern char cmd_line[512];
char saved_command_line[256];
unsigned char aux_device_present;

#if !defined(CONFIG_MACH_SPECIFIC)
unsigned long ISA_DMA_THRESHOLD;
unsigned long DMA_MODE_READ, DMA_MODE_WRITE;
int _machine;
/* if we have openfirmware */
unsigned long have_of;
#endif /* ! CONFIG_MACH_SPECIFIC */

/* copy of the residual data */
RESIDUAL res;
int _prep_type;

/*
 * Perhaps we can put the pmac screen_info[] here
 * on pmac as well so we don't need the ifdef's.
 * Until we get multiple-console support in here
 * that is.  -- Cort
 */ 
#ifndef CONFIG_MBX
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
__initfunc(int pmac_display_supported(char *name))
{
	return 0;
}
__initfunc(void pmac_find_display(void))
{
}
#endif

#else /* CONFIG_MBX */

/* We need this to satisfy some external references until we can
 * strip the kernel down.
 */
struct screen_info screen_info = {
	0, 25,			/* orig-x, orig-y */
	{ 0, 0 },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	0,			/* orig-video-isVGA */
	16			/* orig-video-points */
};
#endif /* CONFIG_MBX */

/* cmd is ignored for now... */
void machine_restart(char *cmd)
{
#ifndef CONFIG_MBX
	struct adb_request req;
	unsigned long flags;
	unsigned long i = 10000;
#if 0
	int err;
#endif	

	switch(_machine)
	{
	case _MACH_Pmac:
		switch (adb_hardware) {
		case ADB_VIACUDA:
			cuda_request(&req, NULL, 2, CUDA_PACKET,
				     CUDA_RESET_SYSTEM);
			for (;;)
				cuda_poll();
			break;
		case ADB_VIAPMU:
			pmu_request(&req, NULL, 1, PMU_RESET);
			for (;;)
				pmu_poll();
			break;
		default:
		}
		break;

	case _MACH_chrp:
#if 0		/* RTAS doesn't seem to work on Longtrail.
		   For now, do it the same way as the PReP. */
	        /*err = call_rtas("system-reboot", 0, 1, NULL);
		printk("RTAS system-reboot returned %d\n", err);
		for (;;);*/
		
		{
			extern unsigned int rtas_entry, rtas_data, rtas_size;
			unsigned long status, value;
			printk("rtas_entry: %08x rtas_data: %08x rtas_size: %08x\n",
			       rtas_entry,rtas_data,rtas_size);
	}
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
	case _MACH_apus:
		cli();
		/* APUS:FIXME: Reset the system. Apparently there's
		 * more magic to it than this!?!?
		 */
#if 0
		APUS_WRITE(APUS_REG_SHADOW, REGSHADOW_SELFRESET);
		APUS_WRITE(APUS_REG_RESET, 
			   REGRESET_PPCRESET|REGRESET_M68KRESET|
			   REGRESET_AMIGARESET|REGRESET_AUXRESET|
			   REGRESET_SCSIRESET);
#endif
		printk("\n**************************************\n");
		printk("*** You can make a hard reset now! ***\n");
		printk("**************************************\n");
		for(;;);
		break;
	}
#else /* CONFIG_MBX */
	extern void MBX_gorom(void);
	MBX_gorom();
#endif /* CONFIG_MBX */
}

void machine_power_off(void)
{
#ifndef CONFIG_MBX	
	struct adb_request req;
#if 0	
	int err;
#endif	

	switch (_machine) {
	case _MACH_Pmac:
		switch (adb_hardware) {
		case ADB_VIACUDA:
			cuda_request(&req, NULL, 2, CUDA_PACKET,
				     CUDA_POWERDOWN);
			for (;;)
				cuda_poll();
			break;
		case ADB_VIAPMU:
			pmu_request(&req, NULL, 5, PMU_SHUTDOWN,
				    'M', 'A', 'T', 'T');
			for (;;)
				pmu_poll();
			break;
		default:
		}
		break;

	case _MACH_chrp:
#if 0		/* RTAS doesn't seem to work on Longtrail.
		   For now, do it the same way as the PReP. */
		err = call_rtas("power-off", 2, 1, NULL, 0, 0);
		printk("RTAS system-reboot returned %d\n", err);
		for (;;);
#endif

	case _MACH_prep:
		machine_restart(NULL);
#ifdef CONFIG_APUS
	case _MACH_apus:
#if defined(CONFIG_APM) && defined(CONFIG_APM_POWER_OFF)
		apm_set_power_state(APM_STATE_OFF);
		for (;;);
#endif
#endif
	}
	for (;;);
#else /* CONFIG_MBX */
	machine_restart(NULL);
#endif /* CONFIG_MBX */
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
	else /* prep, chrp or apus */
		machine_restart(NULL);

}

#ifdef CONFIG_BLK_DEV_IDE
void ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
#ifndef CONFIG_MBX
	switch (_machine) {
	case _MACH_Pmac:
		pmac_ide_init_hwif_ports(p,base,irq);
		break;
	case _MACH_chrp:
		chrp_ide_init_hwif_ports(p,base,irq);
		break;
	case _MACH_prep:
		prep_ide_init_hwif_ports(p,base,irq);
		break;
	}
#endif
}
#endif

unsigned long cpu_temp(void)
{
#if 0	
	unsigned long i, temp, thrm1, dir;
	int sanity;
	
	/*
	 * setup thrm3 - need to give TAU at least 20us
	 * to do the compare so assume a 300MHz clock.
	 * We need 300*20 ticks then.
	 * -- Cort
	 */
	asm("mtspr 1020, %1\n\t"
	    "mtspr 1021, %1\n\t"
	    "mtspr 1022, %0\n\t"::
	    "r" ( ((300*20)<<18) | THRM3_E), "r" (0) );
		
#if 0
	for ( i = 127 ; i >= 0 ; i-- )
	{
		asm("mtspr 1020, %0\n\t"::
		    "r" (THRM1_TID|THRM1_V|(i<<2)) );
		/* check value */
		while ( !( thrm1 & THRM1_TIV) )
			asm("mfspr %0, 1020 \n\t": "=r" (thrm1) );
		if ( thrm1 & THRM1_TIN )
		{
			printk("tin set: %x tiv %x\n", thrm1,thrm1&THRM1_TIV);
			goto out;
		}

	}
#endif
#if 0
	i = 32;			/* increment */
	dir = 1;		/* direction we're checking 0=up 1=down */
	temp = 64;		/* threshold checking against */
	while ( i )
	{
		_set_THRM1((1<<29) | THRM1_V | (temp<<2) );
		printk("checking %d in dir %d thrm set to %x/%x\n", temp,dir,
		       ( (1<<29) | THRM1_V | (temp<<2)),_get_THRM1());
		/* check value */
		sanity = 0x0fffffff;
		while ( (!( thrm1 & THRM1_TIV)) && (sanity--) )
			thrm1 = _get_THRM1();
			/*asm("mfspr %0, 1020 \n\t": "=r" (thrm1) );*/
		if ( ! sanity || sanity==0xffffffff ) printk("no sanity\n");
		/* temp is not in that direction */
		if ( !(thrm1 & THRM1_TIN) )
		{
			printk("not in that dir thrm1 %x\n",thrm1);
			if ( dir == 0 ) dir = 1;
			else dir = 0;
		}
		if ( dir ) temp -= i;
		else temp += i;
		i /= 2;
	}
	asm("mtspr 1020, %0\n\t"
	    "mtspr 1022, %0\n\t" ::"r" (0) );
#endif
#endif	
	return 0;
}

int get_cpuinfo(char *buffer)
{
	extern int pmac_get_cpuinfo(char *);
	extern int chrp_get_cpuinfo(char *);	
	extern int prep_get_cpuinfo(char *);
	extern int apus_get_cpuinfo(char *);
	unsigned long len = 0;
	unsigned long bogosum = 0;
	unsigned long i;
	unsigned long cr;
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
			len += sprintf(len+buffer,"750\n");
			cr = _get_L2CR();
			len += sprintf(len+buffer,"L2CR\t\t: %lx\n",cr);
			if ( cr & (0x1<<1)) cr = 256;
			else if ( cr & (0x2<<1)) cr = 512;
			else if ( cr & (0x3<<1)) cr = 1024;
			else cr = 0;
			len += sprintf(len+buffer,"on-chip l2\t: "
				       "%ld KB (%s)\n",
				       cr,(_get_L2CR()&1) ? "on" : "off");
			len += sprintf(len+buffer,"temperature \t: %lu C\n",
				       cpu_temp());
			break;
		case 9:
			len += sprintf(len+buffer, "604e\n");
			break;
		case 10:
			len += sprintf(len+buffer, "604ev5 (MachV)\n");
			break;
		case 50:
			len += sprintf(len+buffer, "821\n");
		case 80:
			len += sprintf(len+buffer, "860\n");
			break;
		default:
			len += sprintf(len+buffer, "unknown (%lu)\n",
				       GET_PVR>>16);
			break;
		}
		
#ifndef CONFIG_MBX
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
#else /* CONFIG_MBX */
		{
			bd_t	*bp;
			extern	RESIDUAL res;
			
			bp = (bd_t *)&res;
			
			len += sprintf(len+buffer,"clock\t\t: %dMHz\n"
				      "bus clock\t: %dMHz\n",
				      bp->bi_intfreq /*/ 1000000*/,
				      bp->bi_busfreq /*/ 1000000*/);
		}
#endif /* CONFIG_MBX */		
		
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

#ifndef CONFIG_MBX
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
#ifdef CONFIG_APUS
	case _MACH_apus:
		len += apus_get_cpuinfo(buffer+len);
		break;
#endif
	}
#endif /* ndef CONFIG_MBX */	
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
	extern void setup_pci_ptrs(void);
#ifndef CONFIG_MBX

#ifdef CONFIG_APUS
	if ( r3 == 0x61707573 )
	{
		/* Parse bootinfo. The bootinfo is located right after
                   the kernel bss */
		m68k_parse_bootinfo((const struct bi_record *)&_end);

		have_of = 0;
		
#ifdef CONFIG_BLK_DEV_INITRD
		/* Take care of initrd if we have one. Use data from
		   bootinfo to avoid the need to initialize PPC
		   registers when kernel is booted via a PPC reset. */
		if ( m68k_ramdisk.addr ) {
			initrd_start = (unsigned long) __va(m68k_ramdisk.addr);
			initrd_end = (unsigned long) 
				__va(m68k_ramdisk.size + m68k_ramdisk.addr);
		}
#endif /* CONFIG_BLK_DEV_INITRD */

		return 0;
	}
#endif /* CONFIG_APUS */

#ifndef CONFIG_MACH_SPECIFIC
	/* prep boot loader tells us if we're prep or not */
	if ( *(unsigned long *)(KERNELBASE) == (0xdeadc0de) )
	{
		_machine = _MACH_prep;
		have_of = 0;
	} else
	{
		/* need to ask OF if we're chrp or pmac */
		extern unsigned char OF_type[16], OF_model[16];
		prom_print(OF_type);
		prom_print(OF_model);
		if ( !strncmp("chrp", OF_type,4) )
		{
			_machine = _MACH_chrp;
		}
		else 
		{
		        /*if ( !strncmp("Power Macintosh", type,15) )*/
			_machine = _MACH_Pmac;
                }
		_machine = _MACH_Pmac;

	}
#endif /* CONFIG_MACH_SPECIFIC */		

	if ( have_of )
	{
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
			chosen = find_devices("chosen");
			if (chosen != NULL) {
				p = get_property(chosen, "bootargs", NULL);
				if (p != NULL)
					strncpy(cmd_line, p, sizeof(cmd_line));
			}
		}
		cmd_line[sizeof(cmd_line) - 1] = 0;
	}

#ifdef CONFIG_PCI
	/* so that pmac/chrp can use pci to find its console -- Cort */
	setup_pci_ptrs();
#endif

	switch (_machine)
	{
	case _MACH_Pmac:
#if !defined(CONFIG_MACH_SPECIFIC)
		/* isa_io_base gets set in pmac_find_bridges */
		isa_mem_base = PMAC_ISA_MEM_BASE;
		pci_dram_offset = PMAC_PCI_DRAM_OFFSET;
		ISA_DMA_THRESHOLD = ~0L;
		DMA_MODE_READ = 1;
		DMA_MODE_WRITE = 2;
#endif /* ! CONFIG_MACH_SPECIFIC */
		break;
	case _MACH_prep:
		/* make a copy of residual data */
		if ( r3 )
			memcpy((void *)&res,(void *)(r3+KERNELBASE),
			       sizeof(RESIDUAL));
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
				_prep_type = _PREP_IBM;
			else
				_prep_type = _PREP_Motorola;
		}
		else /* assume motorola if no residual (netboot?) */
			_prep_type = _PREP_Motorola;
#ifdef CONFIG_BLK_DEV_INITRD
		/* take care of initrd if we have one */
		if ( r4 )
		{
			initrd_start = r4 + KERNELBASE;
			initrd_end = r5 + KERNELBASE;
		}
#endif /* CONFIG_BLK_DEV_INITRD */
		/* take care of cmd line */
		if ( r6 )
		{
			*(char *)(r7+KERNELBASE) = 0;
			strcpy(cmd_line, (char *)(r6+KERNELBASE));
		}
		break;
	case _MACH_chrp:
#ifdef CONFIG_BLK_DEV_INITRD
		/* take care of initrd if we have one */
		if ( r3 )
		{
			initrd_start = r3 + KERNELBASE;
			initrd_end = r3+ r4 + KERNELBASE;
		}
#endif /* CONFIG_BLK_DEV_INITRD */
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
#else /* CONFIG_MBX */

	if ( r3 )
		memcpy( (void *)&res,(void *)(r3+KERNELBASE), sizeof(bd_t) );

	setup_pci_ptrs();

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	/* take care of cmd line */
	if ( r6 )
	{
		
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

#endif /* CONFIG_MBX */
	return 0;
}

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	extern void pmac_setup_arch(unsigned long *, unsigned long *);
	extern void chrp_setup_arch(unsigned long *, unsigned long *);
	extern void prep_setup_arch(unsigned long *, unsigned long *);
	extern void apus_setup_arch(char **, unsigned long *, unsigned long *);
	extern void mbx_setup_arch(unsigned long *, unsigned long *);
	extern int panic_timeout;
	extern char _etext[], _edata[];
	extern char *klimit;
	extern unsigned long find_available_memory(void);
	extern unsigned long *end_of_DRAM;

#ifdef CONFIG_XMON
	extern void xmon_map_scc(void);
	xmon_map_scc();
#endif /* CONFIG_XMON */

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
#ifdef CONFIG_MBX
	mbx_setup_arch(memory_start_p,memory_end_p);
#else /* CONFIG_MBX */	
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
#ifdef CONFIG_APUS
	case _MACH_apus:
		m68k_machtype = MACH_AMIGA;
		apus_setup_arch(cmdline_p,memory_start_p,memory_end_p);
		break;
#endif
	default:
		printk("Unknown machine %d in setup_arch()\n", _machine);
	}
#endif /* CONFIG_MBX */	
}
