/*
 * linux/arch/arm/lib/system.c
 *
 * Copyright (C) 1999 Russell King
 *
 * Converted from ASM version 04/09/1999
 */
#include <linux/kernel.h>

extern void abort(void)
{
	void *lr = __builtin_return_address(0);

	printk(KERN_CRIT "kernel abort from %p!  (Please report to rmk@arm.linux.org.uk)\n",
		lr);

	/* force an oops */
	*(int *)0 = 0;

	/* if that doesn't kill us, halt */
	panic("Oops failed to kill thread");
}
