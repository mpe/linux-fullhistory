/* $Id: ioctl32.c,v 1.3 1997/05/27 19:30:13 jj Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>

#include <asm/types.h>
#include <asm/uaccess.h>

/* As gcc will warn about casting u32 to some ptr, we have to cast it to unsigned long first, and that's what is A() for.
 * You just do (void *)A(x), instead of having to type (void *)((unsigned long)x) or instead of just (void *)x, which will
 * produce warnings */
#define A(x) ((unsigned long)x)

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
 
asmlinkage int sys32_ioctl(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct file * filp;
	int error = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		goto out;
	if (!filp->f_op || !filp->f_op->ioctl) {
		error = sys_ioctl (fd, cmd, (unsigned long)arg);
		goto out;
	}
	error = 0;
	switch (cmd) {
		default:
			error = sys_ioctl (fd, cmd, (unsigned long)arg);
			goto out;
	}
out:
	if (error == -EINVAL) {
		printk ("sys32_ioctl on %016lx's %08x returns EINVAL\n", filp->f_op ? (long)filp->f_op->ioctl : 0UL, cmd);
	}
	unlock_kernel();
	return error;
}
