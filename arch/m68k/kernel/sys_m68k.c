/*
 * linux/arch/m68k/kernel/sys_m68k.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/m68k
 * platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/cachectl.h>
#include <asm/traps.h>
#include <asm/ipc.h>

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

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/m68k cloned Linux/i386, which didn't use to be able to
 * handle more than 4 system call parameters, so these system calls
 * used a memory block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage int old_mmap(struct mmap_arg_struct *arg)
{
	int error;
	struct file * file = NULL;
	struct mmap_arg_struct a;

	error = verify_area(VERIFY_READ, arg, sizeof(*arg));
	if (error)
		return error;
	copy_from_user(&a, arg, sizeof(a));
	if (!(a.flags & MAP_ANONYMOUS)) {
		if (a.fd >= NR_OPEN || !(file = current->files->fd[a.fd]))
			return -EBADF;
	}
	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	error = do_mmap(file, a.addr, a.len, a.prot, a.flags, a.offset);
	return error;
}


extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

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
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
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
			int err;
			if (!ptr)
				return -EINVAL;
			if ((err = verify_area (VERIFY_READ, ptr, sizeof(long))))
				return err;
			get_user(fourth.__pad, (void **)ptr);
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
				if (copy_from_user (&tmp, ptr, sizeof (tmp)))
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
				if ((err = verify_area(VERIFY_WRITE, (ulong*) third, sizeof(ulong))))
					return err;
				err = sys_shmat (first, (char *) ptr, second, &raddr);
				if (err)
					return err;
				put_user (raddr, (ulong *) third);
				return 0;
				}
			case 1:	/* iBCS2 emulator entry point */
				if (get_fs() != get_ds())
					return -EINVAL;
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

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
  return -ENOSYS;
}

/* Convert virtual address VADDR to physical address PADDR, recording
   in VALID whether the virtual address is actually mapped.  */
#define virt_to_phys_040(vaddr, paddr, valid)				\
{									\
  unsigned long _mmusr;							\
									\
  __asm__ __volatile__ (".chip 68040\n\t"				\
			"ptestr (%1)\n\t"				\
			"movec %%mmusr,%0\n\t"				\
			".chip 68k"					\
			: "=r" (_mmusr)					\
			: "a" (vaddr));					\
  if (!(_mmusr & MMU_R_040))						\
    (valid) = 0;							\
  else									\
    {									\
      (valid) = 1;							\
      (paddr) = _mmusr & PAGE_MASK;					\
    }									\
}

static inline int
cache_flush_040 (unsigned long addr, int scope, int cache, unsigned long len)
{
  unsigned long paddr;
  int valid;

  switch (scope)
    {
    case FLUSH_SCOPE_ALL:
      switch (cache)
	{
	case FLUSH_CACHE_DATA:
	  /* This nop is needed for some broken versions of the 68040.  */
	  __asm__ __volatile__ ("nop\n\t"
				".chip 68040\n\t"
				"cpusha %dc\n\t"
				".chip 68k");
	  break;
	case FLUSH_CACHE_INSN:
	  __asm__ __volatile__ ("nop\n\t"
				".chip 68040\n\t"
				"cpusha %ic\n\t"
				".chip 68k");
	  break;
	default:
	case FLUSH_CACHE_BOTH:
	  __asm__ __volatile__ ("nop\n\t"
				".chip 68040\n\t"
				"cpusha %bc\n\t"
				".chip 68k");
	  break;
	}
      break;

    case FLUSH_SCOPE_LINE:
      len >>= 4;
      /* Find the physical address of the first mapped page in the
	 address range.  */
      for (;;)
	{
	  virt_to_phys_040 (addr, paddr, valid);
	  if (valid)
	    break;
	  if (len <= PAGE_SIZE / 16)
	    return 0;
	  len -= (PAGE_SIZE - (addr & PAGE_MASK)) / 16;
	  addr = (addr + PAGE_SIZE) & PAGE_MASK;
	}
      while (len--)
	{
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushl %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushl %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushl %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	  addr += 16;
	  if (len)
	    {
	      if ((addr & (PAGE_SIZE-1)) < 16)
		{
		  /* Recompute physical address when crossing a page
		     boundary. */
		  for (;;)
		    {
		      virt_to_phys_040 (addr, paddr, valid);
		      if (valid)
			break;
		      if (len <= PAGE_SIZE / 16)
			return 0;
		      len -= (PAGE_SIZE - (addr & PAGE_MASK)) / 16;
		      addr = (addr + PAGE_SIZE) & PAGE_MASK;
		    }
		}
	      else
		paddr += 16;
	    }
	}
      break;

    default:
    case FLUSH_SCOPE_PAGE:
      for (len >>= PAGE_SHIFT; len--; addr += PAGE_SIZE)
	{
	  virt_to_phys_040 (addr, paddr, valid);
	  if (!valid)
	    continue;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushp %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushp %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushp %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	}
      break;
    }
  return 0;
}

