/*
 * linux/arch/sparc/sys_solaris.c
 *
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

/* CHECKME: this stuff looks rather bogus */
asmlinkage int
do_solaris_syscall (struct pt_regs *regs)
{
	int ret;

	lock_kernel();
	set_personality(PER_SVR4);

	if (current->exec_domain && current->exec_domain->handler){
		current->exec_domain->handler (0, regs);

		/* What is going on here?  Why do we do this? */

		/* XXX current->exec_domain->use_count = 0; XXX */

		ret = regs->u_regs [UREG_I0];
	} else {
		printk ("No solaris handler\n");
		send_sig (SIGSEGV, current, 1);
		ret = 0;
	}
	unlock_kernel();
	return ret;
}

#ifndef CONFIG_SUNOS_EMUL
asmlinkage int
do_sunos_syscall (struct pt_regs *regs)
{
	static int cnt = 0;
	if (++cnt < 10) printk ("SunOS binary emulation not compiled in\n");
	lock_kernel();
	force_sig (SIGSEGV, current);
	unlock_kernel();
	return 0;
}
#endif
