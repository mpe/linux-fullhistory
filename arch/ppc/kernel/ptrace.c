/*
 *  linux/arch/ppc/kernel/ptrace.c
 *
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Adapted from 'linux/arch/m68k/kernel/ptrace.c'
 * PowerPC version by Gary Thomas (gdt@linuxppc.org)
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
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* Find the stack offset for a register, relative to tss.ksp. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static int regoff[] = {
};

/* change a pid into a task struct. */
static inline struct task_struct * get_task(int pid)
{
	int i;

	for (i = 1; i < NR_TASKS; i++) {
		if (task[i] != NULL && (task[i]->pid == pid))
			return task[i];
	}
	return NULL;
}

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	struct pt_regs *regs = task->tss.regs;
	if (regno <= PT_R31)
	{
		return (regs->gpr[regno]);
	} else
	if (regno == PT_NIP)
	{
		return (regs->nip);
	} else
	if (regno == PT_MSR)
	{
		return (regs->msr);
	} else
	if (regno == PT_ORIG_R3)
	{
		return (regs->orig_gpr3);
	} else
	if (regno == PT_CTR)
	{
		return (regs->ctr);
	} else
	if (regno == PT_LNK)
	{
		return (regs->link);
	} else
	if (regno == PT_XER)
	{
		return (regs->xer);
	} else
	if (regno == PT_CCR)
	{
		return (regs->ccr);
	}
	return (0);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	struct pt_regs *regs = task->tss.regs;
	if (regno <= PT_R31)
	{
		regs->gpr[regno] = data;
	} else
	if (regno == PT_NIP)
	{
		regs->nip = data;
	} else
	if (regno == PT_MSR)
	{
		regs->msr = data;
	} else
	if (regno == PT_CTR)
	{
		regs->ctr = data;
	} else
	if (regno == PT_LNK)
	{
		regs->link = data;
	} else
	if (regno == PT_XER)
	{
		regs->xer = data;
	} else
	if (regno == PT_CCR)
	{
		regs->ccr = data;
	} else
	{ /* Invalid register */
		return (-1);
	}
	return (0);
}

static inline
set_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->tss.regs;
printk("Set single step - Task: %x, Regs: %x", task, regs);
printk(", MSR: %x/", regs->msr);	
	regs->msr |= MSR_SE;
printk("%x\n", regs->msr);	
}

static inline
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
		do_no_page(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir,addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page directory %08lx\n",
		       pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(tsk, vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page >= high_memory)
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
static void put_long(struct task_struct * tsk, struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;
		
repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (!pgd_present(*pgdir)) {
		do_no_page(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir,addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page directory %08lx\n",
		       pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(tsk, vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		do_wp_page(tsk, vma, addr, 2);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page < high_memory) {
		*(unsigned long *) (page + (addr & ~PAGE_MASK)) = data;
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	*pgtable = pte_mkdirty(mk_pte(page, vma->vm_page_prot));
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
	struct user * dummy;

	dummy = NULL;

	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			return -EPERM;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		return 0;
	}
	if (pid == 1)		/* you may not mess with init */
		return -EPERM;
	if (!(child = get_task(pid)))
		return -ESRCH;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			return -EPERM;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->gid)) && !suser())
			return -EPERM;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			return -EPERM;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		return 0;
	}
	if (!(child->flags & PF_PTRACED))
		return -ESRCH;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			return -ESRCH;
	}
	if (child->p_pptr != current)
		return -ESRCH;

	switch (request) {
	/* If I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int res;

			res = read_long(child, addr, &tmp);
			if (res < 0)
				return res;
			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (!res)
				put_user(tmp, (unsigned long *) data);
			return res;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			int res;
			
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				return -EIO;
			
			res = verify_area(VERIFY_WRITE, (void *) data,
					  sizeof(long));
			if (res)
				return res;
			tmp = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */
			if (addr < PT_FPR0) {
				tmp = get_reg(child, addr);
			}
#if 0			
			else if (addr >= PT_FPR0 && addr < PT_FPR31)
				tmp = child->tss.fpr[addr - PT_FPR0];
#endif				
			else
				return -EIO;
			put_user(tmp,(unsigned long *) data);
			return 0;
		}

      /* If I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			return write_long(child,addr,data);

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				return -EIO;

			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PT_ORIG_R3)
				return -EIO;
#if 0 /* Let this check be in 'put_reg' */				
			if (addr == PT_SR) {
				data &= SR_MASK;
				data <<= 16;
				data |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
			}
#endif			
			if (addr < PT_FPR0) {
				if (put_reg(child, addr, data))
					return -EIO;
				return 0;
			}
#if 0			
			if (addr >= 21 && addr < 48)
			{
				child->tss.fp[addr - 21] = data;
				return 0;
			}
#endif			
			return -EIO;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			if ((unsigned long) data >= NSIG)
				return -EIO;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			wake_up_process(child);
			/* make sure the single step bit is not set. */
			clear_single_step(child);
			return 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			if (child->state == TASK_ZOMBIE) /* already dead */
				return 0;
			wake_up_process(child);
			child->exit_code = SIGKILL;
			/* make sure the single step bit is not set. */
			clear_single_step(child);
			return 0;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			if ((unsigned long) data >= NSIG)
				return -EIO;
			child->flags &= ~PF_TRACESYS;
			set_single_step(child);
			wake_up_process(child);
			child->exit_code = data;
			/* give it a chance to run. */
			return 0;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			if ((unsigned long) data >= NSIG)
				return -EIO;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			wake_up_process(child);
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure the single step bit is not set. */
			clear_single_step(child);
			return 0;
		}

		default:
			return -EIO;
	}
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code)
		current->signal |= (1 << (current->exit_code - 1));
	current->exit_code = 0;
	return;
}
