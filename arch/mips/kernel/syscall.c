/*
 * MIPS specific syscall handling functions and syscalls
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <asm/ptrace.h>
#include <asm/segment.h>
#include <asm/signal.h>

extern asmlinkage void syscall_trace(void);
typedef asmlinkage int (*syscall_t)(void *a0,...);
extern asmlinkage int do_syscalls(struct pt_regs *regs, syscall_t fun,
                                  int narg);
extern syscall_t sys_call_table[];
extern unsigned char sys_narg_table[];

asmlinkage int sys_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error;

	lock_kernel();
	error = do_pipe(fd);
	if (error)
		goto out;
	regs->reg2 = fd[0];
	regs->reg3 = fd[1];
out:
	unlock_kernel();
	return error;
}

asmlinkage unsigned long sys_mmap(unsigned long addr, size_t len, int prot,
                                  int flags, int fd, off_t offset)
{
	struct file * file = NULL;
	int ret = -EBADF;

	lock_kernel();
	if (flags & MAP_RENAME) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			goto out;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	ret = do_mmap(file, addr, len, prot, flags, offset);
out:
	unlock_kernel();
	return ret;
}

asmlinkage int sys_idle(void)
{
	int ret = -EPERM;

	lock_kernel();
	if (current->pid != 0)
		goto out;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		/*
		 * R4[26]00 have wait, R4[04]00 don't.
		 */
		if (wait_available && !need_resched)
			__asm__(".set\tmips3\n\t"
				"wait\n\t"
				".set\tmips0\n\t");
		schedule();
	}
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

asmlinkage int sys_fork(struct pt_regs *regs)
{
	int ret;

	lock_kernel();
	ret = do_fork(SIGCHLD, regs->reg29, regs);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int ret;

	lock_kernel();
	clone_flags = regs->reg4;
	newsp = regs->reg5;
	if (!newsp)
		newsp = regs->reg29;
	ret = do_fork(clone_flags, newsp, regs);
	unlock_kernel();
	return ret;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs *regs)
{
	int error;
	char * filename;

	lock_kernel();
	error = getname((char *) regs->reg4, &filename);
	if (error)
		goto out;
	error = do_execve(filename, (char **) regs->reg5,
	                  (char **) regs->reg6, regs);
	putname(filename);
out:
	unlock_kernel();
	return error;
}

/*
 * Do the indirect syscall syscall.
 */
asmlinkage int sys_syscall(unsigned long a0, unsigned long a1, unsigned long a2,
                           unsigned long a3, unsigned long a4, unsigned long a5,
                           unsigned long a6)
{
	syscall_t syscall;

	if (a0 > __NR_Linux + __NR_Linux_syscalls)
		return -ENOSYS;

	syscall = sys_call_table[a0];
	/*
	 * Prevent stack overflow by recursive
	 * syscall(__NR_syscall, __NR_syscall,...);
	 */
	if (syscall == (syscall_t) sys_syscall)
		return -EINVAL;

	if (syscall == NULL)
		return -ENOSYS;

	return syscall((void *)a0, a1, a2, a3, a4, a5, a6);
}

void do_sys(struct pt_regs *regs)
{
	unsigned long syscallnr, usp;
	syscall_t syscall;
	int errno, narg;

	/*
	 * Compute the return address;
	 */
	if (regs->cp0_cause & CAUSEF_BD)
	{
		/*
		 * This syscall is in a branch delay slot.  Since we don't do
		 * branch delay slot handling we would get a process trying
		 * to do syscalls ever and ever again.  So better zap it.
		 */
		printk("%s: syscall in branch delay slot.\n", current->comm);
		current->sig->action[SIGILL-1].sa_handler = NULL;
		current->blocked &= ~(1<<(SIGILL-1));
		send_sig(SIGILL, current, 1);
		return;
	}
	regs->cp0_epc += 4;

	syscallnr = regs->reg2;
	if (syscallnr > (__NR_Linux + __NR_Linux_syscalls))
		goto illegal_syscall;

	syscall = sys_call_table[syscallnr];
	if (syscall == NULL)
		goto illegal_syscall;

	narg = sys_narg_table[syscallnr];
	if (narg > 4)
	{
		/*
		 * Verify that we can safely get the additional parameters
		 * from the user stack.  Of course I could read the params
		 * from unaligned addresses ...  Consider this a programming
		 * course caliber .45.
		 */
		usp = regs->reg29;
		if (usp & 3)
		{
			printk("unaligned usp\n");
			send_sig(SIGSEGV, current, 1);
			regs->reg2 = EFAULT;
			regs->reg7 = 1;
			return;
		}
		errno = verify_area(VERIFY_READ, (void *) (usp + 16),
		                    (narg - 4) * sizeof(unsigned long));
		if (errno < 0)
			goto bad_syscall;
	}

	if ((current->flags & PF_TRACESYS) == 0)
	{
		errno = do_syscalls(regs, syscall, narg);
		if (errno < 0 || current->errno)
			goto bad_syscall;

		regs->reg2 = errno;
		regs->reg7 = 0;
	}
	else
	{
		syscall_trace();

		errno = do_syscalls(regs, syscall, narg);
		if (errno < 0 || current->errno)
		{
			regs->reg2 = -errno;
			regs->reg7 = 1;
		}
		else
		{
			regs->reg2 = errno;
			regs->reg7 = 0;
		}

		syscall_trace();
	}
	return;

bad_syscall:
	regs->reg2 = -errno;
	regs->reg7 = 1;
	return;
illegal_syscall:
	regs->reg2 = ENOSYS;
	regs->reg7 = 1;
	return;
}
