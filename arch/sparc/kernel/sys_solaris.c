/*
 * linux/arch/sparc/sys_solaris.c
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/mm.h>

asmlinkage int
do_solaris_syscall (struct pt_regs *regs)
{
	current->personality = PER_SVR4;
	current->exec_domain = lookup_exec_domain(PER_SVR4);

	if (current->exec_domain && current->exec_domain->handler){
		current->exec_domain->handler (regs);
		current->exec_domain->use_count = 0;
		return regs->u_regs [UREG_I0];
	}
	printk ("No solaris handler\n");
	send_sig (SIGSEGV, current, 1);
	return 0;
}
