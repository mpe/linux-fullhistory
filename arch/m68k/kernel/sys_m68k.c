/*
 * linux/arch/m68k/kernel/sys_m68k.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/m68k
 * platform.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>

#include <asm/segment.h>
#include <asm/cachectl.h>

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

asmlinkage int old_mmap(unsigned long *buffer)
{
	int error;
	unsigned long flags;
	struct file * file = NULL;

	error = verify_area(VERIFY_READ, buffer, 6*sizeof(long));
	if (error)
		return error;
	flags = get_user(buffer+3);
	if (!(flags & MAP_ANONYMOUS)) {
		unsigned long fd = get_user(buffer+4);
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	return do_mmap(file, get_user(buffer), get_user(buffer+1),
		       get_user(buffer+2), flags, get_user(buffer+5));
}


extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

asmlinkage int old_select(unsigned long *buffer)
{
	int n;
	fd_set *inp;
	fd_set *outp;
	fd_set *exp;
	struct timeval *tvp;

	n = verify_area(VERIFY_READ, buffer, 5*sizeof(unsigned long));
	if (n)
	  return n;

	n = get_user(buffer);
	inp = (fd_set *) get_user(buffer+1);
	outp = (fd_set *) get_user(buffer+2);
	exp = (fd_set *) get_user(buffer+3);
	tvp = (struct timeval *) get_user(buffer+4);
	return sys_select(n, inp, outp, exp, tvp);
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
			fourth.__pad = get_user((void **)ptr);
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
				int err;
				if (!ptr)
					return -EINVAL;
				if ((err = verify_area (VERIFY_READ, ptr, sizeof(tmp))))
					return err;
				memcpy_fromfs (&tmp,(struct ipc_kludge *) ptr,
					       sizeof (tmp));
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
  register unsigned long _tmp1 __asm__ ("a0") = (vaddr);		\
  register unsigned long _tmp2 __asm__ ("d0");				\
  unsigned long _mmusr;							\
									\
  __asm__ __volatile__ (".word 0xf568 /* ptestr (%1) */\n\t"		\
			".long 0x4e7a0805 /* movec %%mmusr,%0 */"	\
			: "=d" (_tmp2)					\
			: "a" (_tmp1));					\
  _mmusr = _tmp2;							\
  if (0 /* XXX _mmusr & MMU_?_040 */)					\
    (valid) = 0;							\
  else									\
    {									\
      (valid) = 1;							\
      (paddr) = _mmusr & ~0xfff;					\
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
				".word 0xf478 /* cpusha %%dc */");
	  break;
	case FLUSH_CACHE_INSN:
	  __asm__ __volatile__ ("nop\n\t"
				".word 0xf4b8 /* cpusha %%ic */");
	  break;
	default:
	case FLUSH_CACHE_BOTH:
	  __asm__ __volatile__ ("nop\n\t"
				".word 0xf4f8 /* cpusha %%bc */");
	  break;
	}
      break;

    case FLUSH_SCOPE_LINE:
      len >>= 4;
      for (;;)
	{
	  virt_to_phys_040 (addr, paddr, valid);
	  if (valid)
	    break;
	  if (len <= PAGE_SIZE / 16)
	    return 0;
	  len -= PAGE_SIZE / 16;
	  addr += PAGE_SIZE;
	}
      while (len--)
	{
	  register unsigned long tmp __asm__ ("a0") = paddr;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ ("nop\n\t"
				    ".word 0xf468 /* cpushl %%dc,(%0) */"
				    : : "a" (tmp));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ ("nop\n\t"
				    ".word 0xf4a8 /* cpushl %%ic,(%0) */"
				    : : "a" (tmp));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ ("nop\n\t"
				    ".word 0xf4e8 /* cpushl %%bc,(%0) */"
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
		      len -= PAGE_SIZE / 16;
		      addr += PAGE_SIZE;
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
	  register unsigned long tmp __asm__ ("a0");
	  virt_to_phys_040 (addr, paddr, valid);
	  if (!valid)
	    continue;
	  tmp = paddr;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ ("nop\n\t"
				    ".word 0xf470 /* cpushp %%dc,(%0) */"
				    : : "a" (tmp));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ ("nop\n\t"
				    ".word 0xf4b0 /* cpushp %%ic,(%0) */"
				    : : "a" (tmp));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ ("nop\n\t"
				    ".word 0xf4f0 /* cpushp %%bc,(%0) */"
				    : : "a" (tmp));
	      break;
	    }
	}
      break;
    }
  return 0;
}

