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

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>

/* for the mac fs */
kdev_t boot_dev;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_sec;

unsigned long empty_zero_page[1024];
unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

/* copy of the residual data */
RESIDUAL res;
/* ptr to residual data from hw, must be initialized so not in bss (gets cleared )*/
unsigned long resptr = 0;
int _machine;
extern unsigned long _TotalMemory;

#define COMMAND_LINE_SIZE 256
static char command_line[COMMAND_LINE_SIZE] = { 0, };
char saved_command_line[COMMAND_LINE_SIZE];
#ifdef HASHSTATS
unsigned long evicts;
#endif

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

void machine_halt(void)
{
	machine_restart(NULL);
}

void machine_power_off(void)
{
	machine_restart(NULL);
}

void machine_restart(char *cmd)
{
	unsigned char ctl;
	unsigned long flags;
	unsigned long i = 10000;
	
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

int
get_cpuinfo(char *buffer)
{
	extern char *Motherboard_map_name;
	int i;
	int pvr = _get_PVR();
	int len;
	char *model;
	PTE *ptr;
	unsigned long kptes = 0, uptes = 0, overflow = 0;
	unsigned int ti;
  
  
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
	default:
		model = "unknown";
		break;
	}
  
#ifdef __SMP__
#define CD(X)		(cpu_data[n].X)  
#else
#define CD(X) (X)
#define CPUN 0
#endif
  
	len = sprintf(buffer,"processor\t: %d\n"
		      "cpu\t\t: %s\n"
		      "revision\t: %d.%d\n"
		      "upgrade\t\t: %s\n"
		      "clock\t\t: %dMHz\n"
		      "bus clock\t: %dMHz\n"
		      "machine\t\t: %s (sn %s)\n"
		      "pci map\t\t: %s\n",
		      CPUN,
		      model,
		      MAJOR(pvr), MINOR(pvr),
		      (inb(IBM_EQUIP_PRESENT) & 2) ? "not upgrade" : "upgrade",
		      (res.VitalProductData.ProcessorHz > 1024) ?
		      res.VitalProductData.ProcessorHz>>20 :
		      res.VitalProductData.ProcessorHz,
		      (res.VitalProductData.ProcessorBusHz > 1024) ?
		      res.VitalProductData.ProcessorBusHz>>20 :
		      res.VitalProductData.ProcessorBusHz,
		      res.VitalProductData.PrintableModel,
		      res.VitalProductData.Serial,
		      Motherboard_map_name
		);
  
	/* print info about SIMMs */
	len += sprintf(buffer+len,"simms\t\t: ");
	for ( i = 0 ; (res.ActualNumMemories) && (i < MAX_MEMS) ; i++ )
	{
		if ( res.Memories[i].SIMMSize != 0 )
			len += sprintf(buffer+len,"%d:%dM ",i,
				       (res.Memories[i].SIMMSize > 1024) ?
				       res.Memories[i].SIMMSize>>20 :
				       res.Memories[i].SIMMSize);
	}
	len += sprintf(buffer+len,"\n");

	/* TLB */
	len += sprintf(buffer+len,"tlb\t\t:");
	switch(res.VitalProductData.TLBAttrib)
	{
	case CombinedTLB:
		len += sprintf(buffer+len," %d entries\n",
			       res.VitalProductData.TLBSize);
		break;
	case SplitTLB:
		len += sprintf(buffer+len," (split I/D) %d/%d entries\n",
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
		len += sprintf(buffer+len,"%dkB LineSize\n",
			       res.VitalProductData.CacheSize,
			       res.VitalProductData.CacheLineSize);
		break;
	case SplitCAC:
		len += sprintf(buffer+len,"(split I/D) %dkB/%dkB Linesize %dB/%dB\n",
			       res.VitalProductData.I_CacheSize,
			       res.VitalProductData.D_CacheSize,
			       res.VitalProductData.D_CacheLineSize,
			       res.VitalProductData.D_CacheLineSize);
		break;
	case NoneCAC:
		len += sprintf(buffer+len,"not present\n");
		break;
	}

	/* L2 */
	if ( (inb(IBM_EQUIP_PRESENT) & 1) == 0) /* l2 present */
	{
		int size;
    
		len += sprintf(buffer+len,"l2\t\t: %dkB %s\n",
			       ((inb(IBM_L2_STATUS) >> 5) & 1) ? 512 : 256,
			       (inb(IBM_SYS_CTL) & 64) ? "enabled" : "disabled");
	}
	else
	{
		len += sprintf(buffer+len,"l2\t\t: not present\n");
	}


	len += sprintf(buffer+len, "bogomips\t: %lu.%02lu\n",
		       CD(loops_per_sec+2500)/500000,
		       (CD(loops_per_sec+2500)/5000) % 100);
  
	/*
	 * Ooh's and aah's info about zero'd pages in idle task
	 */ 
	{
		extern unsigned int zerocount, zerototal, zeropage_hits;
		len += sprintf(buffer+len,"zero pages\t: total %u (%uKb) "
			       "current: %u (%uKb) hits: %u\n",
			       zerototal, (zerototal*PAGE_SIZE)>>10,
			       zerocount, (zerocount*PAGE_SIZE)>>10,
			       zeropage_hits);
	}


	/* ram/hash table info */
	len += sprintf(buffer+len,"hash table\t: %dkB (%dk buckets)\n",
		       Hash_size>>10,(Hash_size/(sizeof(PTE)*8)) >> 10);

	/* if booted print info about hash table use (overflows, etc) */
#ifdef HASHSTATS
	for ( ptr = Hash ; ptr < (PTE *)(Hash+Hash_size) ; ptr++)
	{
		if (ptr->v)
		{
			/* user not allowed read or write */
			if (ptr->pp == PP_RWXX)
				kptes++;
			else
				uptes++;
			if (ptr->h == 1)
				overflow++;
		}
	}
	/*len+=sprintf(buffer+len,"Hash %x Hash+Hash_size %x MemEnd %x\n",
	  Hash,Hash+Hash_size,KERNELBASE+_TotalMemory);*/
	/*len += sprintf(buffer+len,"PTEs: (user/kernel/max) %d (%d%%)/%d "
	  "(%d%%)/%d (%d%% full)\n",
	  uptes,(uptes*100)/(Hash_size/sizeof(PTE)),
	  kptes,(kptes*100)/(Hash_size/sizeof(PTE)),
	  Hash_size/sizeof(PTE),
	  ((uptes+kptes)*100)/(Hash_size/sizeof(PTE)));
	  len += sprintf(buffer+len,"Current Ovflw PTE's: %d Total Evicts: %u\n",
	  overflow,evicts);*/
#endif /* HASHSTATS */
	return len;
}

__initfunc(unsigned long
bios32_init(unsigned long memory_start, unsigned long memory_end))
{
	return memory_start;
}

__initfunc(void
setup_arch(char **cmdline_p, unsigned long * memory_start_p,
	   unsigned long * memory_end_p))
{
	extern char cmd_line[];
	extern char _etext[], _edata[], _end[];
	unsigned char reg;
	extern int panic_timeout;

	/* Save unparsed command line copy for /proc/cmdline */
	strcpy( saved_command_line, cmd_line );
	*cmdline_p = cmd_line;
  
	*memory_start_p = (unsigned long) Hash+Hash_size;
	(unsigned long *)*memory_end_p = (unsigned long *)(_TotalMemory+KERNELBASE);

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_sec = 50000000;
	
	/* reboot on panic */	
	/*panic_timeout = 180;*/
	
	init_task.mm->start_code = PAGE_OFFSET;
	init_task.mm->end_code = (unsigned long) _etext;
	init_task.mm->end_data = (unsigned long) _edata;
	init_task.mm->brk = (unsigned long) _end;	
	
	aux_device_present = 0xaa;
	
	switch ( _machine )
	{
	case _MACH_IBM:
		ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
		break;
	case _MACH_Motorola:
		ROOT_DEV = to_kdev_t(0x0801); /* sda1 */
		break;
	}
	/*ROOT_DEV = to_kdev_t(0x0811);*/ /* sdb1 */
#if 0
	strcpy(cmd_line+strlen(cmd_line),"console=1,9600,n8");
#endif
	
#if 0  
	if ( _machine == _MACH_Motorola )
	{
		/* get root via nfs from gordito -- only used for testing */
		ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255);	/* nfs */
		/*nfsaddrs=myip:serverip:gateip:netmaskip:clientname*/
		strcpy(cmd_line+strlen(cmd_line),
		       "nfsaddrs=129.138.6.13:129.138.6.101:129.138.6.1:255.255.255.0:"
		       "pandora nfsroot=/usr/src/root/");
	}
#endif
  
#ifdef CONFIG_BLK_DEV_RAM
#if 0
	ROOT_DEV = to_kdev_t(0x0200); /* floppy */  
	rd_prompt = 1;
	rd_doload = 1;
	rd_image_start = 0;
#endif
#endif
	
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}

