/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#include <errno.h>
#include <string.h>
#include <sys/ptrace.h>
#include "sysdep/ptrace.h"
#include "uml-config.h"
#include "skas_ptregs.h"
#include "registers.h"
#include "user.h"

/* These are set once at boot time and not changed thereafter */

static unsigned long exec_regs[HOST_FRAME_SIZE];
static unsigned long exec_fp_regs[HOST_FP_SIZE];

void init_thread_registers(union uml_pt_regs *to)
{
	memcpy(to->skas.regs, exec_regs, sizeof(to->skas.regs));
	memcpy(to->skas.fp, exec_fp_regs, sizeof(to->skas.fp));
}

static int move_registers(int pid, int int_op, int fp_op,
			  union uml_pt_regs *regs)
{
	if(ptrace(int_op, pid, 0, regs->skas.regs) < 0)
		return(-errno);

	if(ptrace(fp_op, pid, 0, regs->skas.fp) < 0)
		return(-errno);

	return(0);
}

void save_registers(int pid, union uml_pt_regs *regs)
{
	int err;

	err = move_registers(pid, PTRACE_GETREGS, PTRACE_GETFPREGS, regs);
	if(err)
		panic("save_registers - saving registers failed, errno = %d\n",
		      -err);
}

void restore_registers(int pid, union uml_pt_regs *regs)
{
	int err;

	err = move_registers(pid, PTRACE_SETREGS, PTRACE_SETFPREGS, regs);
	if(err)
		panic("restore_registers - saving registers failed, "
		      "errno = %d\n", -err);
}

void init_registers(int pid)
{
	int err;

	err = ptrace(PTRACE_GETREGS, pid, 0, exec_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETREGS failed, errno = %d",
		      err);

	err = ptrace(PTRACE_GETFPREGS, pid, 0, exec_fp_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETFPREGS failed, errno = %d",
		      err);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
