/* $Id: syscall.c,v 1.3 2000/02/04 07:40:24 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <asm/ipc.h>
#include <asm/cachectl.h>
#include <asm/offset.h>
#include <asm/pgalloc.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/stackframe.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>

asmlinkage int sys_pipe(abi64_no_regargs, struct pt_regs regs)
{
	int fd[2];
	int error, res;

	lock_kernel();
	error = do_pipe(fd);
	if (error) {
		res = error;
		goto out;
	}
	regs.regs[3] = fd[1];
	res = fd[0];
out:
	unlock_kernel();
	return res;
}

asmlinkage unsigned long
sys_mmap(unsigned long addr, size_t len, unsigned long prot,
         unsigned long flags, unsigned long fd, off_t offset)
{
	struct file * file = NULL;
	unsigned long error = -EFAULT;

	lock_kernel();
	if (!(flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(fd);
		if (!file)
			goto out;
	}
        flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down(&current->mm->mmap_sem);
        error = do_mmap(file, addr, len, prot, flags, offset);
	up(&current->mm->mmap_sem);
        if (file)
                fput(file);
out:
	unlock_kernel();

	return error;
}

asmlinkage int sys_fork(abi64_no_regargs, struct pt_regs regs)
{
	int res;

	save_static(&regs);
	res = do_fork(SIGCHLD, regs.regs[29], &regs);
	return res;
}

asmlinkage int sys_clone(abi64_no_regargs, struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int res;

	save_static(&regs);
	clone_flags = regs.regs[4];
	newsp = regs.regs[5];
	if (!newsp)
		newsp = regs.regs[29];
	res = do_fork(clone_flags, newsp, &regs);
	return res;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(abi64_no_regargs, struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname((char *) (long)regs.regs[4]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) (long)regs.regs[5],
	                  (char **) (long)regs.regs[6], &regs);
	putname(filename);

out:
	return error;
}

/*
 * Compacrapability ...
 */
asmlinkage int sys_uname(struct old_utsname * name)
{
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		return 0;
	return -EFAULT;
}

/*
 * Compacrapability ...
 */
asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	error = error ? -EFAULT : 0;

	return error;
}

/*
 * Do the indirect syscall syscall.
 *
 * XXX This is borken.
 */
asmlinkage int sys_syscall(abi64_no_regargs, struct pt_regs regs)
{
	return -ENOSYS;
}

asmlinkage int
sys_sysmips(int cmd, long arg1, int arg2, int arg3)
{
	int	*p;
	char	*name;
	int	flags, tmp, len, errno;

	switch(cmd)
	{
	case SETNAME:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		name = (char *) arg1;
		len = strlen_user(name);

		if (len == 0 || len > __NEW_UTS_LEN)
			return -EINVAL;
		down(&uts_sem);
		errno = -EFAULT;
		if (!copy_from_user(system_utsname.nodename, name, len)) {
			system_utsname.nodename[len] = '\0';
			errno = 0;
		}
		up(&uts_sem);
		return errno;

	case MIPS_ATOMIC_SET:
		/* This is broken in case of page faults and SMP ...
		   Risc/OS fauls after maximum 20 tries with EAGAIN.  */
		p = (int *) arg1;
		errno = verify_area(VERIFY_WRITE, p, sizeof(*p));
		if (errno)
			return errno;
		save_and_cli(flags);
		errno = *p;
		*p = arg2;
		restore_flags(flags);

		return errno;		/* This is broken ...  */

	case MIPS_FIXADE:
		tmp = current->thread.mflags & ~3;
		current->thread.mflags = tmp | (arg1 & 3);
		return 0;

	case FLUSH_CACHE:
		flush_cache_all();
		return 0;

	case MIPS_RDNVRAM:
		return -EIO;
	}

	return -EINVAL;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second,
			int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semop (first, (struct sembuf *)ptr, second);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void **) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf *) ptr, 
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;
			
			if (copy_from_user(&tmp,
					   (struct ipc_kludge *) ptr, 
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = sys_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return sys_shmat (first, (char *) ptr, second, (ulong *) third);
		}
	case SHMDT: 
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -EINVAL;
	}
}

/*
 * No implemented yet ...
 */
asmlinkage int
sys_cachectl(char *addr, int nbytes, int op)
{
	return -ENOSYS;
}

/*
 * If we ever come here the user sp is bad.  Zap the process right away.
 * Due to the bad stack signaling wouldn't work.
 */
asmlinkage void bad_stack(void)
{
	do_exit(SIGSEGV);
}

asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}
