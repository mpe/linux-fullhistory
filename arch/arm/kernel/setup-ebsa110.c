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
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/utsname.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/pgtable.h>

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE	"root=nfs rw console=ttyS1,38400n8"
#endif
#define MEM_SIZE	(16*1024*1024)

#define COMMAND_LINE_SIZE 256

unsigned char aux_device_present;
unsigned long arm_id;
extern int root_mountflags;
extern int _etext, _edata, _end;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */

static inline void setup_ramdisk (void)
{
    rd_image_start	= 0;
    rd_prompt		= 1;
    rd_doload		= 1;
}
#else
#define setup_ramdisk()	
#endif

static char default_command_line[] = CONFIG_CMDLINE;
static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

struct processor processor;
extern const struct processor sa110_processor_functions;

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
	char c = ' ', *to = command_line, *from;
	int len = 0;

	memory_start = (unsigned long)&_end;
	memory_end = 0xc0000000 + MEM_SIZE;
	from = default_command_line;

	processor = sa110_processor_functions;
	processor._proc_init ();

	ROOT_DEV		= 0x00ff;
	setup_ramdisk();

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