#define virt_to_phys_060(vaddr, paddr, valid)		\
{							\
  register unsigned long _tmp __asm__ ("a0") = (vaddr);	\
							\
  __asm__ __volatile__ (".word 0xf5c8 /* plpar (%1) */"	\
			: "=a" (_tmp)			\
			: "0" (_tmp));			\
  (valid) = 1; /* XXX */				\
  (paddr) = _tmp;					\
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
	  __asm__ __volatile__ (".word 0xf478 /* cpusha %%dc */\n\t"
				".word 0xf458 /* cinva %%dc */");
	  break;
	case FLUSH_CACHE_INSN:
	  __asm__ __volatile__ (".word 0xf4b8 /* cpusha %%ic */\n\t"
				".word 0xf498 /* cinva %%ic */");
	  break;
	default:
	case FLUSH_CACHE_BOTH:
	  __asm__ __volatile__ (".word 0xf4f8 /* cpusha %%bc */\n\t"
				".word 0xf4d8 /* cinva %%bc */");
	  break;
	}
      break;

    case FLUSH_SCOPE_LINE:
      len >>= 4;
      for (;;)
	{
	  virt_to_phys_060 (addr, paddr, valid);
	  if (valid)
	    break;
	  if (len <= PAGE_SIZE / 16)
	    return 0;
	  len -= PAGE_SIZE / 16;
	  addr += PAGE_SIZE;
	}
      while (len--)
	{
	  register unsigned long tmp __asm__ ("a0") = paddr;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ (".word 0xf468 /* cpushl %%dc,(%0) */\n\t"
				    ".word 0xf448 /* cinv %%dc,(%0) */"
				    : : "a" (tmp));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ (".word 0xf4a8 /* cpushl %%ic,(%0) */\n\t"
				    ".word 0xf488 /* cinv %%ic,(%0) */"
				    : : "a" (tmp));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ (".word 0xf4e8 /* cpushl %%bc,(%0) */\n\t"
				    ".word 0xf4c8 /* cinv %%bc,(%0) */"
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
		      len -= PAGE_SIZE / 16;
		      addr += PAGE_SIZE;
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
	  register unsigned long tmp __asm__ ("a0");
	  virt_to_phys_060 (addr, paddr, valid);
	  if (!valid)
	    continue;
	  tmp = paddr;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ (".word 0xf470 /* cpushp %%dc,(%0) */\n\t"
				    ".word 0xf450 /* cinv %%dc,(%0) */"
				    : : "a" (tmp));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ (".word 0xf4b0 /* cpushp %%ic,(%0) */\n\t"
				    ".word 0xf490 /* cinv %%ic,(%0) */"
				    : : "a" (tmp));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ (".word 0xf4f0 /* cpushp %%bc,(%0) */\n\t"
				    ".word 0xf4d0 /* cinv %%bc,(%0) */"
				    : : "a" (tmp));
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
      vma = find_vma (current, addr);
      if (vma == NULL || addr < vma->vm_start || addr + len > vma->vm_end)
	return -EINVAL;
    }

  switch (m68k_is040or060)
    {
    default: /* 030 */
      /* Always flush the whole cache, everything else would not be
	 worth the hassle.  */
      __asm__ __volatile__
	("movec %%cacr, %%d0\n\t"
	 "or %0, %%d0\n\t"
	 "movec %%d0, %%cacr"
	 : /* no outputs */
	 : "di" ((cache & FLUSH_CACHE_INSN ? 8 : 0)
		 | (cache & FLUSH_CACHE_DATA ? 0x800 : 0))
	 : "d0");
      return 0;

    case 4: /* 040 */
      return cache_flush_040 (addr, scope, cache, len);

    case 6: /* 060 */
      return cache_flush_060 (addr, scope, cache, len);
    }
}
