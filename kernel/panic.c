/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */
#include <linux/kernel.h>
#include <linux/sched.h>

extern "C" void sys_sync(void);	/* it's really int */

volatile void panic(const char * s)
{
	extern int log_to_console;

	log_to_console = 1;
	printk("Kernel panic: %s\n",s);
	if (current == task[0])
		printk("In swapper task - not syncing\n");
	else
		sys_sync();
	for(;;);
}
