/*
 * linux/arch/m32r/kernel/sys_m32r.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/M32R platform.
 *
 * Taken from i386 version.
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
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/cachectl.h>
#include <asm/cacheflush.h>
#include <asm/ipc.h>

/*
 * sys_tas() - test-and-set
 * linuxthreads testing version
 */
#ifndef CONFIG_SMP
asmlinkage int sys_tas(int *addr)
{
	int oldval;
	unsigned long flags;

	if (!access_ok(VERIFY_WRITE, addr, sizeof (int)))
		return -EFAULT;
	local_irq_save(flags);
	oldval = *addr;
	*addr = 1;
	local_irq_restore(flags);
	return oldval;
}
#else /* CONFIG_SMP */
#include <linux/spinlock.h>

static DEFINE_SPINLOCK(tas_lock);

asmlinkage int sys_tas(int *addr)
{
	int oldval;

	if (!access_ok(VERIFY_WRITE, addr, sizeof (int)))
		return -EFAULT;

	_raw_spin_lock(&tas_lock);
	oldval = *addr;
	*addr = 1;
	_raw_spin_unlock(&tas_lock);

	return oldval;
}
#endif /* CONFIG_SMP */

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage int
sys_pipe(unsigned long r0, unsigned long r1, unsigned long r2,
	unsigned long r3, unsigned long r4, unsigned long r5,
	unsigned long r6, struct pt_regs regs)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user((void *)r0, (void *)fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file *file = NULL;

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
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc(uint call, int first, int second,
		       int third, void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				      second, NULL);
	case SEMTIMEDOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				      second, (const struct timespec __user *)fifth);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void __user * __user *) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
		}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf __user *) ptr,
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;

			if (copy_from_user(&tmp,
					   (struct ipc_kludge __user *) ptr,
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
			}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf __user *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second,
				   (struct msqid_ds __user *) ptr);
	case SHMAT: {
		ulong raddr;

		if ((ret = verify_area(VERIFY_WRITE, (ulong __user *) third,
				      sizeof(ulong))))
			return ret;
		ret = do_shmat (first, (char __user *) ptr, second, &raddr);
		if (ret)
			return ret;
		return put_user (raddr, (ulong __user *) third);
		}
	case SHMDT:
		return sys_shmdt ((char __user *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds __user *) ptr);
	default:
		return -ENOSYS;
	}
}

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

asmlinkage int sys_cacheflush(void *addr, int bytes, int cache)
{
	/* This should flush more selectivly ...  */
	_flush_cache_all();
	return 0;
}

asmlinkage int sys_cachectl(char *addr, int nbytes, int op)
{
	/* Not implemented yet. */
	return -ENOSYS;
}

