/*
 *  linux/arch/ppc/kernel/traps.c
 *
 *  Copyright (C) 1995  Gary Thomas
 *  Adapted for PowerPC by Gary Thomas
 */

/*
 * This file handles the architecture-dependent parts of hardware exceptions
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include "ppc_machine.h"

/*
 * Trap & Exception support
 */

void
trap_init(void)
{
}

void
_exception(int signr, struct pt_regs *regs)
{
	dump_regs(regs);
	force_sig(signr, current);
	if (!user_mode(regs))
	{
		printk("Failure in kernel at PC: %x, MSR: %x\n", regs->nip, regs->msr);
		while (1) ;
	}
}

MachineCheckException(struct pt_regs *regs)
{
	printk("Machine check at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	_exception(SIGSEGV, regs);	
}

ProgramCheckException(struct pt_regs *regs)
{
	printk("Program check at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	if (current->flags & PF_PTRACED)
	{
		_exception(SIGTRAP, regs);
	} else
	{
		_exception(SIGILL, regs);
	}
}

SingleStepException(struct pt_regs *regs)
{
	printk("Single step at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */
	_exception(SIGTRAP, regs);	
}

FloatingPointCheckException(struct pt_regs *regs)
{
	printk("Floating point check at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	_exception(SIGFPE, regs);	
}

AlignmentException(struct pt_regs *regs)
{
	printk("Alignment error at PC: %x, SR: %x\n", regs->nip, regs->msr);
	dump_regs(regs);
	cnpause();
	printk("Alignment error at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	_exception(SIGBUS, regs);	
}

bad_stack(struct pt_regs *regs)
{
	printk("Kernel stack overflow at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	dump_regs(regs);
	while (1) ;
}

dump_regs(struct pt_regs *regs)
{
	int i;
	printk("NIP: %08X, MSR: %08X, XER: %08X, LR: %08X, FRAME: %08X\n", regs->nip, regs->msr, regs->xer, regs->link, regs);
#if 0	
	printk("HASH = %08X/%08X, MISS = %08X/%08X, CMP = %08X/%08X\n", regs->hash1, regs->hash2, regs->imiss, regs->dmiss, regs->icmp, regs->dcmp);
#endif	
	printk("TASK = %x[%d] '%s'\n", current, current->pid, current->comm);
	for (i = 0;  i < 32;  i++)
	{
		if ((i % 8) == 0)
		{
			printk("GPR%02d: ", i);
		}
		printk("%08X ", regs->gpr[i]);
		if ((i % 8) == 7)
		{
			printk("\n");
		}
	}
#if 0	
	if (regs->nip >= 0x1000)
		dump_buf(regs->nip-32, 64);
	dump_buf((regs->nip&0x0FFFFFFF)|KERNELBASE, 32);
#endif
}

trace_syscall(struct pt_regs *regs)
{
	static int count;
	printk("Task: %08X(%d), PC: %08X/%08X, Syscall: %3d, Result: %s%d\n", current, current->pid, regs->nip, regs->link, regs->gpr[0], regs->ccr&0x10000000?"Error=":"", regs->gpr[3]);
	if (++count == 20)
	{
		count = 0;
		cnpause();
	}
}

