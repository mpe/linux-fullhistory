/*
 *  arch/mips/vm86.c
 *
 *  Copyright (C) 1994  Waldorf GMBH,
 *  written by Ralf Baechle
 */
#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/vm86.h>

asmlinkage int sys_vm86(struct vm86_struct * v86)
{
	return -ENOSYS;
}
