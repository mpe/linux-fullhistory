/* $Id: sys_sparc.c,v 1.25 1998/10/21 03:21:15 davem Exp $
 * linux/arch/sparc64/kernel/sys_sparc.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/sparc
 * platform.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/utsname.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/utrap.h>
#include <asm/perfctr.h>

/* XXX Make this per-binary type, this way we can detect the type of
 * XXX a binary.  Every Sparc executable calls this very early on.
 */
asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE;
}

extern asmlinkage unsigned long sys_brk(unsigned long brk);

asmlinkage unsigned long sparc_brk(unsigned long brk)
{
	if(brk >= 0x80000000000UL)	/* VM hole */
		return current->mm->brk;
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

asmlinkage int sys_ipc (unsigned call, int first, int second, unsigned long third, void *ptr, long fifth)
{
	int err;

	lock_kernel();
	/* No need for backward compatibility. We can start fresh... */

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			err = sys_semop (first, (struct sembuf *)ptr, second);
			goto out;
		case SEMGET:
			err = sys_semget (first, second, (int)third);
			goto out;
		case SEMCTL: {
			union semun fourth;
			err = -EINVAL;
			if (!ptr)
				goto out;
			err = -EFAULT;
			if(get_user(fourth.__pad, (void **)ptr))
				goto out;
			err = sys_semctl (first, second, (int)third, fourth);
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
					  second, (int)third);
			goto out;
		case MSGRCV:
			err = sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, (int)third);
			goto out;
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
			err = sys_shmat (first, (char *) ptr, second, (ulong *) third);
			goto out;
		case SHMDT:
			err = sys_shmdt ((char *)ptr);
			goto out;
		case SHMGET:
			err = sys_shmget (first, second, (int)third);
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

/* Linux version of mmap */
asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	struct file * file = NULL;
	unsigned long retval = -EBADF;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}
	retval = -ENOMEM;
	len = PAGE_ALIGN(len);
	if(!(flags & MAP_FIXED) && !addr) {
		addr = get_unmapped_area(addr, len);
		if(!addr)
			goto out_putf;
	}

	retval = -EINVAL;
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		if (len > 0xf0000000UL || addr > 0xf0000000UL - len)
			goto out_putf;
	} else {
		if (len >= 0x80000000000UL || 
		    (addr < 0x80000000000UL &&
		     addr > 0x80000000000UL-len))
			goto out_putf;
		if (addr >= 0x80000000000ULL && addr < 0xfffff80000000000UL) {
			/* VM hole */
			retval = current->mm->brk;
			goto out_putf;
		}
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	retval = do_mmap(file, addr, len, prot, flags, off);

out_putf:
	if (file)
		fput(file);
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return retval;
}

/* we come to here via sys_nis_syscall so it can setup the regs argument */
asmlinkage unsigned long
c_sys_nis_syscall (struct pt_regs *regs)
{
	static int count=0;
	lock_kernel();
	if (++count <= 20) { /* Don't make the system unusable, if someone goes stuck */
		printk ("Unimplemented SPARC system call %ld\n",regs->u_regs[1]);
		show_regs (regs);
	}
	unlock_kernel();
	return -ENOSYS;
}

/* #define DEBUG_SPARC_BREAKPOINT */

asmlinkage void
sparc_breakpoint (struct pt_regs *regs)
{
	lock_kernel();
#ifdef DEBUG_SPARC_BREAKPOINT
        printk ("TRAP: Entering kernel PC=%lx, nPC=%lx\n", regs->tpc, regs->tnpc);
#endif
	force_sig(SIGTRAP, current);
#ifdef DEBUG_SPARC_BREAKPOINT
	printk ("TRAP: Returning to space: PC=%lx nPC=%lx\n", regs->tpc, regs->tnpc);
#endif
	unlock_kernel();
}

extern void check_pending(int signum);

asmlinkage int sys_getdomainname(char *name, int len)
{
        int nlen;
	int err = -EFAULT;

 	down(&uts_sem);
 	
	nlen = strlen(system_utsname.domainname) + 1;

        if (nlen < len)
                len = nlen;
	if(len > __NEW_UTS_LEN)
		goto done;
	if(copy_to_user(name, system_utsname.domainname, len))
		goto done;
	err = 0;
done:
	up(&uts_sem);
	return err;
}

/* only AP+ systems have sys_aplib */
asmlinkage int sys_aplib(void)
{
	return -ENOSYS;
}

asmlinkage int solaris_syscall(struct pt_regs *regs)
{
	static int count = 0;
	lock_kernel();
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
	if(++count <= 20)
		printk ("For Solaris binary emulation you need solaris module loaded\n");
	show_regs (regs);
	send_sig(SIGSEGV, current, 1);
	unlock_kernel();
	return -ENOSYS;
}

