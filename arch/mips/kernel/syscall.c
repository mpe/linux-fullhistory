/*
 * MIPS specific syscall handling functions and syscalls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle
 *
 * TODO:  Implement the compatibility syscalls.
 *        Don't waste that much memory for empty entries in the syscall
 *        table.
 */
#undef CONF_PRINT_SYSCALLS
#undef CONF_DEBUG_IRIX

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <asm/branch.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/uaccess.h>

extern asmlinkage void syscall_trace(void);
typedef asmlinkage int (*syscall_t)(void *a0,...);
extern asmlinkage int (*do_syscalls)(struct pt_regs *regs, syscall_t fun,
				     int narg);
extern syscall_t sys_call_table[];
extern unsigned char sys_narg_table[];

asmlinkage int sys_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error, res;

	lock_kernel();
	error = do_pipe(fd);
	if (error) {
		res = error;
		goto out;
	}
	regs->regs[3] = fd[1];
	res = fd[0];
out:
	unlock_kernel();
	return res;
}

asmlinkage unsigned long sys_mmap(unsigned long addr, size_t len, int prot,
                                  int flags, int fd, off_t offset)
{
	struct file * file = NULL;
	unsigned long res;

	lock_kernel();
	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	res = do_mmap(file, addr, len, prot, flags, offset);

	unlock_kernel();
	return res;
}

asmlinkage int sys_idle(void)
{
        int ret = -EPERM;

	lock_kernel();
	if (current->pid != 0)
		goto out;
	/* endless idle loop with no priority at all */
	current->priority = -100;
	current->counter = -100;
	for (;;) {
		/*
		 * R4[236]00 have wait, R4[04]00 don't.
		 * FIXME: We should save power by reducing the clock where
		 *        possible.  Should help alot for battery powered
		 *        R4200/4300i systems.
		 */
		if (wait_available && !need_resched)
			__asm__(".set\tmips3\n\t"
				"wait\n\t"
				".set\tmips0\n\t");
		run_task_queue(&tq_scheduler);
		schedule();
	}
out:
	unlock_kernel();
	return ret;
}

asmlinkage int sys_fork(struct pt_regs *regs)
{
	int res;

	lock_kernel();
	res = do_fork(SIGCHLD, regs->regs[29], regs);
	unlock_kernel();
	return res;
}

asmlinkage int sys_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int res;

	lock_kernel();
	clone_flags = regs->regs[4];
	newsp = regs->regs[5];
	if (!newsp)
		newsp = regs->regs[29];
	res = do_fork(clone_flags, newsp, regs);
	unlock_kernel();
	return res;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs *regs)
{
	int res;
	char * filename;

	lock_kernel();
	res = getname((char *) (long)regs->regs[4], &filename);
	if (res)
		goto out;
	res = do_execve(filename, (char **) (long)regs->regs[5],
	                (char **) (long)regs->regs[6], regs);
	putname(filename);

out:
	unlock_kernel();
	return res;
}

/*
 * Do the indirect syscall syscall.
 * Don't care about kernel locking; the actual syscall will do it.
 */
asmlinkage int sys_syscall(struct pt_regs *regs)
{
	syscall_t syscall;
	unsigned long syscallnr = regs->regs[4];
	unsigned long a0, a1, a2, a3, a4, a5, a6;
	int nargs, errno;

	if (syscallnr > __NR_Linux + __NR_Linux_syscalls)
		return -ENOSYS;

	syscall = sys_call_table[syscallnr];
	nargs = sys_narg_table[syscallnr];
	/*
	 * Prevent stack overflow by recursive
	 * syscall(__NR_syscall, __NR_syscall,...);
	 */
	if (syscall == (syscall_t) sys_syscall) {
		return -EINVAL;
	}

	if (syscall == NULL) {
		return -ENOSYS;
	}

	if(nargs > 3) {
		unsigned long usp = regs->regs[29];
		unsigned long *sp = (unsigned long *) usp;
		if(usp & 3) {
			printk("unaligned usp -EFAULT\n");
			force_sig(SIGSEGV, current);
			return -EFAULT;
		}
		errno = verify_area(VERIFY_READ, (void *) (usp + 16),
		                    (nargs - 3) * sizeof(unsigned long));
		if(errno) {
			return -EFAULT;
		}
		switch(nargs) {
		case 7:
			a3 = sp[4]; a4 = sp[5]; a5 = sp[6]; a6 = sp[7];
			break;
		case 6:
			a3 = sp[4]; a4 = sp[5]; a5 = sp[6]; a6 = 0;
			break;
		case 5:
			a3 = sp[4]; a4 = sp[5]; a5 = a6 = 0;
			break;
		case 4:
			a3 = sp[4]; a4 = a5 = a6 = 0;
			break;

		default:
			a3 = a4 = a5 = a6 = 0;
			break;
		}
	} else {
		a3 = a4 = a5 = a6 = 0;
	}
	a0 = regs->regs[5]; a1 = regs->regs[6]; a2 = regs->regs[7];
	if(nargs == 0)
		a0 = (unsigned long) regs;
	return syscall((void *)a0, a1, a2, a3, a4, a5, a6);
}

/*
 * If we ever come here the user sp is bad.  Zap the process right away.
 * Due to the bad stack signaling wouldn't work.
 * XXX kernel locking???
 */
