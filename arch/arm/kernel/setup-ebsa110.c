/*
 *  linux/arch/arm/kernel/setup-sa.c
 *
 *  Copyright (C) 1995, 1996 Russell King
 */

/*
 * This file obtains various parameters about the system that the kernel
 * is running on.
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
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/utsname.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/procinfo.h>
#include <asm/segment.h>
#include <asm/setup.h>
#include <asm/system.h>

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE	"root=nfs rw console=ttyS1,38400n8"
#endif
#define COMMAND_LINE_SIZE 256

#define MEM_SIZE	(16*1024*1024)

struct screen_info screen_info;
struct processor processor;
unsigned char aux_device_present;

extern const struct processor sa110_processor_functions;

struct armversions armidlist[] = {
	{ 0x4401a100, 0xfffffff0, F_MMU|F_32BIT	, "DEC",	"sa110"		, &sa110_processor_functions  , "sa1x"},
	{ 0x00000000, 0x00000000, 0		, "***",	"*unknown*"	, NULL			      , NULL  }
};

unsigned long arm_id;
int armidindex;

extern int root_mountflags;
extern int _etext, _edata, _end;

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */

static inline void setup_ramdisk(int start, int prompt, int load)
{
	rd_image_start	= start;
	rd_prompt	= prompt;
	rd_doload	= load;
}
#else
#define setup_ramdisk(start,prompt,load)
#endif

#ifdef PARAMS_BASE
static struct param_struct *params = (struct param_struct *)PARAMS_BASE;

static inline char *setup_params(unsigned long *mem_end_p)
{
	ROOT_DEV	  = to_kdev_t(params->u1.s.rootdev);
	ORIG_X		  = params->u1.s.video_x;
	ORIG_Y		  = params->u1.s.video_y;
	ORIG_VIDEO_COLS	  = params->u1.s.video_num_cols;
	ORIG_VIDEO_LINES  = params->u1.s.video_num_rows;

	setup_ramdisk(params->u1.s.rd_start,
		      (params->u1.s.flags & FLAG_RDPROMPT) == 0,
		      (params->u1.s.flags & FLAG_RDLOAD) == 0);

	*mem_end_p = 0xc0000000 + MEM_SIZE;

	return params->commandline;
}
#else
static char default_command_line[] = CONFIG_CMDLINE;

static inline char *setup_params(unsigned long *mem_end_p)
{
	ROOT_DEV	  = 0x00ff;

	setup_ramdisk(0, 1, 1);

	*mem_end_p = 0xc0000000 + MEM_SIZE;

	return default_command_line;
}
#endif

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	unsigned long memory_start, memory_end;
	char c = ' ', *to = command_line, *from;
	int len = 0;

	memory_start = (unsigned long)&_end;

	armidindex = 0;

	processor = sa110_processor_functions;
	processor._proc_init();

	from = setup_params(&memory_end);

	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code	 = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data	 = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk	 = TASK_SIZE + (unsigned long) &_end;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		if (c == ' ' &&
		    from[0] == 'm' &&
		    from[1] == 'e' &&
		    from[2] == 'm' &&
		    from[3] == '=') {
			memory_end = simple_strtoul(from+4, &from, 0);
			if ( *from == 'K' || *from == 'k' ) {
				memory_end = memory_end << 10;
				from++;
			} else if ( *from == 'M' || *from == 'm' ) {
				memory_end = memory_end << 20;
				from++;
			}
			memory_end = memory_end + PAGE_OFFSET;
		}
		c = *from++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}

	*to = '\0';
	*cmdline_p = command_line;
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;
	strcpy (system_utsname.machine, "sa110");
}

int get_cpuinfo(char * buffer)
{
	int len;

	len = sprintf (buffer,  "CPU:\n"
				"Type\t\t: %s\n"
				"Revision\t: %d\n"
				"Manufacturer\t: %s\n"
				"32bit modes\t: %s\n"
				"BogoMips\t: %lu.%02lu\n",
				"sa110",
				(int)arm_id & 15,
				"DEC",
				"yes",
				(loops_per_sec+2500) / 500000,
				((loops_per_sec+2500) / 5000) % 100);
	return len;
}
