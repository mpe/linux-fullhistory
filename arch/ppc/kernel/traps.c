/*
 *  linux/arch/ppc/kernel/traps.c
 *
 *  Copyright (C) 1995  Gary Thomas
 *  Adapted for PowerPC by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
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
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>

#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

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
	if (!user_mode(regs))
	{
		show_regs(regs);
		print_backtrace(regs->gpr[1]);
		panic("Exception in kernel pc %x signal %d",regs->nip,signr);
	}
	force_sig(signr, current);
}

MachineCheckException(struct pt_regs *regs)
{
	if ( !user_mode(regs) )
	{
		printk("Machine check in kernel mode.\n");
		printk("Caused by (from msr): ");
		printk("regs %08x ",regs);
		switch( regs->msr & 0x0000F000)
		{
		case (1<<12) :
			printk("Machine check signal - probably due to mm fault\n"
				"with mmu off\n");
		break;
		case (1<<13) :
			printk("Transfer error ack signal\n");
		break;
		case (1<<14) :
			printk("Data parity signal\n");
		break;
		case (1<<15) :
			printk("Address parity signal\n");
		break;
		default:
			printk("Unknown values in msr\n");
		}
		show_regs(regs);
		print_backtrace(regs->gpr[1]);
		panic("");
	}
	_exception(SIGSEGV, regs);	
}

void
UnknownException(struct pt_regs *regs)
{
	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);
	_exception(SIGTRAP, regs);	
}

void
InstructionBreakpoint(struct pt_regs *regs)
{
#ifdef CONFIG_XMON
	if (xmon_iabr_match(regs))
		return;
#endif
	_exception(SIGTRAP, regs);
}

void
RunModeException(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs);	
}

ProgramCheckException(struct pt_regs *regs)
{
	if (current->flags & PF_PTRACED)
		_exception(SIGTRAP, regs);
	else
		_exception(SIGILL, regs);
}

SingleStepException(struct pt_regs *regs)
{
	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */
	_exception(SIGTRAP, regs);	
}

AlignmentException(struct pt_regs *regs)
{
	_exception(SIGBUS, regs);	
}

trace_syscall(struct pt_regs *regs)
{
	static int count;
	printk("Task: %08X(%d), PC: %08X/%08X, Syscall: %3d, Result: %s%d\n",
	       current, current->pid, regs->nip, regs->link, regs->gpr[0],
	       regs->ccr&0x10000000?"Error=":"", regs->gpr[3]);
	if (++count == 20)
	{
		count = 0;
	}
}
