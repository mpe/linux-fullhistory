/*
 * linux/kernel/math/error.c
 *
 * (C) 1991 Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>

void math_error(void)
{
	if (last_task_used_math)
		send_sig(SIGFPE,last_task_used_math,1);
	__asm__("fnclex");
}
