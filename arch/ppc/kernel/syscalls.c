/*
 * linux/arch/ppc/kernel/sys_ppc.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Derived from "arch/i386/kernel/sys_i386.c"
 * Adapted from the i386 version by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras (paulus@cs.anu.edu.au).
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/PPC
 * platform.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

void
check_bugs(void)
{
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	printk(KERN_ERR "sys_ioperm()\n");
	return -EIO;
}

int sys_iopl(int a1, int a2, int a3, int a4)
{
	lock_kernel();
	printk(KERN_ERR "sys_iopl(%x, %x, %x, %x)!\n", a1, a2, a3, a4);
	unlock_kernel();
	return (-ENOSYS);
}

int sys_vm86(int a1, int a2, int a3, int a4)
{
	lock_kernel();
	printk(KERN_ERR "sys_vm86(%x, %x, %x, %x)!\n", a1, a2, a3, a4);
	unlock_kernel();
	return (-ENOSYS);
}

int sys_modify_ldt(int a1, int a2, int a3, int a4)
{
	lock_kernel();
	printk(KERN_ERR "sys_modify_ldt(%x, %x, %x, %x)!\n", a1, a2, a3, a4);
	unlock_kernel();
	return (-ENOSYS);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int 
sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	lock_kernel();
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	ret = -EINVAL;
	switch (call) {
	case SEMOP:
		ret = sys_semop (first, (struct sembuf *)ptr, second);
		break;
	case SEMGET:
		ret = sys_semget (first, second, third);
		break;
	case SEMCTL: {
		union semun fourth;

		if (!ptr)
			break;
		if ((ret = verify_area (VERIFY_READ, ptr, sizeof(long)))
		    || (ret = get_user(fourth.__pad, (void **)ptr)))
			break;
		ret = sys_semctl (first, second, third, fourth);
		break;
		}
	case MSGSND:
		ret = sys_msgsnd (first, (struct msgbuf *) ptr, second, third);
		break;
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;

			if (!ptr)
				break;
			if ((ret = verify_area (VERIFY_READ, ptr, sizeof(tmp)))
			    || (ret = copy_from_user(&tmp,
						(struct ipc_kludge *) ptr,
						sizeof (tmp))))
				break;
			ret = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp,
					  third);
			break;
			}
		default:
			ret = sys_msgrcv (first, (struct msgbuf *) ptr,
					  second, fifth, third);
			break;
		}
		break;
	case MSGGET:
		ret = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		ret = sys_msgctl (first, second, (struct msqid_ds *) ptr);
		break;
	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;

			if ((ret = verify_area(VERIFY_WRITE, (ulong*) third,
					       sizeof(ulong))))
				break;
			ret = sys_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				break;
			ret = put_user (raddr, (ulong *) third);
			break;
			}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				break;
			ret = sys_shmat (first, (char *) ptr, second,
					 (ulong *) third);
			break;
		}
		break;
	case SHMDT: 
		ret = sys_shmdt ((char *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget (first, second, third);
		break;
	case SHMCTL:
		ret = sys_shmctl (first, second, (struct shmid_ds *) ptr);
		break;
	}

	unlock_kernel();
	return ret;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(int *fildes)
{
	int fd[2];
	int error;

	error = verify_area(VERIFY_WRITE, fildes, 8);
	if (error)
		return error;
	lock_kernel();
	error = do_pipe(fd);
	unlock_kernel();
	if (error)
		return error;
	if (__put_user(fd[0],0+fildes)
	    || __put_user(fd[1],1+fildes))
		return -EFAULT;	/* should we close the fds? */
	return 0;
}

asmlinkage unsigned long sys_mmap(unsigned long addr, size_t len,
				  unsigned long prot, unsigned long flags,
				  unsigned long fd, off_t offset)
{
	struct file * file = NULL;
	int ret = -EBADF;

	lock_kernel();
	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			goto out;
	}
	
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	ret = do_mmap(file, addr, len, prot, flags, offset);
out:
	unlock_kernel();
	return ret;
}

extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

/*
 * Due to some executables calling the wrong select we sometimes
 * get wrong args.  This determines how the args are being passed
 * (a single ptr to them all args passed) then calls
 * sys_select() with the appropriate args. -- Cort
 */
asmlinkage int 
ppc_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	if ( (unsigned long)n >= 4096 )
	{
		unsigned long *buffer = (unsigned long *)n;
		if (verify_area(VERIFY_READ, buffer, 5*sizeof(unsigned long))
		    || __get_user(n, buffer)
		    || __get_user(inp, ((fd_set **)(buffer+1)))
		    || __get_user(outp, ((fd_set **)(buffer+2)))
		    || __get_user(exp, ((fd_set **)(buffer+3)))
		    || __get_user(tvp, ((struct timeval **)(buffer+4))))
			return -EFAULT;
	}
	return sys_select(n, inp, outp, exp, tvp);
}

asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

asmlinkage int sys_uname(struct old_utsname * name)
{
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		return 0;
	return -EFAULT;
}

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
