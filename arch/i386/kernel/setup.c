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
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/smp.h>

/*
 * Tell us the machine setup..
 */
char hard_math = 0;		/* set by kernel/head.S */
char x86 = 0;			/* set by kernel/head.S to 3..6 */
char x86_model = 0;		/* set by kernel/head.S */
char x86_mask = 0;		/* set by kernel/head.S */
int x86_capability = 0;		/* set by kernel/head.S */
int fdiv_bug = 0;		/* set if Pentium(TM) with FP bug */
int have_cpuid = 0;             /* set if CPUID instruction works */

char x86_vendor_id[13] = "unknown";

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
			if (to != command_line) to--;
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

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE) {
		initrd_start = INITRD_START;
		initrd_end = INITRD_START+INITRD_SIZE;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    initrd_end,memory_end);
			initrd_start = 0;
		}
	}
#endif

	/* request io space for devices used on all i[345]86 PC'S */
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x20,"dma page reg");
	request_region(0xc0,0x20,"dma2");
	request_region(0xf0,0x10,"npu");
}

static const char * i486model(unsigned int nr)
{
	static const char *model[] = {
		"0","DX","SX","DX/2","4","SX/2","6","DX/2-WB","DX/4","DX/4-WB",
		"10","11","12","13","Am5x86-WT","Am5x86-WB"
	};
	if (nr < sizeof(model)/sizeof(char *))
		return model[nr];
	return NULL;
}

static const char * i586model(unsigned int nr)
{
	static const char *model[] = {
		"0", "Pentium 60/66","Pentium 75+","OverDrive PODP5V83"
	};
	if (nr < sizeof(model)/sizeof(char *))
		return model[nr];
	return NULL;
}

static const char * i686model(unsigned int nr)
{
	static const char *model[] = {
		"PPro A-step", "Pentium Pro"
	};
	if (nr < sizeof(model)/sizeof(char *))
		return model[nr];
	return NULL;
}

static const char * getmodel(int x86, int model)
{
        const char *p = NULL;
        static char nbuf[12];
	switch (x86) {
		case 4:
			p = i486model(model);
			break;
		case 5:
			p = i586model(model);
			break;
		case 6:
			p = i686model(model);
			break;
	}
        if (p)
                return p;

        sprintf(nbuf, "%d", model);
        return nbuf;
}

int get_cpuinfo(char * buffer)
{
        int i, len = 0;
        static const char *x86_cap_flags[] = {
                "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
                "cx8", "apic", "10", "11", "mtrr", "pge", "mca", "cmov",
                "16", "17", "18", "19", "20", "21", "22", "mmx",
                "24", "25", "26", "27", "28", "29", "30", "31"
        };
        
#ifdef __SMP__
        int n;

#define CD(X)		(cpu_data[n].X)
/* SMP has the wrong name for loops_per_sec */
#define loops_per_sec	udelay_val
#define CPUN n

        for ( n = 0 ; n < 32 ; n++ ) {
                if ( cpu_present_map & (1<<n) ) {
                        if (len) buffer[len++] = '\n'; 

#else
#define CD(X) (X)
#define CPUN 0
#endif

                        len += sprintf(buffer+len,"processor\t: %d\n"
                                       "cpu\t\t: %c86\n"
                                       "model\t\t: %s\n"
                                       "vendor_id\t: %s\n",
                                       CPUN,
                                       CD(x86)+'0',
                                       CD(have_cpuid) ? 
                                         getmodel(CD(x86), CD(x86_model)) :
                                         "unknown",
                                       CD(x86_vendor_id));
        
                        if (CD(x86_mask))
                                len += sprintf(buffer+len,
                                               "stepping\t: %d\n",
                                               CD(x86_mask));
                        else
                                len += sprintf(buffer+len, 
                                               "stepping\t: unknown\n");
        
                        len += sprintf(buffer+len,
                                       "fdiv_bug\t: %s\n"
                                       "hlt_bug\t\t: %s\n"
                                       "fpu\t\t: %s\n"
                                       "fpu_exception\t: %s\n"
                                       "cpuid\t\t: %s\n"
                                       "wp\t\t: %s\n"
                                       "flags\t\t:",
                                       CD(fdiv_bug) ? "yes" : "no",
                                       CD(hlt_works_ok) ? "no" : "yes",
                                       CD(hard_math) ? "yes" : "no",
                                       (CD(hard_math) && ignore_irq13)
                                         ? "yes" : "no",
                                       CD(have_cpuid) ? "yes" : "no",
                                       CD(wp_works_ok) ? "yes" : "no");
        
                        for ( i = 0 ; i < 32 ; i++ ) {
                                if ( CD(x86_capability) & (1 << i) ) {
                                        len += sprintf(buffer+len, " %s",
                                                       x86_cap_flags[i]);
                                }
                        }
                        len += sprintf(buffer+len,
                                       "\nbogomips\t: %lu.%02lu\n",
                                       CD(loops_per_sec)/500000,
                                       (CD(loops_per_sec)/5000) % 100);
#ifdef __SMP__
                }
        }
#endif
        return len;
}