asmlinkage int sys_utrap_install(utrap_entry_t type, utrap_handler_t new_p,
				 utrap_handler_t new_d,
				 utrap_handler_t *old_p, utrap_handler_t *old_d)
{
	if (type < UT_INSTRUCTION_EXCEPTION || type > UT_TRAP_INSTRUCTION_31)
		return -EINVAL;
	if (new_p == (utrap_handler_t)(long)UTH_NOCHANGE) {
		if (old_p) {
			if (!current->tss.utraps)
				put_user_ret(NULL, old_p, -EFAULT);
			else
				put_user_ret((utrap_handler_t)(current->tss.utraps[type]), old_p, -EFAULT);
		}
		if (old_d)
			put_user_ret(NULL, old_d, -EFAULT);
		return 0;
	}
	lock_kernel();
	if (!current->tss.utraps) {
		current->tss.utraps = kmalloc((UT_TRAP_INSTRUCTION_31+1)*sizeof(long), GFP_KERNEL);
		if (!current->tss.utraps) return -ENOMEM;
		current->tss.utraps[0] = 1;
		memset(current->tss.utraps+1, 0, UT_TRAP_INSTRUCTION_31*sizeof(long));
	} else {
		if ((utrap_handler_t)current->tss.utraps[type] != new_p && current->tss.utraps[0] > 1) {
			long *p = current->tss.utraps;
			
			current->tss.utraps = kmalloc((UT_TRAP_INSTRUCTION_31+1)*sizeof(long), GFP_KERNEL);
			if (!current->tss.utraps) {
				current->tss.utraps = p;
				return -ENOMEM;
			}
			p[0]--;
			current->tss.utraps[0] = 1;
			memcpy(current->tss.utraps+1, p+1, UT_TRAP_INSTRUCTION_31*sizeof(long));
		}
	}
	if (old_p)
		put_user_ret((utrap_handler_t)(current->tss.utraps[type]), old_p, -EFAULT);
	if (old_d)
		put_user_ret(NULL, old_d, -EFAULT);
	current->tss.utraps[type] = (long)new_p;
	unlock_kernel();
	return 0;
}

long sparc_memory_ordering(unsigned long model, struct pt_regs *regs)
{
	if (model >= 3)
		return -EINVAL;
	regs->tstate = (regs->tstate & ~TSTATE_MM) | (model << 14);
	return 0;
}

asmlinkage int
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
		new_ka.ka_restorer = NULL;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage int
sys_rt_sigaction(int sig, const struct sigaction *act, struct sigaction *oact,
		 void *restorer, size_t sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (act) {
		new_ka.ka_restorer = restorer;
		if (copy_from_user(&new_ka.sa, act, sizeof(*act)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_ka.sa, sizeof(*oact)))
			return -EFAULT;
	}

	return ret;
}

/* Invoked by rtrap code to update performance counters in
 * user space.
 */
asmlinkage void
update_perfctrs(void)
{
	unsigned long pic, tmp;

	read_pic(pic);
	tmp = (current->tss.kernel_cntd0 += (unsigned int)pic);
	__put_user(tmp, current->tss.user_cntd0);
	tmp = (current->tss.kernel_cntd1 += (pic >> 32));
	__put_user(tmp, current->tss.user_cntd1);
	reset_pic();
}

asmlinkage int
sys_perfctr(int opcode, unsigned long arg0, unsigned long arg1, unsigned long arg2)
{
	int err = 0;

	switch(opcode) {
	case PERFCTR_ON:
		current->tss.pcr_reg = arg2;
		current->tss.user_cntd0 = (u64 *) arg0;
		current->tss.user_cntd1 = (u64 *) arg1;
		current->tss.kernel_cntd0 =
			current->tss.kernel_cntd1 = 0;
		write_pcr(arg2);
		reset_pic();
		current->tss.flags |= SPARC_FLAG_PERFCTR;
		break;

	case PERFCTR_OFF:
		err = -EINVAL;
		if ((current->tss.flags & SPARC_FLAG_PERFCTR) != 0) {
			current->tss.user_cntd0 =
				current->tss.user_cntd1 = NULL;
			current->tss.pcr_reg = 0;
			write_pcr(0);
			current->tss.flags &= ~(SPARC_FLAG_PERFCTR);
			err = 0;
		}
		break;

	case PERFCTR_READ: {
		unsigned long pic, tmp;

		if (!(current->tss.flags & SPARC_FLAG_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		read_pic(pic);
		tmp = (current->tss.kernel_cntd0 += (unsigned int)pic);
		err |= __put_user(tmp, current->tss.user_cntd0);
		tmp = (current->tss.kernel_cntd1 += (pic >> 32));
		err |= __put_user(tmp, current->tss.user_cntd1);
		reset_pic();
		break;
	}

	case PERFCTR_CLRPIC:
		if (!(current->tss.flags & SPARC_FLAG_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		current->tss.kernel_cntd0 =
			current->tss.kernel_cntd1 = 0;
		reset_pic();
		break;

	case PERFCTR_SETPCR: {
		u64 *user_pcr = (u64 *)arg0;
		if (!(current->tss.flags & SPARC_FLAG_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		err |= __get_user(current->tss.pcr_reg, user_pcr);
		write_pcr(current->tss.pcr_reg);
		current->tss.kernel_cntd0 =
			current->tss.kernel_cntd1 = 0;
		reset_pic();
		break;
	}

	case PERFCTR_GETPCR: {
		u64 *user_pcr = (u64 *)arg0;
		if (!(current->tss.flags & SPARC_FLAG_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		err |= __put_user(current->tss.pcr_reg, user_pcr);
		break;
	}

	default:
		err = -EINVAL;
		break;
	};
	return err;
}
