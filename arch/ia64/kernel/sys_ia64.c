/*
 * This file contains various system calls that have different calling
 * conventions on different platforms.
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/file.h>		/* doh, must come after sched.h... */
#include <linux/smp.h>
#include <linux/smp_lock.h>

asmlinkage long
ia64_getpriority (int which, int who, long arg2, long arg3, long arg4, long arg5, long arg6, 
		  long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	extern long sys_getpriority (int, int);
	long prio;

	prio = sys_getpriority(which, who);
	if (prio >= 0) {
		regs->r8 = 0;	/* ensure negative priority is not mistaken as error code */
		prio = 20 - prio;
	}
	return prio;
}

asmlinkage unsigned long
sys_getpagesize (void)
{
	return PAGE_SIZE;
}

asmlinkage unsigned long
ia64_shmat (int shmid, void *shmaddr, int shmflg, long arg3, long arg4, long arg5, long arg6,
	    long arg7, long stack)
{
	extern int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr);
	struct pt_regs *regs = (struct pt_regs *) &stack;
	unsigned long raddr;
	int retval;

	retval = sys_shmat(shmid, shmaddr, shmflg, &raddr);
	if (retval < 0)
		return retval;

	regs->r8 = 0;	/* ensure negative addresses are not mistaken as an error code */
	return raddr;
}

asmlinkage unsigned long
ia64_brk (long brk, long arg1, long arg2, long arg3,
	  long arg4, long arg5, long arg6, long arg7, long stack)
{
	extern unsigned long sys_brk (unsigned long brk);
	struct pt_regs *regs = (struct pt_regs *) &stack;
	unsigned long retval;

	retval = sys_brk(brk);

	regs->r8 = 0;	/* ensure large retval isn't mistaken as error code */
	return retval;
}

/*
 * On IA-64, we return the two file descriptors in ret0 and ret1 (r8
 * and r9) as this is faster than doing a copy_to_user().
 */
asmlinkage long
sys_pipe (long arg0, long arg1, long arg2, long arg3,
	  long arg4, long arg5, long arg6, long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	int fd[2];
	int retval;

	lock_kernel();
	retval = do_pipe(fd);
	if (retval)
		goto out;
	retval = fd[0];
	regs->r9 = fd[1];
  out:
	unlock_kernel();
	return retval;
}

static inline unsigned long
do_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, unsigned long pgoff)
{
	struct file *file = 0;

	/*
	 * A zero mmap always succeeds in Linux, independent of
	 * whether or not the remaining arguments are valid.
	 */
	if (PAGE_ALIGN(len) == 0)
		return addr;

#ifdef notyet
	/* Don't permit mappings that would cross a region boundary: */
	region_start = IA64_GET_REGION(addr);
	region_end   = IA64_GET_REGION(addr + len);
	if (region_start != region_end)
		return -EINVAL;

	<<x??x>>
#endif

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return -EBADF;
	}

	down(&current->mm->mmap_sem);
	lock_kernel();

	addr = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);

	unlock_kernel();
	up(&current->mm->mmap_sem);

	if (file)
		fput(file);
	return addr;
}

/*
 * mmap2() is like mmap() except that the offset is expressed in units
 * of PAGE_SIZE (instead of bytes).  This allows to mmap2() (pieces
 * of) files that are larger than the address space of the CPU.
 */
asmlinkage unsigned long
sys_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, long pgoff,
	   long arg6, long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;

	addr = do_mmap2(addr, len, prot, flags, fd, pgoff);
	if (!IS_ERR(addr))
		regs->r8 = 0;	/* ensure large addresses are not mistaken as failures... */
	return addr;
}

asmlinkage unsigned long
sys_mmap (unsigned long addr, unsigned long len, int prot, int flags,
	  int fd, long off, long arg6, long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;

	addr = do_mmap2(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
	if (!IS_ERR(addr))
		regs->r8 = 0;	/* ensure large addresses are not mistaken as failures... */
	return addr;
}

asmlinkage long
sys_ioperm (unsigned long from, unsigned long num, int on)
{
        printk(KERN_ERR "sys_ioperm(from=%lx, num=%lx, on=%d)\n", from, num, on);
        return -EIO;
}

asmlinkage long
sys_iopl (int level, long arg1, long arg2, long arg3)
{
        lock_kernel();
        printk(KERN_ERR "sys_iopl(level=%d)!\n", level);
        unlock_kernel();
        return -ENOSYS;
}

asmlinkage long
sys_vm86 (long arg0, long arg1, long arg2, long arg3)
{
        lock_kernel();
        printk(KERN_ERR "sys_vm86(%lx, %lx, %lx, %lx)!\n", arg0, arg1, arg2, arg3);
        unlock_kernel();
        return -ENOSYS;
}

asmlinkage long
sys_modify_ldt (long arg0, long arg1, long arg2, long arg3)
{
        lock_kernel();
        printk(KERN_ERR "sys_modify_ldt(%lx, %lx, %lx, %lx)!\n", arg0, arg1, arg2, arg3);
        unlock_kernel();
        return -ENOSYS;
}

#ifndef CONFIG_PCI

asmlinkage long
sys_pciconfig_read (unsigned long bus, unsigned long dfn, unsigned long off, unsigned long len,
		    void *buf)
{
	return -ENOSYS;
}

asmlinkage long
sys_pciconfig_write (unsigned long bus, unsigned long dfn, unsigned long off, unsigned long len,
		     void *buf)
{
	return -ENOSYS;
}


#endif /* CONFIG_PCI */
