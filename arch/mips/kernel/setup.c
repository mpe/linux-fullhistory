/*
 *  linux/arch/mips/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
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

#include <asm/bootinfo.h>
#include <asm/segment.h>
#include <asm/system.h>

/*
 * Tell us the machine setup..
 */
char wait_available;		/* set if the "wait" instruction available */

/*
 * Bus types ..
 */
int EISA_bus = 0;

/*
 * Setup options
 */
struct drive_info_struct drive_info;
struct screen_info screen_info;

unsigned char aux_device_present;
extern int ramdisk_size;
extern int root_mountflags;
extern int end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * Initialise this structure so that it will be placed in the
 * .data section of the object file
 */
struct bootinfo boot_info = BOOT_INFO;

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
#define EXT_MEM (boot_info.memupper)
#define DRIVE_INFO (boot_info.drive_info)
#define SCREEN_INFO (screen_info)
#define MOUNT_ROOT_RDONLY (boot_info.mount_root_rdonly)
#define RAMDISK_SIZE (boot_info.ramdisk_size)
#if 0
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#endif
#define COMMAND_LINE (boot_info.command_line)

static char command_line[CL_SIZE] = { 0, };

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

#if 0
 	ROOT_DEV = ORIG_ROOT_DEV;
#endif
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
#if 0
	aux_device_present = AUX_DEVICE_INFO;
#endif
	memory_end = EXT_MEM;
	memory_end &= PAGE_MASK;
	ramdisk_size = RAMDISK_SIZE;
	if (MOUNT_ROOT_RDONLY)
		root_mountflags |= MS_RDONLY;
	memory_start = (unsigned long) &end - KERNELBASE;

	for (;;) {
		if (c == ' ' && *(unsigned long *)from == *(unsigned long *)"mem=") {
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
		if (CL_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;
}