#define virt_to_phys_060(vaddr, paddr, valid)		\
{							\
  __asm__ __volatile__ (".chip 68060\n\t"		\
			"plpar (%0)\n\t"		\
			".chip 68k"			\
			: "=a" (paddr)			\
			: "0" (vaddr));			\
  (valid) = 1; /* XXX */				\
}

static inline int
cache_flush_060 (unsigned long addr, int scope, int cache, unsigned long len)
{
  unsigned long paddr;
  int valid;

  switch (scope)
    {
    case FLUSH_SCOPE_ALL:
      switch (cache)
	{
	case FLUSH_CACHE_DATA:
	  __asm__ __volatile__ (".chip 68060\n\t"
				"cpusha %dc\n\t"
				"cinva %dc\n\t"
				".chip 68k");
	  break;
	case FLUSH_CACHE_INSN:
	  __asm__ __volatile__ (".chip 68060\n\t"
				"cpusha %ic\n\t"
				"cinva %ic\n\t"
				".chip 68k");
	  break;
	default:
	case FLUSH_CACHE_BOTH:
	  __asm__ __volatile__ (".chip 68060\n\t"
				"cpusha %bc\n\t"
				"cinva %bc\n\t"
				".chip 68k");
	  break;
	}
      break;

    case FLUSH_SCOPE_LINE:
      len >>= 4;
      /* Find the physical address of the first mapped page in the
	 address range.  */
      for (;;)
	{
	  virt_to_phys_060 (addr, paddr, valid);
	  if (valid)
	    break;
	  if (len <= PAGE_SIZE / 16)
	    return 0;
	  len -= (PAGE_SIZE - (addr & PAGE_MASK)) / 16;
	  addr = (addr + PAGE_SIZE) & PAGE_MASK;
	}
      while (len--)
	{
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushl %%dc,(%0)\n\t"
				    "cinvl %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushl %%ic,(%0)\n\t"
				    "cinvl %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushl %%bc,(%0)\n\t"
				    "cinvl %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	  addr += 16;
	  if (len)
	    {
	      if ((addr & (PAGE_SIZE-1)) < 16)
		{
		  /* Recompute the physical address when crossing a
		     page boundary.  */
		  for (;;)
		    {
		      virt_to_phys_060 (addr, paddr, valid);
		      if (valid)
			break;
		      if (len <= PAGE_SIZE / 16)
			return 0;
		      len -= (PAGE_SIZE - (addr & PAGE_MASK)) / 16;
		      addr = (addr + PAGE_SIZE) & PAGE_MASK;
		    }
		}
	      else
		paddr += 16;
	    }
	}
      break;

    default:
    case FLUSH_SCOPE_PAGE:
      for (len >>= PAGE_SHIFT; len--; addr += PAGE_SIZE)
	{
	  virt_to_phys_060 (addr, paddr, valid);
	  if (!valid)
	    continue;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushp %%dc,(%0)\n\t"
				    "cinvp %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushp %%ic,(%0)\n\t"
				    "cinvp %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushp %%bc,(%0)\n\t"
				    "cinvp %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	}
      break;
    }
  return 0;
}

/* sys_cacheflush -- flush (part of) the processor cache.  */
asmlinkage int
sys_cacheflush (unsigned long addr, int scope, int cache, unsigned long len)
{
  struct vm_area_struct *vma;

  if (scope < FLUSH_SCOPE_LINE || scope > FLUSH_SCOPE_ALL
      || cache & ~FLUSH_CACHE_BOTH)
    return -EINVAL;

  if (scope == FLUSH_SCOPE_ALL)
    {
      /* Only the superuser may flush the whole cache. */
      if (!suser ())
	return -EPERM;
    }
  else
    {
      /* Verify that the specified address region actually belongs to
	 this process.  */
      vma = find_vma (current->mm, addr);
      if (vma == NULL || addr < vma->vm_start || addr + len > vma->vm_end)
	return -EINVAL;
    }

  if (CPU_IS_020_OR_030) {
    if (scope == FLUSH_SCOPE_LINE)
      {
	unsigned long cacr;
	__asm__ ("movec %%cacr, %0" : "=r" (cacr));
	if (cache & FLUSH_CACHE_INSN)
	  cacr |= 4;
	if (cache & FLUSH_CACHE_DATA)
	  cacr |= 0x400;
	len >>= 4;
	while (len--)
	  {
	    __asm__ __volatile__ ("movec %1, %%caar\n\t"
				  "movec %0, %%cacr"
				  : /* no outputs */
				  : "r" (cacr), "r" (addr));
	    addr += 16;
	  }
      }
    else
      {
	/* Flush the whole cache, even if page granularity is requested.  */
	unsigned long cacr;
	__asm__ ("movec %%cacr, %0" : "=r" (cacr));
	if (cache & FLUSH_CACHE_INSN)
	  cacr |= 8;
	if (cache & FLUSH_CACHE_DATA)
	  cacr |= 0x800;
	__asm__ __volatile__ ("movec %0, %%cacr" : : "r" (cacr));
      }
    return 0;
  } else if (CPU_IS_040)
    return cache_flush_040 (addr, scope, cache, len);
  else if (CPU_IS_060)
    return cache_flush_060 (addr, scope, cache, len);
}