asmlinkage void bad_stack(void)
{
	do_exit(SIGSEGV);
}

#ifdef CONF_PRINT_SYSCALLS
#define SYS(fun, narg) #fun,
static char *sfnames[] = {
#include "syscalls.h"
};
#endif

#if defined(CONFIG_BINFMT_IRIX) && defined(CONF_DEBUG_IRIX)
#define SYS(fun, narg) #fun,
static char *irix_sys_names[] = {
#include "irix5sys.h"
};
#endif

/*
 * This isn't entirely correct with respect to kernel locking ...
 */
void do_sys(struct pt_regs *regs)
{
	unsigned long syscallnr, usp;
	syscall_t syscall;
	int errno, narg;

        /* Skip syscall instruction */
	if (delay_slot(regs)) {
		/*
		 * By convention "li v0,<syscallno>" is always preceeding
		 * the syscall instruction.  So if we're in a delay slot
		 * userland is screwed up.
		 */
		force_sig(SIGILL, current);
		return;
	}
	regs->cp0_epc += 4;

	syscallnr = regs->regs[2];
	if (syscallnr > (__NR_Linux + __NR_Linux_syscalls))
		goto illegal_syscall;

	syscall = sys_call_table[syscallnr];
	if (syscall == NULL)
		goto illegal_syscall;

	narg = sys_narg_table[syscallnr];
#ifdef CONF_PRINT_SYSCALLS
	if(syscallnr >= 4000)
		printk("do_sys(%s:%d): %s(%08lx,%08lx,%08lx,%08lx)<pc=%08lx>",
		       current->comm, current->pid, sfnames[syscallnr - __NR_Linux],
		       regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7],
		       regs->cp0_epc);
#endif
#if defined(CONFIG_BINFMT_IRIX) && defined(CONF_DEBUG_IRIX)
	if(syscallnr < 2000 && syscallnr >= 1000) {
		printk("irix_sys(%s:%d): %s(", current->comm,
		       current->pid, irix_sys_names[syscallnr - 1000]);
		if((narg < 4) && (narg != 0)) {
			int i = 0;

			while(i < (narg - 1)) {
				printk("%08lx, ", regs->regs[i + 4]);
				i++;
			}
			printk("%08lx) ", regs->regs[i + 4]);
		} else if(narg == 0) {
			printk("%08lx, %08lx, %08lx, %08lx) ",
			       regs->regs[4], regs->regs[5], regs->regs[6],
			       regs->regs[7]);
		} else
			printk("narg=%d) ", narg);
	}
#endif
	if (narg > 4) {
		/*
		 * Verify that we can safely get the additional parameters
		 * from the user stack.  Of course I could read the params
		 * from unaligned addresses ...  Consider this a programming
		 * course caliber .45.
		 */
		usp = regs->regs[29];
		if (usp & 3) {
			printk("unaligned usp\n");
			force_sig(SIGSEGV, current);
			regs->regs[2] = EFAULT;
			regs->regs[7] = 1;
			return;
		}
		if (!access_ok(VERIFY_READ, (void *) (usp + 16),
		      (narg - 4) * sizeof(unsigned long))) {
			regs->regs[2] = EFAULT;
			regs->regs[7] = 1;
			return;
		}
	}

	if ((current->flags & PF_TRACESYS) == 0)
	{
		errno = do_syscalls(regs, syscall, narg);
		if ((errno < 0 && errno > (-ENOIOCTLCMD - 1)) || current->errno) {
			goto bad_syscall;
		}
		regs->regs[2] = errno;
		regs->regs[7] = 0;
	}
	else
	{
		syscall_trace();

		errno = do_syscalls(regs, syscall, narg);
		if (errno < 0 || current->errno)
		{
			regs->regs[2] = -errno;
			regs->regs[7] = 1;
		}
		else
		{
			regs->regs[2] = errno;
			regs->regs[7] = 0;
		}

		syscall_trace();
	}
#if defined(CONF_PRINT_SYSCALLS) || \
    (defined(CONFIG_BINFMT_IRIX) && defined(CONF_DEBUG_IRIX))
#if 0
	printk(" returning: normal\n");
#else
	if(syscallnr >= 4000 && syscallnr < 5000)
		printk(" returning: %08lx\n", (unsigned long) errno);
#endif
#endif
	return;

bad_syscall:
	regs->regs[0] = regs->regs[2] = -errno;
	regs->regs[7] = 1;
#if defined(CONF_PRINT_SYSCALLS) || \
    (defined(CONFIG_BINFMT_IRIX) && defined(CONF_DEBUG_IRIX))
#if 0
	printk(" returning: bad_syscall\n");
#else
	if(syscallnr >= 4000 && syscallnr < 5000)
		printk(" returning error: %d\n", errno);
#endif
#endif
	return;
illegal_syscall:

	regs->regs[2] = ENOSYS;
	regs->regs[7] = 1;
#if defined(CONF_PRINT_SYSCALLS) || \
    (defined(CONFIG_BINFMT_IRIX) && defined(CONF_DEBUG_IRIX))
	if(syscallnr >= 1000 && syscallnr < 2000)
		printk(" returning: illegal_syscall\n");
#endif
	return;
}
