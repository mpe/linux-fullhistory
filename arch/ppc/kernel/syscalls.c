/*
 * linux/arch/ppc/kernel/sys_ppc.c
 *
 * Adapted from the i386 version by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu)
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/PPC
 * platform.
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
#include <asm/uaccess.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long * fildes)
{
	int error;

	lock_kernel();
	error = verify_area(VERIFY_WRITE,fildes,8);
	if (error)
		goto out;
	error = do_pipe(fildes);
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

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */
asmlinkage int old_mmap(unsigned long *buffer)
{
	int error;
	unsigned long flags;
	long a,b,c,d,e;
	struct file * file = NULL;

	lock_kernel();
	error = verify_area(VERIFY_READ, buffer, 6*sizeof(long));
	if (error)
		goto out;
	get_user(flags,buffer+3);
	if (!(flags & MAP_ANONYMOUS)) {
		unsigned long fd;
		get_user(fd,buffer+4);
		error = -EBADF;
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			goto out;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	error = -EFAULT;
	if ( get_user(a,buffer) || get_user(b,buffer+1) ||
	     get_user(c,buffer+2)||get_user(d,buffer+5)	)
		goto out;
	error = do_mmap(file,a,b,c, flags, d);
out:
	unlock_kernel();
	return error;
}

extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

asmlinkage int old_select(unsigned long *buffer)
{
	int n;
	fd_set *inp;
	fd_set *outp;
	fd_set *exp;
	struct timeval *tvp;

	lock_kernel();
	n = verify_area(VERIFY_READ, buffer, 5*sizeof(unsigned long));
	if (n)
		goto out;
	get_user(n,buffer);
	get_user(inp,buffer+1);
	get_user(outp,buffer+2);
	get_user(exp,buffer+3);
	get_user(tvp,buffer+4);
	n = sys_select(n, inp, outp, exp, tvp);
out:
	unlock_kernel();
	return n;
}
#if 0

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	lock_kernel();
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			ret = sys_semop (first, (struct sembuf *)ptr, second);
			goto out;
		case SEMGET:
			ret = sys_semget (first, second, third);
			goto out;
		case SEMCTL: {
			union semun fourth;
			ret = -EINVAL;
			if (!ptr)
				goto out;
			if ((ret = verify_area (VERIFY_READ, ptr, sizeof(long))))
				goto out;
			fourth.__pad = (void *) get_fs_long(ptr);
			ret = sys_semctl (first, second, third, fourth);
			goto out;
			}
		default:
			ret = -EINVAL;
			goto out;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			ret = sys_msgsnd (first, (struct msgbuf *) ptr, 
					  second, third);
			goto out;
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				ret = -EINVAL;
				if (!ptr)
					goto out;
				if ((ret = verify_area (VERIFY_READ, ptr, sizeof(tmp))))
					goto out;
				memcpy_fromfs (&tmp,(struct ipc_kludge *) ptr,
					       sizeof (tmp));
				ret = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				goto out;
				}
			case 1: default:
				ret = sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, third);
				goto out;
			}
		case MSGGET:
			ret = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			ret = sys_msgctl (first, second, (struct msqid_ds *) ptr);
			goto out;
		default:
			ret = -EINVAL;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				if ((ret = verify_area(VERIFY_WRITE, (ulong*) third, sizeof(ulong))))
					goto out;
				ret = sys_shmat (first, (char *) ptr, second, &raddr);
				if (ret)
					goto out;
				put_fs_long (raddr, (ulong *) third);
				ret = 0;
				goto out;
				}
			case 1:	/* iBCS2 emulator entry point */
				ret = -EINVAL;
				if (get_fs() != get_ds())
					goto out;
				ret = sys_shmat (first, (char *) ptr, second, (ulong *) third);
				goto out;
			}
		case SHMDT: 
			ret = sys_shmdt ((char *)ptr);
			goto out;
		case SHMGET:
			ret = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			ret = sys_shmctl (first, second, (struct shmid_ds *) ptr);
			goto out;
		default:
			ret = -EINVAL;
			goto out;
		}
	else
		ret = -EINVAL;
out:
	unlock_kernel();
	return ret;
}
#endif
