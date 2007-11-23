/*
 *  linux/abi/emulate.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 */

/*
 * Emulate.c contains the entry point for the 'lcall 7,xxx' handler.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/segment.h>
#include <linux/ptrace.h>

#include <asm/segment.h>
#include <asm/system.h>

extern "C" void iABI_emulate(struct pt_regs * regs)
{
	printk("lcall 7,xxx: eax = %08x\n",regs->eax);
}
