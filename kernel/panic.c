/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (including mm and fs)
 * to indicate a major problem.
 */
#include <stdarg.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>

asmlinkage void sys_sync(void);	/* it's really int */
extern void hard_reset_now(void);

static int timeout = -1;

void panic_setup(char *str, int *ints)
{
	if (ints[0] == 1)
		timeout = ints[1];
}

NORET_TYPE void panic(const char * fmt, ...)
{
	static char buf[1024];
	va_list args;
	int i;

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	printk(KERN_EMERG "Kernel panic: %s\n",buf);
	if (current == task[0])
		printk(KERN_EMERG "In swapper task - not syncing\n");
	else
		sys_sync();
	if (timeout >= 0)
	{
		/*
	 	 * Delay timeout seconds before rebooting the machine. 
		 * We can't use the "normal" timers since we just paniced..
	 	 */
		printk(KERN_EMERG "Rebooting in %d seconds..",timeout);
		for(i = 0; i < (timeout*1000); i++)
			udelay(1000);
		hard_reset_now();
	}
	for(;;);
}

/*
 * GCC 2.5.8 doesn't always optimize correctly; see include/asm/segment.h
 */

int bad_user_access_length(void)
{
        panic("bad_user_access_length executed (not cool, dude)");
}
