/*
 * kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/sched.h>

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
	unsigned long i;

	printk("%s %ld\n", str, err);
	for (i = 0 ; i++ ; i < 500000000)
		/* pause */;
	halt();
}

extern asmlinkage void entMM(void);

void trap_init(void)
{
	unsigned long gptr;

	__asm__("br %0,___tmp\n"
		"___tmp:\tldgp %0,0(%0)"
		: "=r" (gptr));
	wrkgp(gptr);
	wrent(entMM, 2);
}
