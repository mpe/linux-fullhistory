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

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>

extern unsigned long *end_of_DRAM;
extern PTE *Hash;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
unsigned long empty_zero_page[1024];

unsigned char aux_device_present;
#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

#undef HASHSTATS

extern unsigned long isBeBox[];

/* copy of the residual data */
RESIDUAL res;
unsigned long resptr = 0; /* ptr to residual data from hw */

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a EGA+ colour display.
 */
 /* this is changed only in minor ways from the original
        -- Cort
 */
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


unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
  return memory_start;
}

#ifdef HASHSTATS
unsigned long *hashhits;
#endif

extern unsigned long _TotalMemory;
/* find the physical size of RAM and setup hardware hash table */
unsigned long *find_end_of_memory(void)
{
  extern BAT BAT2;
  _TotalMemory = res.TotalMemory;
  
  if (_TotalMemory == 0 )
  {
    printk("Ramsize from residual data was 0 -- Probing for value\n");
    /* this needs be done differently since the bats actually map
       addresses beyond physical memory! -- Cort */
#if 0
    probingmem = 1;
    while ( probingmem )
    {
	_TotalMemory += 0x00800000; /* 8M */
	*(unsigned long *)_TotalMemory+KERNELBASE;
    }
    _TotalMemory -= 0x00800000;
#else    
    _TotalMemory = 0x03000000;
#endif
    printk("Ramsize probed to be %dM\n", _TotalMemory>>20);
  }
  
  /* setup BAT2 mapping so that it covers kernelbase to kernelbase+ramsize */
  switch(_TotalMemory)
  {
    case 0x01000000:		/* 16M */
      BAT2.batu.bl = BL_16M;
      Hash_size = HASH_TABLE_SIZE_128K;
      Hash_mask = HASH_TABLE_MASK_128K;
      break;
    case 0x00800000:		/* 8M */
      BAT2.batu.bl = BL_8M;
      Hash_size = HASH_TABLE_SIZE_64K;
      Hash_mask = HASH_TABLE_MASK_64K;
      break;
    case 0x01800000:		/* 24M */
    case 0x02000000:		/* 32M */
      BAT2.batu.bl = BL_32M;
      Hash_size = HASH_TABLE_SIZE_256K;
      Hash_mask = HASH_TABLE_MASK_256K;
      break;
    case 0x03000000:		/* 48M */
    case 0x04000000:		/* 64M */
      BAT2.batu.bl = BL_64M;
      Hash_size = HASH_TABLE_SIZE_512K;
      Hash_mask = HASH_TABLE_MASK_512K;
      break;
    case 0x05000000:		/* 80M */
      BAT2.batu.bl = BL_128M;
      Hash_size = HASH_TABLE_SIZE_1M;
      Hash_mask = HASH_TABLE_MASK_1M;
      break;    
    default:
      printk("WARNING: setup.c: find_end_of_memory() unknown total ram size %x\n",
	     _TotalMemory);
      break;
  }
  
  Hash = (PTE *)((_TotalMemory-Hash_size)+KERNELBASE);
  bzero(Hash, Hash_size);
  
 
#ifdef HASHSTATS
  hashhits = (unsigned long *)Hash - (Hash_size/sizeof(struct _PTE))/2;
  bzero(hashhits, (Hash_size/sizeof(struct _PTE))/2);
  return ((unsigned long *)hashhits);
#else
  return ((unsigned long *)Hash);
#endif
}

int size_memory;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#ifdef CONFIG_APM
#define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+64))
#endif
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned long *) (PARAM+0x214))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];


void
setup_arch(char **cmdline_p, unsigned long * memory_start_p,
	   unsigned long * memory_end_p)
{
  extern int _end;
  extern char cmd_line[];
  unsigned char reg;
  extern int panic_timeout;
  char inf[512];
  int i;

  if (isBeBox[0])
    _Processor = _PROC_Be;
  else
  {
    if (strncmp(res.VitalProductData.PrintableModel,"IBM",3))
    {
      _Processor = _PROC_Motorola;
    }
    else
      _Processor = _PROC_IBM;
  }
  
  get_cpuinfo(&inf);
  printk("%s",inf);
  
  /* Set up floppy in PS/2 mode */
  outb(0x09, SIO_CONFIG_RA);
  reg = inb(SIO_CONFIG_RD);
  reg = (reg & 0x3F) | 0x40;
  outb(reg, SIO_CONFIG_RD);
  outb(reg, SIO_CONFIG_RD);	/* Have to write twice to change! */

  switch ( _Processor )
  {
    case _PROC_IBM:
        ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
	break;
    case _PROC_Motorola:
        ROOT_DEV = to_kdev_t(0x0801); /* sda1 */
        break;
  }
  aux_device_present = 0xaa;

  panic_timeout = 300;		/* reboot on panic */
  
#if 0
  /* get root via nfs from charon -- was only used for testing */
  ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255);	/* nfs */
  /*nfsaddrs=myip:serverip:gateip:netmaskip:clientname*/
  strcpy(cmd_line,
	 "nfsaddrs=129.138.6.101:129.138.6.90:129.138.6.1:255.255.255.0:gordito nfsroot=/joplin/ppc/root/");
