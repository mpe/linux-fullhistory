/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */
#include <stdarg.h>

#include <linux/kernel.h>
#include <linux/sched.h>

asmlinkage void sys_sync(void);	/* it's really int */

extern int vsprintf(char * buf, const char * fmt, va_list args);

volatile void panic(const char * fmt, ...)
{
	extern int log_to_console;
	static char buf[1024];
	va_list args;

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	log_to_console = 1;
	printk("Kernel panic: %s\n",buf);
	if (current == task[0])
		printk("In swapper task - not syncing\n");
	else
		sys_sync();
	for(;;);
}
