/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Based upon code written by Ross Biro, Linus Torvalds, Bob Manson,
 * and David Mosberger.
 *
 * Added Linux support -miguel (wierd, eh?, the orignal code was meant
 * to emulate SunOS).
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/asi.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/psrcompat.h>
#include <asm/visasm.h>

#define MAGIC_CONSTANT 0x80000000

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static pte_t *get_page(struct task_struct * tsk,
	struct vm_area_struct * vma, unsigned long addr, int write)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);

	/* Seems non-intuitive but the page copy/clear routines always
	 * check current's value.
	 */
	current->mm->segments = (void *) (addr & PAGE_SIZE);

	if (pgd_none(*pgdir)) {
		handle_mm_fault(tsk, vma, addr, write);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %016lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, write);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %016lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, write);
		goto repeat;
	}
	if (write && !pte_write(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, write);
		goto repeat;
	}
	return pgtable;
}

/* We must bypass the L1-cache to avoid alias issues.  -DaveM */
static __inline__ unsigned long read_user_long(unsigned long kvaddr)
{
	unsigned long ret;

	__asm__ __volatile__("ldxa [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (__pa(kvaddr)), "i" (ASI_PHYS_USE_EC));
	return ret;
}

static __inline__ unsigned int read_user_int(unsigned long kvaddr)
{
	unsigned int ret;

	__asm__ __volatile__("lduwa [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (__pa(kvaddr)), "i" (ASI_PHYS_USE_EC));
	return ret;
}

static __inline__ void write_user_long(unsigned long kvaddr, unsigned long val)
{
	__asm__ __volatile__("stxa %0, [%1] %2"
			     : /* no outputs */
			     : "r" (val), "r" (__pa(kvaddr)), "i" (ASI_PHYS_USE_EC));
}

static __inline__ void write_user_int(unsigned long kvaddr, unsigned int val)
{
	__asm__ __volatile__("stwa %0, [%1] %2"
			     : /* no outputs */
			     : "r" (val), "r" (__pa(kvaddr)), "i" (ASI_PHYS_USE_EC));
}

static inline unsigned long get_long(struct task_struct * tsk,
	struct vm_area_struct * vma, unsigned long addr)
{
	pte_t * pgtable;
	unsigned long page, retval;
	
	if (!(pgtable = get_page (tsk, vma, addr, 0))) return 0;
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) >= max_mapnr)
		return 0;
	page += addr & ~PAGE_MASK;
	retval = read_user_long(page);
	flush_page_to_ram(page);
	return retval;
}

static inline void put_long(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long addr, unsigned long data)
{
	pte_t *pgtable;
	unsigned long page;

	if (!(pgtable = get_page (tsk, vma, addr, 1))) return;
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	flush_cache_page(vma, addr);
	if (MAP_NR(page) < max_mapnr) {
		unsigned long pgaddr;

		pgaddr = page + (addr & ~PAGE_MASK);
		write_user_long(pgaddr, data);

		__asm__ __volatile__("
		membar	#StoreStore
		flush	%0
"		: : "r" (pgaddr & ~7) : "memory");
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb_page(vma, addr);
}

static inline unsigned int get_int(struct task_struct * tsk,
	struct vm_area_struct * vma, unsigned long addr)
{
	pte_t * pgtable;
	unsigned long page;
	unsigned int retval;
	
	if (!(pgtable = get_page (tsk, vma, addr, 0))) return 0;
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) >= max_mapnr)
		return 0;
	page += addr & ~PAGE_MASK;
	retval = read_user_int(page);
	flush_page_to_ram(page);
	return retval;
}