#endif
  *cmdline_p = cmd_line;
  *memory_start_p = (unsigned long) &_end;
  (unsigned long *)*memory_end_p = (unsigned long *)end_of_DRAM;
  size_memory = *memory_end_p - KERNELBASE;  /* Relative size of memory */

#ifdef CONFIG_BLK_DEV_RAM
  rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
  rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
  rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#if 1
  rd_prompt = 1;
  rd_doload = 1;
  rd_image_start = 0;
#endif
#endif

  /* Save unparsed command line copy for /proc/cmdline */
  memcpy(saved_command_line, cmd_line,strlen(cmd_line)+1);
  printk("Command line: %s\n", cmd_line);
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
  return -EIO;
}


int
get_cpuinfo(char *buffer)
{
  extern unsigned long loops_per_sec;
  int i;
  int pvr = _get_PVR();
  int len;
  char *model;
  unsigned long full = 0, overflow = 0;
  unsigned int ti;
  PTE *ptr;  
  
  switch (pvr>>16)
    {
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
  
  len = sprintf(buffer, "PowerPC %s/%dMHz revision %d.%d %s\n",
		model,
		(res.VitalProductData.ProcessorHz > 1024) ?
		res.VitalProductData.ProcessorHz>>20 :
		res.VitalProductData.ProcessorHz,
		MAJOR(pvr), MINOR(pvr),
		(inb(IBM_EQUIP_PRESENT) & 2) ? "" : "upgrade");
#if 1
  if ( res.VitalProductData.PrintableModel[0] )
    len += sprintf(buffer+len,"%s\n",res.VitalProductData.PrintableModel);
  
  len += sprintf(buffer+len,"Bus %dMHz\n",
		(res.VitalProductData.ProcessorBusHz > 1024) ?
		res.VitalProductData.ProcessorBusHz>>20 :
		res.VitalProductData.ProcessorBusHz);
  
  /* make sure loops_per_sec has been setup -- ie not at boottime -- Cort */
  if ( CD(loops_per_sec+2500)/500000 > 0)
    len += sprintf(buffer+len,
		   "bogomips: %lu.%02lu\n",
		   CD(loops_per_sec+2500)/500000,
		   (CD(loops_per_sec+2500)/5000) % 100);


  len += sprintf(buffer+len,"Total Ram: %dM Hash Table: %dkB (%dk buckets)\n",
	 _TotalMemory>>20, Hash_size>>10,
	 (Hash_size/(sizeof(PTE)*8)) >> 10);
  
  for ( i = 0 ; (res.ActualNumMemories) && (i < MAX_MEMS) ; i++ )
  {
    if (i == 0)
      len += sprintf(buffer+len,"SIMM Banks: ");
    if ( res.Memories[i].SIMMSize != 0 )
      len += sprintf(buffer+len,"%d:%dM ",i,
		     (res.Memories[i].SIMMSize > 1024) ?
		     res.Memories[i].SIMMSize>>20 :
		     res.Memories[i].SIMMSize);
    if ( i == MAX_MEMS-1)
      len += sprintf(buffer+len,"\n");
  }

  /* TLB */
  len += sprintf(buffer+len,"TLB");
  switch(res.VitalProductData.TLBAttrib)
  {
    case CombinedTLB:
      len += sprintf(buffer+len,": %d entries\n",
		     res.VitalProductData.TLBSize);
      break;
    case SplitTLB:
      len += sprintf(buffer+len,": (split I/D) %d/%d entries\n",
		     res.VitalProductData.I_TLBSize,
		     res.VitalProductData.D_TLBSize);
      break;
    case NoneTLB:
      len += sprintf(buffer+len,": not present\n");
      break;
  }

  /* L1 */
  len += sprintf(buffer+len,"L1: ");
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
    
    len += sprintf(buffer+len,"L2: %dkB %s\n",
		   ((inb(IBM_L2_STATUS) >> 5) & 1) ? 512 : 256,
                   (inb(IBM_SYS_CTL) & 64) ? "enabled" : "disabled");
  }
  else
  {
    len += sprintf(buffer+len,"L2: not present\n");
  }
#if 0  
  len+= sprintf(buffer+len,"Equip register %x\n",
		inb(IBM_EQUIP_PRESENT));
  len+= sprintf(buffer+len,"L2Status register %x\n",
		inb(IBM_L2_STATUS));
#endif
#endif

  
  return len;
}
