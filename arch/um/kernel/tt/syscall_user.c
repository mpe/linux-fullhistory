/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <asm/unistd.h>
#include "sysdep/ptrace.h"
#include "sigcontext.h"
#include "ptrace_user.h"
#include "task.h"
#include "user_util.h"
#include "kern_util.h"
#include "syscall_user.h"
#include "tt.h"


void syscall_handler_tt(int sig, union uml_pt_regs *regs)
{
	void *sc;
	long result;
	int syscall;
#ifdef UML_CONFIG_DEBUG_SYSCALL
	int index;
#endif

	syscall = UPT_SYSCALL_NR(regs);
	sc = UPT_SC(regs);
	SC_START_SYSCALL(sc);

#ifdef UML_CONFIG_DEBUG_SYSCALL
  	index = record_syscall_start(syscall);
#endif
	syscall_trace(regs, 0);
	result = execute_syscall_tt(regs);

	/* regs->sc may have changed while the system call ran (there may
	 * have been an interrupt or segfault), so it needs to be refreshed.
	 */
	UPT_SC(regs) = sc;

	SC_SET_SYSCALL_RETURN(sc, result);

	syscall_trace(regs, 1);
#ifdef UML_CONFIG_DEBUG_SYSCALL
  	record_syscall_end(index, result);
#endif
}

void do_sigtrap(void *task)
{
	UPT_SYSCALL_NR(TASK_REGS(task)) = -1;
}

void do_syscall(void *task, int pid, int local_using_sysemu)
{
	unsigned long proc_regs[FRAME_SIZE];

	if(ptrace_getregs(pid, proc_regs) < 0)
		tracer_panic("Couldn't read registers");

	UPT_SYSCALL_NR(TASK_REGS(task)) = PT_SYSCALL_NR(proc_regs);

	if(((unsigned long *) PT_IP(proc_regs) >= &_stext) &&
	   ((unsigned long *) PT_IP(proc_regs) <= &_etext))
		tracer_panic("I'm tracing myself and I can't get out");

	/* advanced sysemu mode set syscall number to -1 automatically */
	if (local_using_sysemu==2)
		return;

	/* syscall number -1 in sysemu skips syscall restarting in host */
	if(ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_NR_OFFSET,
		  local_using_sysemu ? -1 : __NR_getpid) < 0)
		tracer_panic("do_syscall : Nullifying syscall failed, "
			     "errno = %d", errno);
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
