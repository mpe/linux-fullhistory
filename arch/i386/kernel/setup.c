/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/smp.h>

/*
 * Tell us the machine setup..
 */
char hard_math = 0;		/* set by boot/head.S */
char x86 = 0;			/* set by boot/head.S to 3 or 4 */
char x86_model = 0;		/* set by boot/head.S */
char x86_mask = 0;		/* set by boot/head.S */
int x86_capability = 0;		/* set by boot/head.S */
int fdiv_bug = 0;		/* set if Pentium(TM) with FP bug */

char x86_vendor_id[13] = "Unknown";

char ignore_irq13 = 0;		/* set if exception 16 works */
char wp_works_ok = -1;		/* set if paging hardware honours WP */ 
char hlt_works_ok = 1;		/* set if the "hlt" instruction works */

/*
 * Bus types ..
 */
int EISA_bus = 0;

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
#ifdef CONFIG_APM
struct apm_bios_info apm_bios_info;
#endif

unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern int root_mountflags;
extern int _etext, _edata, _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
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
#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;
	static unsigned char smptrap=0;

	if(smptrap==1)
	{
		return;
	}
	smptrap=1;

 	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
#ifdef CONFIG_APM
	apm_bios_info = APM_BIOS_INFO;
#endif
	aux_device_present = AUX_DEVICE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= PAGE_MASK;
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_MAX_16M
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
#endif
	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	memory_start = (unsigned long) &_end;
	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk = TASK_SIZE + (unsigned long) &_end;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		/*
		 * "mem=nopentium" disables the 4MB page tables.
		 * "mem=XXX[kKmM]" overrides the BIOS-reported
		 * memory size
		 */
		if (c == ' ' && *(const unsigned long *)from == *(const unsigned long *)"mem=") {
			if (!memcmp(from+4, "nopentium", 9)) {
				from += 9+4;
				x86_capability &= ~8;
			} else {
				memory_end = simple_strtoul(from+4, &from, 0);
				if ( *from == 'K' || *from == 'k' ) {
					memory_end = memory_end << 10;
					from++;
				} else if ( *from == 'M' || *from == 'm' ) {
					memory_end = memory_end << 20;
					from++;
				}
			}
		}
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;
	/* request io space for devices used on all i[345]86 PC'S */
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x70,0x10,"rtc");
	request_region(0x80,0x20,"dma page reg");
	request_region(0xc0,0x20,"dma2");
	request_region(0xf0,0x10,"npu");
}

static const char * i486model(unsigned int nr)
{
	static const char *model[] = {
		"0", "DX","SX","DX/2","4","SX/2","6","DX/2-WB","DX/4","DX/4-WB"
	};
	if (nr < sizeof(model)/sizeof(char *))
		return model[nr];
	return "Unknown";
}

static const char * i586model(unsigned int nr)
{
	static const char *model[] = {
		"0", "Pentium 60/66","Pentium 75+"
	};
	if (nr < sizeof(model)/sizeof(char *))
		return model[nr];
	return "Unknown";
}

static const char * getmodel(int x86, int model)
{
	switch (x86) {
		case 4:
			return i486model(model);
		case 5:
			return i586model(model);
	}
	return "Unknown";
}

