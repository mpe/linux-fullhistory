/*
 * Software emulation of some PPC instructions for the 8xx core.
 *
 * Copyright (C) 1998 Dan Malek (dmalek@jlc.net)
 *
 * Software floating emuation for the MPC8xx processor.  I did this mostly
 * because it was easier than trying to get the libraries compiled for
 * software floating point.  The goal is still to get the libraries done,
 * but I lost patience and needed some hacks to at least get init and
 * shells running.  The first problem is the setjmp/longjmp that save
 * and restore the floating point registers.
 *
 * For this emulation, our working registers are found on the register
 * save area.
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

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>

/* Eventually we may need a look-up table, but this works for now.
*/
#define LFD	50
#define LFDU	51
#define STFD	54
#define STFDU	55

/*
 * We return 0 on success, 1 on unimplemented instruction, and EFAULT
 * if a load/store faulted.
 */
int
Soft_emulate_8xx(struct pt_regs *regs)
{
	uint	inst, instword;
	uint	flreg, idxreg, disp;
	uint	retval;
	uint	*ea, *ip;

	retval = 0;

	instword = *((uint *)regs->nip);
	inst = instword >> 26;

	flreg = (instword >> 21) & 0x1f;
	idxreg = (instword >> 16) & 0x1f;
	disp = instword & 0xffff;

	ea = (uint *)(regs->gpr[idxreg] + disp);
	ip = (uint *)&current->tss.fpr[flreg];

	if (inst == LFD) {
		if (copy_from_user(ip, ea, sizeof(double)))
			retval = EFAULT;
	}
	else if (inst == LFDU) {

		if (copy_from_user(ip, ea, sizeof(double)))
			retval = EFAULT;
		else
			regs->gpr[idxreg] = (uint)ea;
	}
	else if (inst == STFD) {

		if (copy_to_user(ea, ip, sizeof(double)))
			retval = EFAULT;
	}
	else if (inst == STFDU) {

		if (copy_to_user(ea, ip, sizeof(double)))
			retval = EFAULT;
		else
			regs->gpr[idxreg] = (uint)ea;
	}
	else {
		retval = 1;
	}

	if (retval == 0)
		regs->nip += 4;
	return(retval);
}
