/*
 * This file contains various system calls that have different calling
 * conventions on different platforms.
 *
 * Copyright (C) 1999-2000, 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
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
#include <linux/highuid.h>

#include <asm/shmparam.h>
#include <asm/uaccess.h>

#ifdef CONFIG_HUGETLB_PAGE
# define SHMLBA_HPAGE		HPAGE_SIZE
# define COLOR_HALIGN(addr)	(((addr) + SHMLBA_HPAGE - 1) & ~(SHMLBA_HPAGE - 1))
# define TASK_HPAGE_BASE	((REGION_HPAGE << REGION_SHIFT) | HPAGE_SIZE)
#endif

unsigned long
arch_get_unmapped_area (struct file *filp, unsigned long addr, unsigned long len,
			unsigned long pgoff, unsigned long flags)
{
	long map_shared = (flags & MAP_SHARED);
	unsigned long align_mask = PAGE_SIZE - 1;
	struct vm_area_struct * vmm;

	if (len > RGN_MAP_LIMIT)
		return -ENOMEM;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	if (map_shared && (TASK_SIZE > 0xfffffffful))
		/*
		 * For 64-bit tasks, align shared segments to 1MB to avoid potential
		 * performance penalty due to virtual aliasing (see ASDM).  For 32-bit
		 * tasks, we prefer to avoid exhausting the address space too quickly by
		 * limiting alignment to a single page.
		 */
		align_mask = SHMLBA - 1;

	addr = (addr + align_mask) & ~align_mask;

	for (vmm = find_vma(current->mm, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (REGION_OFFSET(addr) + len > RGN_MAP_LIMIT)	/* no risk of overflow here... */
			return -ENOMEM;
		if (!vmm || addr + len <= vmm->vm_start)
			return addr;
		addr = (vmm->vm_end + align_mask) & ~align_mask;
	}
}

asmlinkage long
ia64_getpriority (int which, int who)
{
	extern long sys_getpriority (int, int);
	long prio;

	prio = sys_getpriority(which, who);
	if (prio >= 0) {
		force_successful_syscall_return();
		prio = 20 - prio;
	}
	return prio;
}

/* XXX obsolete, but leave it here until the old libc is gone... */
asmlinkage unsigned long
sys_getpagesize (void)
{
	return PAGE_SIZE;
}

asmlinkage unsigned long
ia64_shmat (int shmid, void *shmaddr, int shmflg)
{
	unsigned long raddr;
	int retval;

	retval = sys_shmat(shmid, shmaddr, shmflg, &raddr);
	if (retval < 0)
		return retval;

	force_successful_syscall_return();
	return raddr;
}

asmlinkage unsigned long
ia64_brk (unsigned long brk)
{
	extern int vm_enough_memory (long pages);
	unsigned long rlim, retval, newbrk, oldbrk;
	struct mm_struct *mm = current->mm;

	/*
	 * Most of this replicates the code in sys_brk() except for an additional safety
	 * check and the clearing of r8.  However, we can't call sys_brk() because we need
	 * to acquire the mmap_sem before we can do the test...
	 */
	down_write(&mm->mmap_sem);

	if (brk < mm->end_code)
		goto out;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk)
		goto set_brk;

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		if (!do_munmap(mm, newbrk, oldbrk-newbrk))
			goto set_brk;
		goto out;
	}

	/* Check against unimplemented/unmapped addresses: */
	if ((newbrk - oldbrk) > RGN_MAP_LIMIT || REGION_OFFSET(newbrk) > RGN_MAP_LIMIT)
		goto out;

	/* Check against rlimit.. */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim < RLIM_INFINITY && brk - mm->start_data > rlim)
		goto out;

	/* Check against existing mmap mappings. */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/* Ok, looks good - let it rip. */
	if (do_brk(oldbrk, newbrk-oldbrk) != oldbrk)
		goto out;
set_brk:
	mm->brk = brk;
out:
	retval = mm->brk;
	up_write(&mm->mmap_sem);
	force_successful_syscall_return();
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

	retval = do_pipe(fd);
	if (retval)
		goto out;
	retval = fd[0];
	regs->r9 = fd[1];
  out:
	return retval;
}

