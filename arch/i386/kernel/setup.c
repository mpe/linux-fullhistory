/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
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

#include <asm/segment.h>
#include <asm/system.h>

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
char wp_works_ok = 0;		/* set if paging hardware honours WP */ 
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

unsigned char aux_device_present;
extern int ramdisk_size;
extern int root_mountflags;
extern int _etext, _edata, _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_SIZE (*(unsigned short *) (PARAM+0x1F8))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256

static char command_line[COMMAND_LINE_SIZE] = { 0, };

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
	aux_device_present = AUX_DEVICE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= PAGE_MASK;
	ramdisk_size = RAMDISK_SIZE;
#ifdef CONFIG_MAX_16M
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
#endif
	if (MOUNT_ROOT_RDONLY)
		root_mountflags |= MS_RDONLY;
	memory_start = (unsigned long) &_end;
	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk = TASK_SIZE + (unsigned long) &_end;

	for (;;) {
		if (c == ' ' && *(const unsigned long *)from == *(const unsigned long *)"mem=") {
			memory_end = simple_strtoul(from+4, &from, 0);
			if ( *from == 'K' || *from == 'k' ) {
				memory_end = memory_end << 10;
				from++;
			} else if ( *from == 'M' || *from == 'm' ) {
				memory_end = memory_end << 20;
				from++;
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
	request_region(0xf0,0x2,"npu");
	request_region(0xf8,0x8,"npu");
}

int get_cpuinfo(char * buffer)
{
	static const char *model[2][9]={{"DX","SX","DX/2","4","SX/2","6",
				"DX/2-WB","DX/4"},
			{"Pentium 60/66","Pentium 90/100","3",
				"4","5","6","7","8"}};
	char mask[2];
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
			      x86_model ? model[x86-4][x86_model-1] : "Unknown",
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
}
