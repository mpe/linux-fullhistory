/* $Id: sys_sparc.c,v 1.34 1997/01/06 06:52:35 davem Exp $
 * linux/arch/sparc/kernel/sys_sparc.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/sparc
 * platform.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

/* XXX Make this per-binary type, this way we can detect the type of
 * XXX a binary.  Every Sparc executable calls this very early on.
 */
asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE; /* Possibly older binaries want 8192 on sun4's? */
}

extern asmlinkage unsigned long sys_brk(unsigned long brk);

asmlinkage unsigned long sparc_brk(unsigned long brk)
{
	unsigned long ret;

	lock_kernel();
	if(sparc_cpu_model == sun4c) {
		if(brk >= 0x20000000 && brk < 0xe0000000) {
			ret = current->mm->brk;
			goto out;
		}
	}
	ret = sys_brk(brk);
out:
	unlock_kernel();
	return ret;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sparc_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error;

	lock_kernel();
	error = do_pipe(fd);
	if (error)
		goto out;
	regs->u_regs[UREG_I1] = fd[1];
	error = fd[0];
out:
	unlock_kernel();
	return error;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */

asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, err;

	lock_kernel();
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			err = sys_semop (first, (struct sembuf *)ptr, second);
			goto out;
		case SEMGET:
			err = sys_semget (first, second, third);
			goto out;
		case SEMCTL: {
			union semun fourth;
			err = -EINVAL;
			if (!ptr)
				goto out;
			err = -EFAULT;
			if(get_user(fourth.__pad, (void **)ptr))
				goto out;
			err = sys_semctl (first, second, third, fourth);
			goto out;
			}
		default:
			err = -EINVAL;
			goto out;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			err = sys_msgsnd (first, (struct msgbuf *) ptr, 
					  second, third);
			goto out;
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				err = -EINVAL;
				if (!ptr)
					goto out;
				err = -EFAULT;
				if(copy_from_user(&tmp,(struct ipc_kludge *) ptr, sizeof (tmp)))
					goto out;
				err = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				goto out;
				}
			case 1: default:
				err = sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, third);
				goto out;
			}
		case MSGGET:
			err = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			err = sys_msgctl (first, second, (struct msqid_ds *) ptr);
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				err = sys_shmat (first, (char *) ptr, second, &raddr);
				if (err)
					goto out;
				err = -EFAULT;
				if(put_user (raddr, (ulong *) third))
					goto out;
				err = 0;
				goto out;
				}
			case 1:	/* iBCS2 emulator entry point */
				err = sys_shmat (first, (char *) ptr, second, (ulong *) third);
				goto out;
			}
		case SHMDT: 
			err = sys_shmdt ((char *)ptr);
			goto out;
		case SHMGET:
			err = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			err = sys_shmctl (first, second, (struct shmid_ds *) ptr);
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	else
		err = -EINVAL;
out:
	unlock_kernel();
	return err;
}

extern unsigned long get_unmapped_area(unsigned long addr, unsigned long len);

/* Linux version of mmap */
asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	struct file * file = NULL;
	unsigned long retval = -EBADF;

	lock_kernel();
	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd])){
			goto out;
	    }
	}
	retval = -ENOMEM;
	if(!(flags & MAP_FIXED) && !addr) {
		addr = get_unmapped_area(addr, len);
		if(!addr){
			goto out;
		}
	}

	/* See asm-sparc/uaccess.h */
	retval = -EINVAL;
	if((len > (TASK_SIZE - PAGE_SIZE)) || (addr > (TASK_SIZE-len-PAGE_SIZE)))
		goto out;

	if(sparc_cpu_model == sun4c) {
		if(((addr >= 0x20000000) && (addr < 0xe0000000))) {
			retval = current->mm->brk;
			goto out;
		}
	}

	retval = do_mmap(file, addr, len, prot, flags, off);
out:
	unlock_kernel();
	return retval;
}

/* we come to here via sys_nis_syscall so it can setup the regs argument */
asmlinkage unsigned long
c_sys_nis_syscall (struct pt_regs *regs)
{
	lock_kernel();
	printk ("Unimplemented SPARC system call %d\n",(int)regs->u_regs[1]);
	show_regs (regs);
	unlock_kernel();
	return -ENOSYS;
}

/* #define DEBUG_SPARC_BREAKPOINT */

asmlinkage void
sparc_breakpoint (struct pt_regs *regs)
{
	lock_kernel();
#ifdef DEBUG_SPARC_BREAKPOINT
        printk ("TRAP: Entering kernel PC=%x, nPC=%x\n", regs->pc, regs->npc);
#endif
	force_sig(SIGTRAP, current);
#ifdef DEBUG_SPARC_BREAKPOINT
	printk ("TRAP: Returning to space: PC=%x nPC=%x\n", regs->pc, regs->npc);
#endif
	unlock_kernel();
}

extern void check_pending(int signum);

asmlinkage int
sparc_sigaction (int signum, const struct sigaction *action, struct sigaction *oldaction)
{
	struct sigaction new_sa, *p;
	int err = -EINVAL;

	lock_kernel();
	if(signum < 0) {
		current->tss.new_signal = 1;
		signum = -signum;
	}

	if (signum<1 || signum>32)
		goto out;
	p = signum - 1 + current->sig->action;
	if (action) {
		err = verify_area(VERIFY_READ,action,sizeof(struct sigaction));
		if (err)
			goto out;
		err = -EINVAL;
		if (signum==SIGKILL || signum==SIGSTOP)
			goto out;
		err = -EFAULT;
		if(copy_from_user(&new_sa, action, sizeof(struct sigaction)))
			goto out;	
		if (new_sa.sa_handler != SIG_DFL && new_sa.sa_handler != SIG_IGN) {
			err = verify_area(VERIFY_READ, new_sa.sa_handler, 1);
			if (err)
				goto out;
		}
	}

	if (oldaction) {
		err = -EFAULT;
		if (copy_to_user(oldaction, p, sizeof(struct sigaction)))
			goto out;	
	}

	if (action) {
		*p = new_sa;
		check_pending(signum);
	}

	err = 0;
out:
	unlock_kernel();
	return err;
}

#ifndef CONFIG_AP1000
/* only AP+ systems have sys_aplib */
asmlinkage int sys_aplib(void)
{
	return -ENOSYS;
}
#endif
