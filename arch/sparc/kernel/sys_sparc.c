/* $Id: sys_sparc.c,v 1.32 1996/12/19 05:25:46 davem Exp $
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
	if(sparc_cpu_model == sun4c) {
		if(brk >= 0x20000000 && brk < 0xe0000000)
			return current->mm->brk;
	}
	return sys_brk(brk);
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sparc_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (error) {
		return error;
	} else {
		regs->u_regs[UREG_I1] = fd[1];
		return fd[0];
	}
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */

asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semop (first, (struct sembuf *)ptr, second);
		case SEMGET:
			return sys_semget (first, second, third);
		case SEMCTL: {
			union semun fourth;
			if (!ptr)
				return -EINVAL;
			if(get_user(fourth.__pad, (void **)ptr))
				return -EFAULT;
			return sys_semctl (first, second, third, fourth);
			}
		default:
			return -EINVAL;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			return sys_msgsnd (first, (struct msgbuf *) ptr, 
					   second, third);
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				if (!ptr)
					return -EINVAL;
				if(copy_from_user(&tmp,(struct ipc_kludge *) ptr, sizeof (tmp)))
					return -EFAULT;
				return sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				}
			case 1: default:
				return sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, third);
			}
		case MSGGET:
			return sys_msgget ((key_t) first, second);
		case MSGCTL:
			return sys_msgctl (first, second, (struct msqid_ds *) ptr);
		default:
			return -EINVAL;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				int err;

				err = sys_shmat (first, (char *) ptr, second, &raddr);
				if (err)
					return err;
				if(put_user (raddr, (ulong *) third))
					return -EFAULT;
				return 0;
				}
			case 1:	/* iBCS2 emulator entry point */
				return sys_shmat (first, (char *) ptr, second, (ulong *) third);
			}
		case SHMDT: 
			return sys_shmdt ((char *)ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second, (struct shmid_ds *) ptr);
		default:
			return -EINVAL;
		}
	return -EINVAL;
}

extern unsigned long get_unmapped_area(unsigned long addr, unsigned long len);

/* Linux version of mmap */
asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	struct file * file = NULL;
	long retval;

	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd])){
			return -EBADF;
	    }
	}
	if(!(flags & MAP_FIXED) && !addr) {
		addr = get_unmapped_area(addr, len);
		if(!addr){
			return -ENOMEM;
		}
	}

	/* See asm-sparc/uaccess.h */
	if((len > (TASK_SIZE - PAGE_SIZE)) || (addr > (TASK_SIZE-len-PAGE_SIZE)))
		return -EINVAL;

	if(sparc_cpu_model == sun4c) {
		if(((addr >= 0x20000000) && (addr < 0xe0000000)))
			return current->mm->brk;
	}

	retval = do_mmap(file, addr, len, prot, flags, off);
	return retval;
}

/* we come to here via sys_nis_syscall so it can setup the regs argument */
asmlinkage unsigned long
c_sys_nis_syscall (struct pt_regs *regs)
{
	printk ("Unimplemented SPARC system call %d\n",(int)regs->u_regs[1]);
	show_regs (regs);
	return -ENOSYS;
}

/* #define DEBUG_SPARC_BREAKPOINT */

asmlinkage void
sparc_breakpoint (struct pt_regs *regs)
{
#ifdef DEBUG_SPARC_BREAKPOINT
        printk ("TRAP: Entering kernel PC=%x, nPC=%x\n", regs->pc, regs->npc);
#endif
	force_sig(SIGTRAP, current);
#ifdef DEBUG_SPARC_BREAKPOINT
	printk ("TRAP: Returning to space: PC=%x nPC=%x\n", regs->pc, regs->npc);
#endif
}

extern int
sys_sigaction (int signum, const struct sigaction *action, struct sigaction *oldaction);

asmlinkage int
sparc_sigaction (int signum, const struct sigaction *action, struct sigaction *oldaction)
{
    if (signum >= 0){
	return sys_sigaction (signum, action, oldaction);
    } else {
	current->tss.new_signal = 1;
	return sys_sigaction (-signum, action, oldaction);
    }
}

#ifndef CONFIG_AP1000
/* only AP+ systems have sys_aplib */
asmlinkage int sys_aplib(void)
{
	return -ENOSYS;
}
#endif
