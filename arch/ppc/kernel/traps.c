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

#include <asm/ppc_machine.h>

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
	send_sig(signr, current, 1);
	if (!user_mode(regs))
	{
		printk("Failure in kernel at PC: %x, MSR: %x\n", regs->nip, regs->msr);
		while (1) ;
	}
}

MachineCheckException(struct pt_regs *regs)
{
	unsigned long *eagle_ip = (unsigned long *)0x80000CF8;
	unsigned long *eagle_id = (unsigned long *)0x80000CFC;
	printk("Machine check at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
#if 0	
	*eagle_ip = 0xC0000080;  /* Memory error register */
	printk("Error regs = %08X", *eagle_id);
	*eagle_ip = 0xC4000080;  /* Memory error register */
	printk("/%08X", *eagle_id);
	*eagle_ip = 0xC8000080;  /* Memory error register */
	printk("/%08X\n", *eagle_id);
#endif
	_exception(SIGSEGV, regs);	
}

ProgramCheckException(struct pt_regs *regs)
{
	printk("Program check at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	while(1) ;
	_exception(SIGILL, regs);
}

FloatingPointCheckException(struct pt_regs *regs)
{
	printk("Floating point check at PC: %x[%x], SR: %x\n", regs->nip, va_to_phys(regs->nip), regs->msr);
	_exception(SIGFPE, regs);	
}

AlignmentException(struct pt_regs *regs)
{
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
	printk("HASH = %08X/%08X, MISS = %08X/%08X, CMP = %08X/%08X\n", regs->hash1, regs->hash2, regs->imiss, regs->dmiss, regs->icmp, regs->dcmp);
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
	dump_buf(regs->nip, 32);
	dump_buf((regs->nip&0x0FFFFFFF)|KERNELBASE, 32);
}

trace_syscall(struct pt_regs *regs)
{
	printk("Task: %08X(%d), PC: %08X/%08X, Syscall: %3d, Result: %d\n", current, current->pid, regs->nip, regs->link, regs->gpr[0], regs->gpr[3]);
}