int get_cpuinfo(char * buffer)
{
	char mask[2];
#ifndef __SMP__	
	mask[0] = x86_mask+'@';
	mask[1] = '\0';
	return sprintf(buffer,"cpu\t\t: %c86\n"
			      "model\t\t: %s\n"
			      "mask\t\t: %s\n"
			      "vid\t\t: %s\n"
			      "fdiv_bug\t: %s\n"
			      "math\t\t: %s\n"
			      "hlt\t\t: %s\n"
			      "wp\t\t: %s\n"
			      "Integrated NPU\t: %s\n"
			      "Enhanced VM86\t: %s\n"
			      "IO Breakpoints\t: %s\n"
			      "4MB Pages\t: %s\n"
			      "TS Counters\t: %s\n"
			      "Pentium MSR\t: %s\n"
			      "Mach. Ch. Exep.\t: %s\n"
			      "CMPXCHGB8B\t: %s\n"
		              "BogoMips\t: %lu.%02lu\n",
			      x86+'0', 
			      getmodel(x86, x86_model),
			      x86_mask ? mask : "Unknown",
			      x86_vendor_id,
			      fdiv_bug ? "yes" : "no",
			      hard_math ? "yes" : "no",
			      hlt_works_ok ? "yes" : "no",
			      wp_works_ok ? "yes" : "no",
			      x86_capability & 1 ? "yes" : "no",
			      x86_capability & 2 ? "yes" : "no",
			      x86_capability & 4 ? "yes" : "no",
			      x86_capability & 8 ? "yes" : "no",
			      x86_capability & 16 ? "yes" : "no",
			      x86_capability & 32 ? "yes" : "no",
			      x86_capability & 128 ? "yes" : "no",
			      x86_capability & 256 ? "yes" : "no",
		              loops_per_sec/500000, (loops_per_sec/5000) % 100
			      );
#else
	char *bp=buffer;
	int i;	
	bp+=sprintf(bp,"cpu\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%c86             ",cpu_data[i].x86+'0');
	bp+=sprintf(bp,"\nmodel\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s",getmodel(cpu_data[i].x86,cpu_data[i].x86_model));
	bp+=sprintf(bp,"\nmask\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
		{
			mask[0] = cpu_data[i].x86_mask+'@';
			mask[1] = '\0';		
			bp+=sprintf(bp,"%-16s", cpu_data[i].x86_mask ? mask : "Unknown");
		}
	bp+=sprintf(bp,"\nvid\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].x86_vendor_id);
	bp+=sprintf(bp,"\nfdiv_bug\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].fdiv_bug?"yes":"no");
	bp+=sprintf(bp,"\nmath\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].hard_math?"yes":"no");
	bp+=sprintf(bp,"\nhlt\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].hlt_works_ok?"yes":"no");
	bp+=sprintf(bp,"\nwp\t\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].wp_works_ok?"yes":"no");
	bp+=sprintf(bp,"\nIntegrated NPU\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].x86_capability&1?"yes":"no");
	bp+=sprintf(bp,"\nEnhanced VM86\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", cpu_data[i].x86_capability&2?"yes":"no");
	bp+=sprintf(bp,"\nIO Breakpoints\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", (cpu_data[i].x86_capability&4)?"yes":"no");
	bp+=sprintf(bp,"\n4MB Pages\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", (cpu_data[i].x86_capability)&8?"yes":"no");
	bp+=sprintf(bp,"\nTS Counters\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", (cpu_data[i].x86_capability&16)?"yes":"no");
	bp+=sprintf(bp,"\nPentium MSR\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", (cpu_data[i].x86_capability&32)?"yes":"no");
	bp+=sprintf(bp,"\nMach. Ch. Exep.\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", (cpu_data[i].x86_capability&128)?"yes":"no");
	bp+=sprintf(bp,"\nCMPXCHG8B\t: ");
	for(i=0;i<32;i++)
		if(cpu_present_map&(1<<i))
			bp+=sprintf(bp,"%-16s", (cpu_data[i].x86_capability&256)?"yes":"no");
	bp+=sprintf(bp,"\nBogoMips\t: ");
	for(i=0;i<32;i++)
	{
		char tmp[17];
		if(cpu_present_map&(1<<i))
		{
			sprintf(tmp,"%lu.%02lu",cpu_data[i].udelay_val/500000L,
						   (cpu_data[i].udelay_val/5000L)%100);
			bp+=sprintf(bp,"%-16s",tmp);
		}
	}
	*bp++='\n';
	return bp-buffer;
#endif			      
}
