/* ioport.c:  I/O access on the Sparc. Work in progress.. Most of the things
 *            in this file are for the sole purpose of getting the kernel
 *            through the compiler. :-)
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