static inline unsigned long
do_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, unsigned long pgoff)
{
	unsigned long roff;
	struct file *file = 0;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return -EBADF;

		if (!file->f_op || !file->f_op->mmap) {
			addr = -ENODEV;
			goto out;
		}
	}

	/*
	 * A zero mmap always succeeds in Linux, independent of whether or not the
	 * remaining arguments are valid.
	 */
	len = PAGE_ALIGN(len);
	if (len == 0)
		goto out;

	/*
	 * Don't permit mappings into unmapped space, the virtual page table of a region,
	 * or across a region boundary.  Note: RGN_MAP_LIMIT is equal to 2^n-PAGE_SIZE
	 * (for some integer n <= 61) and len > 0.
	 */
	roff = REGION_OFFSET(addr);
	if ((len > RGN_MAP_LIMIT) || (roff > (RGN_MAP_LIMIT - len))) {
		addr = -EINVAL;
		goto out;
	}

	down_write(&current->mm->mmap_sem);
	addr = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

out:	if (file)
		fput(file);
	return addr;
}

/*
 * mmap2() is like mmap() except that the offset is expressed in units
 * of PAGE_SIZE (instead of bytes).  This allows to mmap2() (pieces
 * of) files that are larger than the address space of the CPU.
 */
asmlinkage unsigned long
sys_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, long pgoff)
{
	addr = do_mmap2(addr, len, prot, flags, fd, pgoff);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

asmlinkage unsigned long
sys_mmap (unsigned long addr, unsigned long len, int prot, int flags, int fd, long off)
{
	if ((off & ~PAGE_MASK) != 0)
		return -EINVAL;

	addr = do_mmap2(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

#ifdef CONFIG_HUGETLB_PAGE

asmlinkage unsigned long
sys_alloc_hugepages (int key, unsigned long addr, size_t len, int prot, int flag)
{
	struct mm_struct *mm = current->mm;
	long retval;
	extern int alloc_hugetlb_pages (int, unsigned long, unsigned long, int, int);

	if ((key < 0) || (len & (HPAGE_SIZE - 1)))
		return -EINVAL;

	if (addr && ((REGION_NUMBER(addr) != REGION_HPAGE) || (addr & (HPAGE_SIZE - 1))))
		addr = TASK_HPAGE_BASE;

	if (!addr)
		addr = TASK_HPAGE_BASE;
	down_write(&mm->mmap_sem);
	{
		retval = arch_get_unmapped_area(NULL, COLOR_HALIGN(addr), len, 0, 0);
		if (retval != -ENOMEM)
			retval = alloc_hugetlb_pages(key, retval, len, prot, flag);
	}
	up_write(&mm->mmap_sem);

	if (IS_ERR((void *) retval))
		return retval;

	force_successful_syscall_return();
	return retval;
}

asmlinkage int
sys_free_hugepages (unsigned long  addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	extern int free_hugepages(struct vm_area_struct *);
	int retval;

	vma = find_vma(mm, addr);
	if (!vma || !is_vm_hugetlb_page(vma) || (vma->vm_start != addr))
		return -EINVAL;

	down_write(&mm->mmap_sem);
	{
		spin_lock(&mm->page_table_lock);
		{
			retval = free_hugepages(vma);
		}
		spin_unlock(&mm->page_table_lock);
	}
	up_write(&mm->mmap_sem);
	return retval;
}

#else /* !CONFIG_HUGETLB_PAGE */

asmlinkage unsigned long
sys_alloc_hugepages (int key, size_t addr, unsigned long len, int prot, int flag)
{
	return -ENOSYS;
}

asmlinkage unsigned long
sys_free_hugepages (unsigned long  addr)
{
	return -ENOSYS;
}

#endif /* !CONFIG_HUGETLB_PAGE */

asmlinkage long
sys_vm86 (long arg0, long arg1, long arg2, long arg3)
{
	printk(KERN_ERR "sys_vm86(%lx, %lx, %lx, %lx)!\n", arg0, arg1, arg2, arg3);
	return -ENOSYS;
}

asmlinkage unsigned long
ia64_create_module (const char *name_user, size_t size)
{
	extern unsigned long sys_create_module (const char *, size_t);
	unsigned long   addr;

	addr = sys_create_module (name_user, size);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
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
