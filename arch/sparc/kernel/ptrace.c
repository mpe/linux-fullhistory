/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
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

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define MAGIC_CONSTANT 0x80000000

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static unsigned long get_long(struct task_struct * tsk,
	struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	unsigned long page, retval;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) >= max_mapnr)
		return 0;
	page += addr & ~PAGE_MASK;
	retval = *(unsigned long *) page;
	flush_page_to_ram(page);
	return retval;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct task_struct * tsk, struct vm_area_struct * vma,
	unsigned long addr, unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (!pgd_present(*pgdir)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	flush_cache_page(vma, addr);
	if (MAP_NR(page) < max_mapnr) {
		*(unsigned long *) (page + (addr & ~PAGE_MASK)) = data;
		flush_page_to_ram(page);
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

static int read_byte(struct task_struct *tsk, unsigned long addr,
		     unsigned char *result)
{
	struct vm_area_struct *vma = find_extend_vma(tsk, addr&~3);
	unsigned long tmp;

	if(!vma)
		return -EIO;
	tmp = get_long(tsk, vma, (addr & ~3));
	switch(addr & 3) {
	case 0:
		*result = (tmp & 0xff000000)>>24;
		break;
	case 1:
		*result = (tmp & 0x00ff0000)>>16;
		break;
	case 2:
		*result = (tmp & 0x0000ff00)>>8;
		break;
	case 3:
		*result = (tmp & 0x000000ff);
		break;
	}
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

static int write_byte(struct task_struct * tsk, unsigned long addr,
		      unsigned char data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, (addr & ~3));
	unsigned long tmp;

	if (!vma)
		return -EIO;
	tmp = get_long(tsk, vma, (addr & ~3));
	switch(addr & 3) {
	case 0:
		tmp &= 0x00ffffff;
		tmp |= (data << 24);
		break;
	case 1:
		tmp &= 0xff00ffff;
		tmp |= ((data << 16) & 0x00ff0000);
		break;
	case 2:
		tmp &= 0xffff00ff;
		tmp |= ((data << 8) & 0x0000ff00);
		break;
	case 3:
		tmp &= 0xffffff00;
		tmp |= (data & 0x000000ff);
		break;
	}
	put_long(tsk, vma, (addr & ~3), tmp);
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
	regs->psr |= PSR_C;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static inline void pt_succ_return(struct pt_regs *regs, unsigned long value)
{
	regs->u_regs[UREG_I0] = value;
	regs->psr &= ~PSR_C;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static void
pt_succ_return_linux(struct pt_regs *regs, unsigned long value, long *addr)
{
	if(put_user(value, addr))
		return pt_error_return(regs, EFAULT);
	regs->u_regs[UREG_I0] = 0;
	regs->psr &= ~PSR_C;
	regs->pc = regs->npc;
	regs->npc += 4;
}

static void
pt_os_succ_return (struct pt_regs *regs, unsigned long val, long *addr)
{
	if (current->personality & PER_BSD)
		pt_succ_return (regs, val);
	else
		pt_succ_return_linux (regs, val, addr);
}

/* Fuck me gently with a chainsaw... */
static inline void read_sunos_user(struct pt_regs *regs, unsigned long offset,
				   struct task_struct *tsk, long *addr)
{
	struct pt_regs *cregs = tsk->tss.kregs;
	struct thread_struct *t = &tsk->tss;
	int v;
	
	if(offset >= 1024)
		offset -= 1024; /* whee... */
	if(offset & ((sizeof(unsigned long) - 1))) {
		pt_error_return(regs, EIO);
		return;
	}
	if(offset >= 16 && offset < 784) {
		offset -= 16; offset >>= 2;
		pt_os_succ_return(regs, *(((unsigned long *)(&t->reg_window[0]))+offset), addr);
		return;
	}
	if(offset >= 784 && offset < 832) {
		offset -= 784; offset >>= 2;
		pt_os_succ_return(regs, *(((unsigned long *)(&t->rwbuf_stkptrs[0]))+offset), addr);
		return;
	}
	switch(offset) {
	case 0:
		v = t->ksp;
		break;
	case 4:
		v = t->kpc;
		break;
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
	if (current->personality & PER_BSD)
		pt_succ_return (regs, v);
	else
		pt_succ_return_linux (regs, v, addr);
	return;
}

static inline void write_sunos_user(struct pt_regs *regs, unsigned long offset,
				    struct task_struct *tsk)
{
	struct pt_regs *cregs = tsk->tss.kregs;
	struct thread_struct *t = &tsk->tss;
	unsigned long value = regs->u_regs[UREG_I3];

	if(offset >= 1024)
		offset -= 1024; /* whee... */
	if(offset & ((sizeof(unsigned long) - 1)))
		goto failure;
	if(offset >= 16 && offset < 784) {
		offset -= 16; offset >>= 2;
		*(((unsigned long *)(&t->reg_window[0]))+offset) = value;
		goto success;
	}
	if(offset >= 784 && offset < 832) {
		offset -= 784; offset >>= 2;
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
	unsigned long request = regs->u_regs[UREG_I0];
	unsigned long pid = regs->u_regs[UREG_I1];
	unsigned long addr = regs->u_regs[UREG_I2];
	unsigned long data = regs->u_regs[UREG_I3];
	unsigned long addr2 = regs->u_regs[UREG_I4];
	struct task_struct *child;

	lock_kernel();
#ifdef DEBUG_PTRACE
	{
		char *s;

		if ((request > 0) && (request < 21))
			s = pt_rq [request];
		else
			s = "unknown";

		if (request == PTRACE_POKEDATA && data == 0x91d02001){
			printk ("do_ptrace: breakpoint pid=%d, addr=%08lx addr2=%08lx\n",
				pid, addr, addr2);
		} else 
			printk("do_ptrace: rq=%s(%d) pid=%d addr=%08lx data=%08lx addr2=%08lx\n",
			       s, (int) request, (int) pid, addr, data, addr2);
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
	if(!(child = find_task_by_pid(pid))) {
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
	switch(request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int res;

		/* XXX Find out what is really going on. */
		flush_cache_all();

		/* Non-word alignment _not_ allowed on Sparc. */
		if(addr & (sizeof(unsigned long) - 1)) {
			pt_error_return(regs, EINVAL);
			goto out;
		}
		down(&child->mm->mmap_sem);
		res = read_long(child, addr, &tmp);
		up(&child->mm->mmap_sem);
		if (res < 0) {
			pt_error_return(regs, -res);
			goto out;
		}
		pt_os_succ_return(regs, tmp, (long *) data);
		goto out;
	}

	case PTRACE_PEEKUSR:
		read_sunos_user(regs, addr, child, (long *) data);
		goto out;

	case PTRACE_POKEUSR:
		write_sunos_user(regs, addr, child);
		goto out;

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA: {
		struct vm_area_struct *vma;
		int res;

		/* Non-word alignment _not_ allowed on Sparc. */
		if(addr & (sizeof(unsigned long) - 1)) {
			pt_error_return(regs, EINVAL);
			goto out;
		}
		down(&child->mm->mmap_sem);
		vma = find_extend_vma(child, addr);
		res = write_long(child, addr, data);
		up(&child->mm->mmap_sem);
		if(res < 0)
			pt_error_return(regs, -res);
		else
			pt_succ_return(regs, res);
		goto out;
	}

	case PTRACE_GETREGS: {
		struct pt_regs *pregs = (struct pt_regs *) addr;
		struct pt_regs *cregs = child->tss.kregs;
		int rval;

		rval = verify_area(VERIFY_WRITE, pregs, sizeof(struct pt_regs));
		if(rval) {
			pt_error_return(regs, -rval);
			goto out;
		}
		__put_user(cregs->psr, (&pregs->psr));
		__put_user(cregs->pc, (&pregs->pc));
		__put_user(cregs->npc, (&pregs->npc));
		__put_user(cregs->y, (&pregs->y));
		for(rval = 1; rval < 16; rval++)
			__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]));
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%x nPC=%x o7=%x\n", cregs->pc, cregs->npc, cregs->u_regs [15]);
#endif
		goto out;
	}

	case PTRACE_SETREGS: {
		struct pt_regs *pregs = (struct pt_regs *) addr;
		struct pt_regs *cregs = child->tss.kregs;
		unsigned long psr, pc, npc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		i = verify_area(VERIFY_READ, pregs, sizeof(struct pt_regs));
		if(i) {
			pt_error_return(regs, -i);
			goto out;
		}
		__get_user(psr, (&pregs->psr));
		__get_user(pc, (&pregs->pc));
		__get_user(npc, (&pregs->npc));
		__get_user(y, (&pregs->y));
		psr &= PSR_ICC;
		cregs->psr &= ~PSR_ICC;
		cregs->psr |= psr;
		if(!((pc | npc) & 3)) {
			cregs->pc = pc;
			cregs->npc =npc;
		}
		cregs->y = y;
		for(i = 1; i < 16; i++)
			__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]));
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_GETFPREGS: {
		struct fps {
			unsigned long regs[32];
			unsigned long fsr;
			unsigned long flags;
			unsigned long extra;
			unsigned long fpqd;
			struct fq {
				unsigned long *insnaddr;
				unsigned long insn;
			} fpq[16];
		} *fps = (struct fps *) addr;
		int i;

		i = verify_area(VERIFY_WRITE, fps, sizeof(struct fps));
		if(i) {
			pt_error_return(regs, -i);
			goto out;
		}
		for(i = 0; i < 32; i++)
			__put_user(child->tss.float_regs[i], (&fps->regs[i]));
		__put_user(child->tss.fsr, (&fps->fsr));
		__put_user(child->tss.fpqdepth, (&fps->fpqd));
		__put_user(0, (&fps->flags));
		__put_user(0, (&fps->extra));
		for(i = 0; i < 16; i++) {
			__put_user(child->tss.fpqueue[i].insn_addr,
				   (&fps->fpq[i].insnaddr));
			__put_user(child->tss.fpqueue[i].insn, (&fps->fpq[i].insn));
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETFPREGS: {
		struct fps {
			unsigned long regs[32];
			unsigned long fsr;
			unsigned long flags;
			unsigned long extra;
			unsigned long fpqd;
			struct fq {
				unsigned long *insnaddr;
				unsigned long insn;
			} fpq[16];
		} *fps = (struct fps *) addr;
		int i;

		i = verify_area(VERIFY_READ, fps, sizeof(struct fps));
		if(i) {
			pt_error_return(regs, -i);
			goto out;
		}
		copy_from_user(&child->tss.float_regs[0], &fps->regs[0], (32 * sizeof(unsigned long)));
		__get_user(child->tss.fsr, (&fps->fsr));
		__get_user(child->tss.fpqdepth, (&fps->fpqd));
		for(i = 0; i < 16; i++) {
			__get_user(child->tss.fpqueue[i].insn_addr,
				   (&fps->fpq[i].insnaddr));
			__get_user(child->tss.fpqueue[i].insn, (&fps->fpq[i].insn));
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_READTEXT:
	case PTRACE_READDATA: {
		unsigned char *dest = (unsigned char *) addr2;
		unsigned long src = addr;
		unsigned char tmp;
		int res, len = data;

		res = verify_area(VERIFY_WRITE, dest, len);
		if(res) {
			pt_error_return(regs, -res);
			goto out;
		}
		while(len) {
			down(&child->mm->mmap_sem);
			res = read_byte(child, src, &tmp);
			up(&child->mm->mmap_sem);
			if(res < 0) {
				pt_error_return(regs, -res);
				goto out;
			}
			__put_user(tmp, dest);
			src++; dest++; len--;
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA: {
		unsigned char *src = (unsigned char *) addr2;
		unsigned long dest = addr;
		int res, len = data;

		res = verify_area(VERIFY_READ, src, len);
		if(res) {
			pt_error_return(regs, -res);
			goto out;
		}
		while(len) {
			unsigned long tmp;

			__get_user(tmp, src);
			down(&child->mm->mmap_sem);
			res = write_byte(child, dest, tmp);
			up(&child->mm->mmap_sem);
			if(res < 0) {
				pt_error_return(regs, -res);
				goto out;
			}
			src++; dest++; len--;
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SYSCALL: /* continue and stop at (return from) syscall */
		addr = 1;

	case PTRACE_CONT: { /* restart after signal. */
		if ((unsigned long) data > _NSIG) {
			pt_error_return(regs, EIO);
			goto out;
		}
		if (addr != 1) {
			if (addr & 3) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
#ifdef DEBUG_PTRACE
			printk ("Original: %08lx %08lx\n", child->tss.kregs->pc, child->tss.kregs->npc);
			printk ("Continuing with %08lx %08lx\n", addr, addr+4);
#endif
			child->tss.kregs->pc = addr;
			child->tss.kregs->npc = addr + 4;
		}

		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;

		child->exit_code = data;
#ifdef DEBUG_PTRACE
		printk("CONT: %s [%d]: set exit_code = %x %x %x\n", child->comm,
			child->pid, child->exit_code,
			child->tss.kregs->pc,
			child->tss.kregs->npc);
		       
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
		wake_up_process(child);
		child->exit_code = SIGKILL;
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
		wake_up_process(child);
		child->exit_code = data;
		write_lock_irqsave(&tasklist_lock, flags);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irqrestore(&tasklist_lock, flags);
		pt_succ_return(regs, 0);
		goto out;
	}

	/* PTRACE_DUMPCORE unsupported... */

	default:
		pt_error_return(regs, EIO);
		goto out;
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
