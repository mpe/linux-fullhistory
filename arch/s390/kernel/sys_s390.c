/*
 *  arch/s390/kernel/sys_s390.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 *  Derived from "arch/i386/kernel/sys_i386.c"
 *
 *  This file contains various random system calls that
 *  have a non-standard calling sequence on the Linux/s390
 *  platform.
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
#include <linux/file.h>
#include <linux/utsname.h>
#ifdef CONFIG_ARCH_S390X
#include <linux/personality.h>
#endif /* CONFIG_ARCH_S390X */

#include <asm/uaccess.h>
#include <asm/ipc.h>

#ifndef CONFIG_ARCH_S390X
#define __SYS_RETTYPE int
#else
#define __SYS_RETTYPE long
#endif /* CONFIG_ARCH_S390X */

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage __SYS_RETTYPE sys_pipe(unsigned long * fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

/* common code for old and new mmaps */
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	__SYS_RETTYPE error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux for S/390 isn't able to handle more than 5
 * system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage long sys_mmap2(struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;
	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
out:
	return error;
}

asmlinkage __SYS_RETTYPE old_mmap(struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	__SYS_RETTYPE error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT);
out:
	return error;
}

extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

#ifndef CONFIG_ARCH_S390X
struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

asmlinkage int old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);

}
#else /* CONFIG_ARCH_S390X */
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags)
{
	struct vm_area_struct *vma;
	unsigned long end;

	if (test_thread_flag(TIF_31BIT)) { 
		if (!addr) 
			addr = 0x40000000; 
		end = 0x80000000;		
	} else { 
		if (!addr) 
			addr = TASK_SIZE / 2;
		end = TASK_SIZE; 
	}

	if (len > end)
		return -ENOMEM;
	addr = PAGE_ALIGN(addr);

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (end - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = vma->vm_end;
	}
}
#endif /* CONFIG_ARCH_S390X */

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage __SYS_RETTYPE sys_ipc (uint call, int first, int second, 
				  unsigned long third, void *ptr)
{
        struct ipc_kludge tmp;
	int ret;

        switch (call) {
        case SEMOP:
		return sys_semtimedop (first, (struct sembuf *) ptr, second,
				       NULL);
	case SEMTIMEDOP:
		return sys_semtimedop (first, (struct sembuf *) ptr, second,
				       (const struct timespec *) third);
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
		break;
        case MSGRCV:
                if (!ptr)
                        return -EINVAL;
                if (copy_from_user (&tmp, (struct ipc_kludge *) ptr,
                                    sizeof (struct ipc_kludge)))
                        return -EFAULT;
                return sys_msgrcv (first, tmp.msgp,
                                   second, tmp.msgtyp, third);
        case MSGGET:
                return sys_msgget ((key_t) first, second);
        case MSGCTL:
                return sys_msgctl (first, second, (struct msqid_ds *) ptr);
                
	case SHMAT: {
		ulong raddr;
		ret = sys_shmat (first, (char *) ptr, second, &raddr);
		if (ret)
			return ret;
		return put_user (raddr, (ulong *) third);
		break;
        }
	case SHMDT: 
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
                                   (struct shmid_ds *) ptr);
	default:
		return -ENOSYS;

	}
        
	return -EINVAL;
}

/*
 * Old cruft
 */
asmlinkage int sys_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

#ifndef CONFIG_ARCH_S390X
asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
  	down_read(&uts_sem);
	
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error |= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error |= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error |= __put_user(0,name->release+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error |= __put_user(0,name->version+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error |= __put_user(0,name->machine+__OLD_UTS_LEN);
	
	up_read(&uts_sem);
	
	error = error ? -EFAULT : 0;

	return error;
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -ENOSYS;
}

#else /* CONFIG_ARCH_S390X */

extern asmlinkage int sys_newuname(struct new_utsname * name);

asmlinkage int s390x_newuname(struct new_utsname * name)
{
	int ret = sys_newuname(name);

	if (current->personality == PER_LINUX32 && !ret) {
		ret = copy_to_user(name->machine, "s390\0\0\0\0", 8);
		if (ret) ret = -EFAULT;
	}
	return ret;
}

extern asmlinkage long sys_personality(unsigned long);

asmlinkage int s390x_personality(unsigned long personality)
{
	int ret;

	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;

	return ret;
}
#endif /* CONFIG_ARCH_S390X */

/*
 * Wrapper function for sys_fadvise64/fadvise64_64
 */
#ifndef CONFIG_ARCH_S390X

extern asmlinkage long sys_fadvise64(int, loff_t, size_t, int);

asmlinkage long
s390_fadvise64(int fd, u32 offset_high, u32 offset_low, size_t len, int advice)
{
	return sys_fadvise64(fd, (u64) offset_high << 32 | offset_low,
			len, advice);
}

#endif

extern asmlinkage long sys_fadvise64_64(int, loff_t, loff_t, int);

struct fadvise64_64_args {
	int fd;
	long long offset;
	long long len;
	int advice;
};

asmlinkage long
s390_fadvise64_64(struct fadvise64_64_args *args)
{
	struct fadvise64_64_args a;

	if ( copy_from_user(&a, args, sizeof(a)) )
		return -EFAULT;
	return sys_fadvise64_64(a.fd, a.offset, a.len, a.advice);
}

