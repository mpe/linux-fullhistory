/* solaris.c: Solaris binary emulation, whee...
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/errno.h>
#include <asm/solerrno.h>

extern asmlinkage int sys_open(const char *,int,int);

asmlinkage int solaris_open(const char *filename, int flags, int mode)
{
	int newflags;
	int ret;

	lock_kernel();
	newflags = flags & 0xf;
	flags &= ~0xf;
	if(flags & 0x8050)
		newflags |= FASYNC;
	if(flags & 0x80)
		newflags |= O_NONBLOCK;
	if(flags & 0x100)
		newflags |= O_CREAT;
	if(flags & 0x200)
		newflags |= O_TRUNC;
	if(flags & 0x400)
		newflags |= O_EXCL;
	if(flags & 0x800)
		newflags |= O_NOCTTY;
	ret = sys_open(filename, newflags, mode);
	unlock_kernel();
	return ret;
}


