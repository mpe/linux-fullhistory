/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (including mm and fs)
 * to indicate a major problem.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/interrupt.h>
#include <linux/nmi.h>

int panic_timeout;
int panic_on_oops;
int tainted;

EXPORT_SYMBOL(panic_timeout);

struct notifier_block *panic_notifier_list;

EXPORT_SYMBOL(panic_notifier_list);

static int __init panic_setup(char *str)
{
	panic_timeout = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("panic=", panic_setup);

static long no_blink(long time)
{
	return 0;
}

/* Returns how long it waited in ms */
long (*panic_blink)(long time);
EXPORT_SYMBOL(panic_blink);

/**
 *	panic - halt the system
 *	@fmt: The text string to print
 *
 *	Display a message, then perform cleanups.
 *
 *	This function never returns.
 */
 
NORET_TYPE void panic(const char * fmt, ...)
{
	long i;
	static char buf[1024];
	va_list args;
#if defined(CONFIG_ARCH_S390)
        unsigned long caller = (unsigned long) __builtin_return_address(0);
#endif

	bust_spinlocks(1);
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printk(KERN_EMERG "Kernel panic - not syncing: %s\n",buf);
	bust_spinlocks(0);

#ifdef CONFIG_SMP
	smp_send_stop();
#endif

	notifier_call_chain(&panic_notifier_list, 0, buf);

	if (!panic_blink)
		panic_blink = no_blink;

	if (panic_timeout > 0)
	{
		/*
	 	 * Delay timeout seconds before rebooting the machine. 
		 * We can't use the "normal" timers since we just panicked..
	 	 */
		printk(KERN_EMERG "Rebooting in %d seconds..",panic_timeout);
		for (i = 0; i < panic_timeout*1000; ) {
			touch_nmi_watchdog();
			i += panic_blink(i);
			mdelay(1);
			i++;
		}
		/*
		 *	Should we run the reboot notifier. For the moment Im
		 *	choosing not too. It might crash, be corrupt or do
		 *	more harm than good for other reasons.
		 */
		machine_restart(NULL);
	}
#ifdef __sparc__
	{
		extern int stop_a_enabled;
		/* Make sure the user can actually press L1-A */
		stop_a_enabled = 1;
		printk(KERN_EMERG "Press L1-A to return to the boot prom\n");
	}
#endif
#if defined(CONFIG_ARCH_S390)
        disabled_wait(caller);
#endif
	local_irq_enable();
	for (i = 0;;) {
		i += panic_blink(i);
		mdelay(1);
		i++;
	}
}

EXPORT_SYMBOL(panic);

/**
 *	print_tainted - return a string to represent the kernel taint state.
 *
 *  'P' - Proprietary module has been loaded.
 *  'F' - Module has been forcibly loaded.
 *  'S' - SMP with CPUs not designed for SMP.
 *  'R' - User forced a module unload.
 *  'M' - Machine had a machine check experience.
 *  'B' - System has hit bad_page.
 *
 *	The string is overwritten by the next call to print_taint().
 */
 
const char *print_tainted(void)
{
	static char buf[20];
	if (tainted) {
		snprintf(buf, sizeof(buf), "Tainted: %c%c%c%c%c%c",
			tainted & TAINT_PROPRIETARY_MODULE ? 'P' : 'G',
			tainted & TAINT_FORCED_MODULE ? 'F' : ' ',
			tainted & TAINT_UNSAFE_SMP ? 'S' : ' ',
			tainted & TAINT_FORCED_RMMOD ? 'R' : ' ',
 			tainted & TAINT_MACHINE_CHECK ? 'M' : ' ',
			tainted & TAINT_BAD_PAGE ? 'B' : ' ');
	}
	else
		snprintf(buf, sizeof(buf), "Not tainted");
	return(buf);
}

void add_taint(unsigned flag)
{
	tainted |= flag;
}
EXPORT_SYMBOL(add_taint);