static inline void put_int(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long addr, unsigned int data)
{
	pte_t *pgtable;
	unsigned long page;

	if (!(pgtable = get_page (tsk, vma, addr, 1))) return;
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	flush_cache_page(vma, addr);
	if (MAP_NR(page) < max_mapnr) {
		unsigned long pgaddr;

		pgaddr = page + (addr & ~PAGE_MASK);
		write_user_int(pgaddr, data);

		__asm__ __volatile__("
		membar	#StoreStore
		flush	%0
"		: : "r" (pgaddr & ~7) : "memory");
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb_page(vma, addr);
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk,
					       unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk->mm,addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_end - addr > tsk->rlim[RLIMIT_STACK].rlim_cur)
		return NULL;
	vma->vm_offset -= vma->vm_start - addr;
	vma->vm_start = addr;
	return vma;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
		     unsigned long * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	*result = get_long(tsk, vma, addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_int() to read a int.
 */
static int read_int(struct task_struct * tsk, unsigned long addr,
		     unsigned int * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	*result = get_int(tsk, vma, addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
		      unsigned long data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	put_long(tsk, vma, addr, data);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_int() to write a int.
 */
static int write_int(struct task_struct * tsk, unsigned long addr,
		     unsigned int data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	put_int(tsk, vma, addr, data);
	return 0;
}

/* Returning from ptrace is a bit tricky because the syscall return
 * low level code assumes any value returned which is negative and
 * is a valid errno will mean setting the condition codes to indicate
 * an error return.  This doesn't work, so we have this hook.
 */
static inline void pt_error_return(struct pt_regs *regs, unsigned long error)
{
	regs->u_regs[UREG_I0] = error;
	regs->tstate |= (TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static inline void pt_succ_return(struct pt_regs *regs, unsigned long value)
{
	regs->u_regs[UREG_I0] = value;
	regs->tstate &= ~(TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static inline void
pt_succ_return_linux(struct pt_regs *regs, unsigned long value, long *addr)
{
	if (current->tss.flags & SPARC_FLAG_32BIT) {
		if(put_user(value, (unsigned int *)addr))
			return pt_error_return(regs, EFAULT);
	} else {
		if(put_user(value, addr))
			return pt_error_return(regs, EFAULT);
	}
	regs->u_regs[UREG_I0] = 0;
	regs->tstate &= ~(TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static void
pt_os_succ_return (struct pt_regs *regs, unsigned long val, long *addr)
{
	if (current->personality & PER_BSD)
		pt_succ_return (regs, val);
	else
		pt_succ_return_linux (regs, val, addr);
}

#if 0
/* XXX: Implement this some day */
/* Fuck me gently with a chainsaw... */
static inline void read_sunos_user(struct pt_regs *regs, unsigned long offset,
				   struct task_struct *tsk, long *addr)
{
	struct pt_regs *cregs = tsk->tss.kregs;
	struct thread_struct *t = &tsk->tss;
	int v;
	
	if(offset >= 1024)
		offset -= 1024; /* whee... */
	if(offset & ((sizeof(unsigned int) - 1))) {
		pt_error_return(regs, EIO);
		return;
	}
	if(offset >= 16 && offset < 784) {
		offset -= 16; offset >>= 2;
		if (t->w_saved)
			pt_os_succ_return(regs, *(((unsigned long *)(&t->reg_window[0]))+offset), addr);
		return;
	}
	if(offset >= 784 && offset < 832) {
		offset -= 784; offset >>= 2;
		if (t->w_saved)
			pt_os_succ_return(regs, *(((unsigned long *)(&t->rwbuf_stkptrs[0]))+offset), addr);
		return;
	}
	switch(offset) {
	case 0:
		v = t->ksp;
		break;
#if 0
	case 4:
		v = t->kpc;
		break;
#endif
	case 8:
		v = t->kpsr;
		break;
	case 12:
		v = t->uwinmask;
		break;
	case 832:
		v = t->w_saved;
		break;
	case 896:
		v = cregs->u_regs[UREG_I0];
		break;
	case 900:
		v = cregs->u_regs[UREG_I1];
		break;
	case 904:
		v = cregs->u_regs[UREG_I2];
		break;
	case 908:
		v = cregs->u_regs[UREG_I3];
		break;
	case 912:
		v = cregs->u_regs[UREG_I4];
		break;
	case 916:
		v = cregs->u_regs[UREG_I5];
		break;
	case 920:
		v = cregs->u_regs[UREG_I6];
		break;
	case 924:
		if(tsk->tss.flags & MAGIC_CONSTANT)
			v = cregs->u_regs[UREG_G1];
		else
			v = 0;
		break;
	case 940:
		v = cregs->u_regs[UREG_I0];
		break;
	case 944:
		v = cregs->u_regs[UREG_I1];
		break;

	case 948:
		/* Isn't binary compatibility _fun_??? */
		if(cregs->psr & PSR_C)
			v = cregs->u_regs[UREG_I0] << 24;
		else
			v = 0;
		break;

		/* Rest of them are completely unsupported. */
	default:
		printk("%s [%d]: Wants to read user offset %ld\n",
		       current->comm, current->pid, offset);
		pt_error_return(regs, EIO);
		return;
	}
	pt_os_succ_return_linux (regs, v, addr);
	return;
}

static inline void write_sunos_user(struct pt_regs *regs, unsigned long offset,
				    struct task_struct *tsk)
{
	struct pt_regs *cregs = tsk->tss.kregs;
	struct thread_struct *t = &tsk->tss;
	unsigned int value = regs->u_regs[UREG_I3];

	if(offset >= 1024)
		offset -= 1024; /* whee... */
	if(offset & ((sizeof(unsigned long) - 1)))
		goto failure;
	if(offset >= 16 && offset < 784) {
		offset -= 16; offset >>= 2;
		if (t->w_saved)
			*(((unsigned long *)(&t->reg_window[0]))+offset) = value;
		goto success;
	}
	if(offset >= 784 && offset < 832) {
		offset -= 784; offset >>= 2;
		if (t->w_saved)
			*(((unsigned long *)(&t->rwbuf_stkptrs[0]))+offset) = value;
		goto success;
	}
	switch(offset) {
	case 896:
		cregs->u_regs[UREG_I0] = value;
		break;
	case 900:
		cregs->u_regs[UREG_I1] = value;
		break;
	case 904:
		cregs->u_regs[UREG_I2] = value;
		break;
	case 908:
		cregs->u_regs[UREG_I3] = value;
		break;
	case 912:
		cregs->u_regs[UREG_I4] = value;
		break;
	case 916:
		cregs->u_regs[UREG_I5] = value;
		break;
	case 920:
		cregs->u_regs[UREG_I6] = value;
		break;
	case 924:
		cregs->u_regs[UREG_I7] = value;
		break;
	case 940:
		cregs->u_regs[UREG_I0] = value;
		break;
	case 944:
		cregs->u_regs[UREG_I1] = value;
		break;

		/* Rest of them are completely unsupported or "no-touch". */
	default:
		printk("%s [%d]: Wants to write user offset %ld\n",
		       current->comm, current->pid, offset);
		goto failure;
	}
success:
	pt_succ_return(regs, 0);
	return;
failure:
	pt_error_return(regs, EIO);
	return;
}
#endif

/* #define ALLOW_INIT_TRACING */
/* #define DEBUG_PTRACE */

#ifdef DEBUG_PTRACE
char *pt_rq [] = {
"TRACEME",
"PEEKTEXT",
"PEEKDATA",
"PEEKUSR",
"POKETEXT",
"POKEDATA",
"POKEUSR",
"CONT",
"KILL",
"SINGLESTEP",
"SUNATTACH",
"SUNDETACH",
"GETREGS",
"SETREGS",
"GETFPREGS",
"SETFPREGS",
"READDATA",
"WRITEDATA",
"READTEXT",
"WRITETEXT",
"GETFPAREGS",
"SETFPAREGS",
""
};
#endif

asmlinkage void do_ptrace(struct pt_regs *regs)
{
	int request = regs->u_regs[UREG_I0];
	pid_t pid = regs->u_regs[UREG_I1];
	unsigned long addr = regs->u_regs[UREG_I2];
	unsigned long data = regs->u_regs[UREG_I3];
	unsigned long addr2 = regs->u_regs[UREG_I4];
	struct task_struct *child;

	if (current->tss.flags & SPARC_FLAG_32BIT) {
		addr &= 0xffffffffUL;
		data &= 0xffffffffUL;
		addr2 &= 0xffffffffUL;
	}
	lock_kernel();
#ifdef DEBUG_PTRACE
	{
		char *s;

		if ((request > 0) && (request < 21))
			s = pt_rq [request];
		else
			s = "unknown";

		if (request == PTRACE_POKEDATA && data == 0x91d02001){
			printk ("do_ptrace: breakpoint pid=%d, addr=%016lx addr2=%016lx\n",
				pid, addr, addr2);
		} else 
			printk("do_ptrace: rq=%s(%d) pid=%d addr=%016lx data=%016lx addr2=%016lx\n",
			       s, request, pid, addr, data, addr2);
	}
#endif
	if(request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		pt_succ_return(regs, 0);
		goto out;
	}
#ifndef ALLOW_INIT_TRACING
	if(pid == 1) {
		/* Can't dork with init. */
		pt_error_return(regs, EPERM);
		goto out;
	}
#endif
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);

	if(!child) {
		pt_error_return(regs, ESRCH);
		goto out;
	}

	if (((current->personality & PER_BSD) && (request == PTRACE_SUNATTACH))
	    || (!(current->personality & PER_BSD) && (request == PTRACE_ATTACH))) {
		if(child == current) {
			/* Try this under SunOS/Solaris, bwa haha
			 * You'll never be able to kill the process. ;-)
			 */
			pt_error_return(regs, EPERM);
			goto out;
		}
		if((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->uid) ||
		    (current->gid != child->egid) ||
		    (current->gid != child->gid)) && 
		   !capable(CAP_SYS_PTRACE)) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		child->flags |= PF_PTRACED;
		if(child->p_pptr != current) {
			unsigned long flags;

			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
		}
		send_sig(SIGSTOP, child, 1);
		pt_succ_return(regs, 0);
		goto out;
	}
	if (!(child->flags & PF_PTRACED)
	    && ((current->personality & PER_BSD) && (request != PTRACE_SUNATTACH))
	    && (!(current->personality & PER_BSD) && (request != PTRACE_ATTACH))) {
		pt_error_return(regs, ESRCH);
		goto out;
	}
	if(child->state != TASK_STOPPED) {
		if(request != PTRACE_KILL) {
			pt_error_return(regs, ESRCH);
			goto out;
		}
	}
	if(child->p_pptr != current) {
		pt_error_return(regs, ESRCH);
		goto out;
	}

	if(!(child->tss.flags & SPARC_FLAG_32BIT)	&&
	   ((request == PTRACE_READDATA64)		||
	    (request == PTRACE_WRITEDATA64)		||
	    (request == PTRACE_READTEXT64)		||
	    (request == PTRACE_WRITETEXT64)		||
	    (request == PTRACE_PEEKTEXT64)		||
	    (request == PTRACE_POKETEXT64)		||
	    (request == PTRACE_PEEKDATA64)		||
	    (request == PTRACE_POKEDATA64))) {
		addr = regs->u_regs[UREG_G2];
		addr2 = regs->u_regs[UREG_G3];
		request -= 30; /* wheee... */
	}

	switch(request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int res;

		/* Non-word alignment _not_ allowed on Sparc. */
		if (current->tss.flags & SPARC_FLAG_32BIT) {
			unsigned int x;
			if(addr & (sizeof(unsigned int) - 1)) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
			res = read_int(child, addr, &x);
			tmp = x;
		} else {
			if(addr & (sizeof(unsigned long) - 1)) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
			res = read_long(child, addr, &tmp);
		}
		if (res < 0) {
			pt_error_return(regs, -res);
			goto out;
		}
		pt_os_succ_return(regs, tmp, (long *) data);
		goto out;
	}

	case PTRACE_PEEKUSR:
#if 0	
		read_sunos_user(regs, addr, child, (long *) data);
#endif
		goto out;

	case PTRACE_POKEUSR:
#if 0	
		write_sunos_user(regs, addr, child);
#endif		
		goto out;

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA: {
		int res;

		/* Non-word alignment _not_ allowed on Sparc. */
		if (current->tss.flags & SPARC_FLAG_32BIT) {
			if(addr & (sizeof(unsigned int) - 1)) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
			res = write_int(child, addr, data);
		} else {
			if(addr & (sizeof(unsigned long) - 1)) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
			res = write_long(child, addr, data);
		}
		if(res < 0)
			pt_error_return(regs, -res);
		else
			pt_succ_return(regs, res);
		goto out;
	}

	case PTRACE_GETREGS: {
		struct pt_regs32 *pregs = (struct pt_regs32 *) addr;
		struct pt_regs *cregs = child->tss.kregs;
		int rval;

		if (__put_user(tstate_to_psr(cregs->tstate), (&pregs->psr)) ||
		    __put_user(cregs->tpc, (&pregs->pc)) ||
		    __put_user(cregs->tnpc, (&pregs->npc)) ||
		    __put_user(cregs->y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		for(rval = 1; rval < 16; rval++)
			if (__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%lx nPC=%lx o7=%lx\n", cregs->tpc, cregs->tnpc, cregs->u_regs [15]);
#endif
		goto out;
	}

	case PTRACE_GETREGS64: {
		struct pt_regs *pregs = (struct pt_regs *) addr;
		struct pt_regs *cregs = child->tss.kregs;
		int rval;

		if (__put_user(cregs->tstate, (&pregs->tstate)) ||
		    __put_user(cregs->tpc, (&pregs->tpc)) ||
		    __put_user(cregs->tnpc, (&pregs->tnpc)) ||
		    __put_user(cregs->y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		for(rval = 1; rval < 16; rval++)
			if (__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%lx nPC=%lx o7=%lx\n", cregs->tpc, cregs->tnpc, cregs->u_regs [15]);
#endif
		goto out;
	}

	case PTRACE_SETREGS: {
		struct pt_regs32 *pregs = (struct pt_regs32 *) addr;
		struct pt_regs *cregs = child->tss.kregs;
		unsigned int psr, pc, npc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (__get_user(psr, (&pregs->psr)) ||
		    __get_user(pc, (&pregs->pc)) ||
		    __get_user(npc, (&pregs->npc)) ||
		    __get_user(y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		cregs->tstate &= ~(TSTATE_ICC);
		cregs->tstate |= psr_to_tstate_icc(psr);
               	if(!((pc | npc) & 3)) {
			cregs->tpc = pc;
			cregs->tnpc = npc;
		}
		cregs->y = y;
		for(i = 1; i < 16; i++) {
			if (__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETREGS64: {
		struct pt_regs *pregs = (struct pt_regs *) addr;
		struct pt_regs *cregs = child->tss.kregs;
		unsigned long tstate, tpc, tnpc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (__get_user(tstate, (&pregs->tstate)) ||
		    __get_user(tpc, (&pregs->tpc)) ||
		    __get_user(tnpc, (&pregs->tnpc)) ||
		    __get_user(y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		tstate &= (TSTATE_ICC | TSTATE_XCC);
		cregs->tstate &= ~(TSTATE_ICC | TSTATE_XCC);
		cregs->tstate |= tstate;
		if(!((tpc | tnpc) & 3)) {
			cregs->tpc = tpc;
			cregs->tnpc = tnpc;
		}
		cregs->y = y;
		for(i = 1; i < 16; i++) {
			if (__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_GETFPREGS: {
		struct fps {
			unsigned int regs[32];
			unsigned int fsr;
			unsigned int flags;
			unsigned int extra;
			unsigned int fpqd;
			struct fq {
				unsigned int insnaddr;
				unsigned int insn;
			} fpq[16];
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);

		if (copy_to_user(&fps->regs[0], fpregs,
				 (32 * sizeof(unsigned int))) ||
		    __put_user(child->tss.xfsr[0], (&fps->fsr)) ||
		    __put_user(0, (&fps->fpqd)) ||
		    __put_user(0, (&fps->flags)) ||
		    __put_user(0, (&fps->extra)) ||
		    clear_user(&fps->fpq[0], 32 * sizeof(unsigned int))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_GETFPREGS64: {
		struct fps {
			unsigned int regs[64];
			unsigned long fsr;
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);

		if (copy_to_user(&fps->regs[0], fpregs,
				 (64 * sizeof(unsigned int))) ||
		    __put_user(child->tss.xfsr[0], (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETFPREGS: {
		struct fps {
			unsigned int regs[32];
			unsigned int fsr;
			unsigned int flags;
			unsigned int extra;
			unsigned int fpqd;
			struct fq {
				unsigned int insnaddr;
				unsigned int insn;
			} fpq[16];
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);
		unsigned fsr;

		if (copy_from_user(fpregs, &fps->regs[0],
				   (32 * sizeof(unsigned int))) ||
		    __get_user(fsr, (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		child->tss.xfsr[0] &= 0xffffffff00000000UL;
		child->tss.xfsr[0] |= fsr;
		if (!(child->tss.fpsaved[0] & FPRS_FEF))
			child->tss.gsr[0] = 0;
		child->tss.fpsaved[0] |= (FPRS_FEF | FPRS_DL);
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETFPREGS64: {
		struct fps {
			unsigned int regs[64];
			unsigned long fsr;
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);

		if (copy_from_user(fpregs, &fps->regs[0],
				   (64 * sizeof(unsigned int))) ||
		    __get_user(child->tss.xfsr[0], (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		if (!(child->tss.fpsaved[0] & FPRS_FEF))
			child->tss.gsr[0] = 0;
		child->tss.fpsaved[0] |= (FPRS_FEF | FPRS_DL | FPRS_DU);
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_READTEXT:
	case PTRACE_READDATA: {
		unsigned char *dest = (unsigned char *) addr2;
		unsigned long src = addr;
		int len = data, curlen;
		struct vm_area_struct *vma;
		pte_t *pgtable;
		unsigned long page;

		while(len) {
			vma = find_extend_vma(child, src);
			if (!vma) {
				pt_error_return(regs, EIO);
				goto flush_and_out;
			}
			pgtable = get_page (child, vma, src, 0);
			if (src & ~PAGE_MASK) {
				curlen = PAGE_SIZE - (src & ~PAGE_MASK);
				if (curlen > len) curlen = len;
			} else if (len > PAGE_SIZE)
				curlen = PAGE_SIZE;
			else
				curlen = len;
			if (pgtable && MAP_NR(page = pte_page(*pgtable)) < max_mapnr) {
				if (copy_to_user (dest, ((char *)page) + (src & ~PAGE_MASK), curlen)) {
					flush_page_to_ram(page);
					pt_error_return(regs, EFAULT);
					goto flush_and_out;
				}
				flush_page_to_ram(page);
			} else {
				if (clear_user (dest, curlen)) {
					pt_error_return(regs, EFAULT);
					goto flush_and_out;
				}
			}
			src += curlen;
			dest += curlen;
			len -= curlen;
		}
		pt_succ_return(regs, 0);
		goto flush_and_out;
	}

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA: {
		unsigned char *src = (unsigned char *) addr2;
		unsigned long dest = addr;
		int len = data, curlen;
		struct vm_area_struct *vma;
		pte_t *pgtable;
		unsigned long page;

		while(len) {
			vma = find_extend_vma(child, dest);
			if (!vma) {
				pt_error_return(regs, EIO);
				goto flush_and_out;
			}
			pgtable = get_page (child, vma, dest, 1);
			if (dest & ~PAGE_MASK) {
				curlen = PAGE_SIZE - (dest & ~PAGE_MASK);
				if (curlen > len) curlen = len;
			} else if (len > PAGE_SIZE)
				curlen = PAGE_SIZE;
			else
				curlen = len;
			if (pgtable && MAP_NR(page = pte_page(*pgtable)) < max_mapnr) {
				flush_cache_page(vma, dest);
				if (copy_from_user (((char *)page) + (dest & ~PAGE_MASK), src, curlen)) {
					flush_page_to_ram(page);
					set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
					flush_tlb_page(vma, dest);
					pt_error_return(regs, EFAULT);
					goto flush_and_out;
				}
				flush_page_to_ram(page);
				set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
				flush_tlb_page(vma, dest);
			}
			src += curlen;
			dest += curlen;
			len -= curlen;
		}
		pt_succ_return(regs, 0);
		goto flush_and_out;
	}

	case PTRACE_SYSCALL: /* continue and stop at (return from) syscall */
		addr = 1;

	case PTRACE_CONT: { /* restart after signal. */
		if (data > _NSIG) {
			pt_error_return(regs, EIO);
			goto out;
		}
		if (addr != 1) {
			if (addr & 3) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
#ifdef DEBUG_PTRACE
			printk ("Original: %016lx %016lx\n", child->tss.kregs->tpc, child->tss.kregs->tnpc);
			printk ("Continuing with %016lx %016lx\n", addr, addr+4);
#endif
			child->tss.kregs->tpc = addr;
			child->tss.kregs->tnpc = addr + 4;
		}

		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;

		child->exit_code = data;
#ifdef DEBUG_PTRACE
		printk("CONT: %s [%d]: set exit_code = %x %lx %lx\n", child->comm,
			child->pid, child->exit_code,
			child->tss.kregs->tpc,
			child->tss.kregs->tnpc);
		       
#endif
		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
	case PTRACE_KILL: {
		if (child->state == TASK_ZOMBIE) {	/* already dead */
			pt_succ_return(regs, 0);
			goto out;
		}
		child->exit_code = SIGKILL;
		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SUNDETACH: { /* detach a process that was attached. */
		unsigned long flags;

		if ((unsigned long) data > _NSIG) {
			pt_error_return(regs, EIO);
			goto out;
		}
		child->flags &= ~(PF_PTRACED|PF_TRACESYS);
		child->exit_code = data;

		write_lock_irqsave(&tasklist_lock, flags);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irqrestore(&tasklist_lock, flags);

		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out;
	}

	/* PTRACE_DUMPCORE unsupported... */

	default:
		pt_error_return(regs, EIO);
		goto out;
	}
flush_and_out:
	{
		unsigned long va;
		for(va =  0; va < (PAGE_SIZE << 1); va += 32)
			spitfire_put_dcache_tag(va, 0x0);
	}
out:
	unlock_kernel();
}

asmlinkage void syscall_trace(void)
{
#ifdef DEBUG_PTRACE
	printk("%s [%d]: syscall_trace\n", current->comm, current->pid);
#endif
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	current->tss.flags ^= MAGIC_CONSTANT;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
#ifdef DEBUG_PTRACE
	printk("%s [%d]: syscall_trace exit= %x\n", current->comm,
		current->pid, current->exit_code);
#endif
	if (current->exit_code) {
		send_sig (current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
