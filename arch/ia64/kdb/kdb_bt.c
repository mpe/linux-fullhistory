/**
 * Minimalist Kernel Debugger 
 * Machine dependent stack traceback code for IA-64.
 *
 * Copyright (C) 1999 Goutham Rao <goutham.rao@intel.com>
 * Copyright (C) 1999 Sreenivas Subramoney <sreenivas.subramoney@intel.com>
 * Intel Corporation, August 1999.
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 99/12/03 D. Mosberger	Reimplemented based on <asm-ia64/unwind.h> API.
 * 99/12/06 D. Mosberger	Added support for backtracing other processes.
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kdb.h>
#include <asm/system.h>
#include <asm/current.h>
#include <asm/kdbsupport.h>

/*
 * Minimal stack back trace functionality.
 */
int
kdb_bt (int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	struct task_struct *task = current;
	struct ia64_frame_info info;
	char *name;
	int diag;

	if (strcmp(argv[0], "btp") == 0) {
		unsigned long pid;

		diag = kdbgetularg(argv[1], &pid);
		if (diag)
			return diag;

		task = find_task_by_pid(pid);
		if (!task) {
			kdb_printf("No process with pid == %d found\n", pid);
			return 0;
		}
		regs = ia64_task_regs(task);
	} else if (argc) {
		kdb_printf("bt <address> is unsupported for IA-64\n");
		return 0;
	}

	if (task == current) {
		/*
		 * Upon entering kdb, the stack frame looks like this:
		 *
		 *	+---------------------+
		 *	|   struct pt_regs    |
		 *	+---------------------+
		 *	|		      |
		 *	|   kernel stack      |
		 *	|		      |
		 *	+=====================+ <--- top of stack upon entering kdb
		 *	|   struct pt_regs    |
		 *	+---------------------+
		 *	| struct switch_stack |
		 *	+---------------------+
		 */
		if (user_mode(regs)) {
			/* We are not implementing stack backtrace from user mode code */
			kdb_printf ("Not in Kernel\n");
			return 0;
		}
		ia64_unwind_init_from_current(&info, regs);
	} else {
		/*
		 * For a blocked task, the stack frame looks like this:
		 *
		 *	+---------------------+
		 *	|   struct pt_regs    |
		 *	+---------------------+
		 *	|		      |
		 *	|   kernel stack      |
		 *	|		      |
		 *	+---------------------+
		 *	| struct switch_stack |
		 *	+=====================+ <--- task->thread.ksp
		 */
		ia64_unwind_init_from_blocked_task(&info, task);
	}

	kdb_printf("Ret Address           Reg Stack base       Name\n\n") ;
	do {
		unsigned long ip = ia64_unwind_get_ip(&info);

		name = kdbnearsym(ip);
		if (!name) {
			kdb_printf("Interrupt\n");
			return 0;
		}
		kdb_printf("0x%016lx: [0x%016lx] %s\n", ip, ia64_unwind_get_bsp(&info), name);
	} while (ia64_unwind_to_previous_frame(&info) >= 0);
	return 0;
}
