/*
 *  linux/arch/ppc/kernel/ptrace.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@cs.nmt.edu) 
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#define MSR_DEBUGCHANGE	(MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1)

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	if (regno < sizeof(struct pt_regs) / sizeof(unsigned long))
		return ((unsigned long *)task->tss.regs)[regno];
	return (0);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	if (regno <= PT_MQ) {
		if (regno == PT_MSR)
			data = (data & MSR_DEBUGCHANGE)
				| (task->tss.regs->msr & ~MSR_DEBUGCHANGE);
		((unsigned long *)task->tss.regs)[regno] = data;
		return 0;
	}
	return -1;
}

static inline void
set_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->tss.regs;
	regs->msr |= MSR_SE;
}

static inline void
clear_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->tss.regs;
	regs->msr &= ~MSR_SE;
}

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 */
static unsigned long get_long(struct task_struct * tsk, 
	struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace[1]: bad page directory %lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir,addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace[3]: bad pmd %lx\n", pmd_val(*pgmiddle));
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
	return *(unsigned long *) page;
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
		printk("ptrace[2]: bad page directory %lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir,addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace[4]: bad pmd %lx\n", pmd_val(*pgmiddle));
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
	if (MAP_NR(page) < max_mapnr) {
		unsigned long phys_addr = page + (addr & ~PAGE_MASK);
		*(unsigned long *) phys_addr = data;
		flush_icache_range(phys_addr, phys_addr+4);
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb_all();
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
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
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_low = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_low = vma->vm_next;
			if (!vma_low || vma_low->vm_start != vma->vm_end)
				return -EIO;
		}
		high = get_long(tsk, vma,addr & ~(sizeof(long)-1));
		low = get_long(tsk, vma_low,(addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 3:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 1:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	} else
		*result = get_long(tsk, vma,addr);
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
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_low = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_low = vma->vm_next;
			if (!vma_low || vma_low->vm_start != vma->vm_end)
				return -EIO;
		}
		high = get_long(tsk, vma,addr & ~(sizeof(long)-1));
		low = get_long(tsk, vma_low,(addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				high = data;
				break;
			case 3:
				low &= 0x000000ff;
				low |= data << 8;
				high &= ~0xff;
				high |= data >> 24;
				break;
			case 2:
				low &= 0x0000ffff;
				low |= data << 16;
				high &= ~0xffff;
				high |= data >> 16;
				break;
			case 1:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(tsk, vma,addr & ~(sizeof(long)-1),high);
		put_long(tsk, vma_low,(addr+sizeof(long)) & ~(sizeof(long)-1),low);
	} else
		put_long(tsk, vma,addr,data);
	return 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret = -EPERM;

	lock_kernel();
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}
	if (pid == 1)		/* you may not mess with init */
		goto out;
	ret = -ESRCH;
	if (!(child = find_task_by_pid(pid)))
		goto out;
	ret = -EPERM;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child->flags & PF_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	switch (request) {
	/* If I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			down(&child->mm->mmap_sem);
			ret = read_long(child, addr, &tmp);
			up(&child->mm->mmap_sem);
			if (ret < 0)
				goto out;
			ret = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (!ret)
				put_user(tmp, (unsigned long *) data);
			goto out;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			
			if ((addr & 3) || addr < 0 || addr > (PT_FPSCR << 2)) {
				ret = -EIO;
				goto out;
			}
			
			ret = verify_area(VERIFY_WRITE, (void *) data,
					  sizeof(long));
			if (ret)
				goto out;
			tmp = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */
			if (addr < PT_FPR0) {
				tmp = get_reg(child, addr);
			}
			else if (addr >= PT_FPR0 && addr <= PT_FPSCR) {
#ifdef __SMP__
				if (child->tss.regs->msr & MSR_FP )
					smp_giveup_fpu(child);
#else			  
			  /* only current can be last task to use math on SMP */
				if (last_task_used_math == child)
					giveup_fpu();
#endif				
				tmp = ((long *)child->tss.fpr)[addr - PT_FPR0];
			}
			else
				ret = -EIO;
			if (!ret)
				put_user(tmp,(unsigned long *) data);
			goto out;
		}

      /* If I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			down(&child->mm->mmap_sem);
			ret = write_long(child,addr,data);
			up(&child->mm->mmap_sem);
			goto out;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= ((PT_FPR0 + 64) << 2))
				goto out;

			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PT_ORIG_R3)
				goto out;
			if (addr < PT_FPR0) {
				if (put_reg(child, addr, data))
					goto out;
				ret = 0;
				goto out;
			}
			if (addr >= PT_FPR0 && addr < PT_FPR0 + 64) {
#ifndef __SMP__
				if (last_task_used_math == child)
					giveup_fpu();
#else	
				if (child->tss.regs->msr & MSR_FP )
					smp_giveup_fpu(child);
#endif				
				((long *)child->tss.fpr)[addr - PT_FPR0] = data;
				ret = 0;
				goto out;
			}
			goto out;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			ret = -EIO;
			if ((unsigned long) data >= _NSIG)
				goto out;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			wake_up_process(child);
			/* make sure the single step bit is not set. */
			clear_single_step(child);
			ret = 0;
			goto out;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			ret = 0;
			if (child->state == TASK_ZOMBIE) /* already dead */
				goto out;
			wake_up_process(child);
			child->exit_code = SIGKILL;
			/* make sure the single step bit is not set. */
			clear_single_step(child);
			goto out;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			ret = -EIO;
			if ((unsigned long) data >= _NSIG)
				goto out;
			child->flags &= ~PF_TRACESYS;
			set_single_step(child);
			wake_up_process(child);
			child->exit_code = data;
			/* give it a chance to run. */
			ret = 0;
			goto out;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			ret = -EIO;
			if ((unsigned long) data >= _NSIG)
				goto out;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			wake_up_process(child);
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure the single step bit is not set. */
			clear_single_step(child);
			ret = 0;
			goto out;
		}

		default:
			ret = -EIO;
			goto out;
	}
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	lock_kernel();
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		goto out;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
out:
	unlock_kernel();
}
