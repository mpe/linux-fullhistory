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
#include <linux/smp.h>
#include <linux/reboot.h>

asmlinkage void sys_sync(void);	/* it's really int */
extern void do_unblank_screen(void);
extern int C_A_D;

int panic_timeout = 0;

void panic_setup(char *str, int *ints)
{
	if (ints[0] == 1)
		panic_timeout = ints[1];
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

	do_unblank_screen();

#ifdef __SMP__
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_STOP_CPU, 0, 0);
#endif
	if (panic_timeout > 0)
	{
		/*
	 	 * Delay timeout seconds before rebooting the machine. 
		 * We can't use the "normal" timers since we just panicked..
	 	 */
		printk(KERN_EMERG "Rebooting in %d seconds..",panic_timeout);
		for(i = 0; i < (panic_timeout*1000); i++)
			udelay(1000);
		/*
		 *	Should we run the reboot notifier. For the moment Im
		 *	choosing not too. It might crash, be corrupt or do
		 *	more harm than good for other reasons.
		 */
		machine_restart(NULL);
	}
#ifdef __sparc__
	printk("Press L1-A to return to the boot prom\n");
#endif
	sti();
	for(;;);
}

