/*
 *  include/asm-mips/bugs.h
 *
 *  Copyright (C) 1995  Waldorf Electronics
 *  written by Ralf Baechle
 */
#include <asm/bootinfo.h>

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

extern struct bootinfo boot_info;

static void check_wait(void)
{
	printk("Checking for 'wait' instruction... ");
	switch(boot_info.cputype) {
		case CPU_R4200: 
		case CPU_R4600: 
			wait_available = 1;
			printk(" available.\n");
			break;
		default:
			printk(" unavailable.\n");
			break;
		}
}

static void check_bugs(void)
{
	check_wait();
}
