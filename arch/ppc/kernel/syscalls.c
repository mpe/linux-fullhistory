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

#include <linux/config.h>
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
#include <linux/ipc.h>
#include <asm/uaccess.h>
#include <asm/ipc.h>


void
check_bugs(void)
{
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	printk("sys_ioperm()\n");
	return -EIO;
}

int sys_iopl(int a1, int a2, int a3, int a4)
{
	lock_kernel();
	printk( "sys_iopl(%x, %x, %x, %x)!\n", a1, a2, a3, a4);
	unlock_kernel();
	return (ENOSYS);
}

int sys_vm86(int a1, int a2, int a3, int a4)
{
	lock_kernel();
	printk( "sys_vm86(%x, %x, %x, %x)!\n", a1, a2, a3, a4);
	unlock_kernel();
	return (ENOSYS);
}

int sys_modify_ldt(int a1, int a2, int a3, int a4)
{
	lock_kernel();
	printk( "sys_modify_ldt(%x, %x, %x, %x)!\n", a1, a2, a3, a4);
	unlock_kernel();
	return (ENOSYS);
}

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
			ret = -EFAULT;
			if (get_user(fourth.__pad, (void **) ptr))
				goto out;	
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
				ret = -EFAULT;
				if (copy_from_user(&tmp,(struct ipc_kludge *) ptr, 
						   sizeof (tmp)))
					goto out; 	
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
				ret = sys_shmat (first, (char *) ptr, second, &raddr);
				if (ret)
					goto out;
				ret = put_user (raddr, (ulong *) third);
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


#ifndef CONFIG_MODULES
void
scsi_register_module(void)
{
	lock_kernel();
	panic("scsi_register_module");
	unlock_kernel();
}

void
scsi_unregister_module(void)
{
	lock_kernel();
	panic("scsi_unregister_module");
	unlock_kernel();
}
#endif

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long * fildes)
{
	int fd[2];
	int error;

	error = verify_area(VERIFY_WRITE,fildes,8);
	if (error)
		return error;
	error = do_pipe(fd);
	if (error)
		return error;
	put_user(fd[0],0+fildes);
	put_user(fd[1],1+fildes);
	return 0;
}

asmlinkage unsigned long sys_mmap(unsigned long addr, size_t len,
				  unsigned long prot, unsigned long flags,
				  unsigned long fd, off_t offset)
{
	struct file * file = NULL;
	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	return do_mmap(file, addr, len, prot, flags, offset);
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
	int err;
	if ( (unsigned long)n >= 4096 )
	{
		unsigned long *buffer = (unsigned long *)n;
		if ( get_user(n, buffer) ||
		     get_user(inp,buffer+1) ||
		     get_user(outp,buffer+2) ||
		     get_user(exp,buffer+3) ||
		     get_user(tvp,buffer+4) )
			return -EFAULT;
	}
	return sys_select(n, inp, outp, exp, tvp);
}
