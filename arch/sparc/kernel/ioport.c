/* ioport.c:  I/O access on the Sparc. Work in progress.. Most of the things
 *            in this file are for the sole purpose of getting the kernel
 *	      throught the compiler. :-)
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

/*
 * this changes the io permissions bitmap in the current task.
 */
asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	return 0;
}
