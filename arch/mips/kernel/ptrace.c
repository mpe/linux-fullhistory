/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* further hacked for MIPS by David S. Miller (dm@engr.sgi.com) */

#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static unsigned long get_long(struct task_struct * tsk,
			      struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
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
	if (MAP_NR(page) >= MAP_NR(high_memory))
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
static void put_long(struct task_struct *tsk,
		     struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
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
	if (MAP_NR(page) < MAP_NR(high_memory)) {
		*(unsigned long *) (page + (addr & ~PAGE_MASK)) = data;
		flush_page_to_ram(page);
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb_page(vma, addr);
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk->mm, addr);
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
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(tsk, vma, addr & ~(sizeof(long)-1));
		high = get_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 1:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 3:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	} else
		*result = get_long(tsk, vma, addr);
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
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(tsk, vma, addr & ~(sizeof(long)-1));
		high = get_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
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
			case 3:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(tsk, vma, addr & ~(sizeof(long)-1),low);
		put_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1),high);
	} else
		put_long(tsk, vma, addr, data);
	return 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int res;

	lock_kernel();
#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED) {
			res = -EPERM;
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		res = 0;
		goto out;
	}
	if (pid == 1) {		/* you may not mess with init */
		res = -EPERM;
		goto out;
	}
	if (!(child = find_task_by_pid(pid))) {
		res = -ESRCH;
		goto out;
	}
	if (request == PTRACE_ATTACH) {
		if (child == current) {
			res = -EPERM;
			goto out;
		}
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
		    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid)) && !suser()) {
			res = -EPERM;
			goto out;
		}
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED) {
			res = -EPERM;
			goto out;
		}
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		res = 0;
		goto out;
	}
	if (!(child->flags & PF_PTRACED)) {
		res = -ESRCH;
		goto out;
	}
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL) {
			res = -ESRCH;
			goto out;
		}
	}
	if (child->p_pptr != current) {
		res = -ESRCH;
		goto out;
	}

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			res = read_long(child, addr, &tmp);
			if (res < 0)
				goto out;
			res = put_user(tmp,(unsigned long *) data);
			goto out;
		}

	/* read the word at location addr in the USER area. */
/* #define DEBUG_PEEKUSR */
		case PTRACE_PEEKUSR: {
			struct pt_regs *regs;
			unsigned long tmp;

			regs = (struct pt_regs *)
				(child->tss.ksp - sizeof(struct pt_regs));
			tmp = 0;  /* Default return value. */
			if(addr < 32 && addr >= 0) {
				tmp = regs->regs[addr];
			} else if(addr >= 32 && addr < 64) {
				unsigned long long *fregs;

				/* We don't want to do a FPU operation here. */
				fregs = (unsigned long long *)
					&child->tss.fpu.hard.fp_regs[0];
				tmp = (unsigned long) fregs[(addr - 32)];
			} else {
				addr -= 64;
				switch(addr) {
				case 0:
					tmp = regs->cp0_epc;
					break;
				case 1:
					tmp = regs->cp0_cause;
					break;
				case 2:
					tmp = regs->cp0_badvaddr;
					break;
				case 3:
					tmp = regs->lo;
					break;
				case 4:
					tmp = regs->hi;
					break;
				case 5:
					tmp = child->tss.fpu.hard.control;
					break;
				case 6:
					tmp = 0;
					break;
				default:
					tmp = 0;
					res = -EIO;
					goto out;
				};
			}
			res = put_user(tmp, (unsigned long *) data);
			goto out;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			res = write_long(child,addr,data);
			goto out;

		case PTRACE_POKEUSR: {
			struct pt_regs *regs;
			int res = 0;

			regs = (struct pt_regs *)
				(child->tss.ksp - sizeof(struct pt_regs));
			if(addr < 32 && addr >= 0) {
				regs->regs[addr] = data;
			} else if(addr >= 32 && addr < 64) {
				unsigned long long *fregs;

				/* We don't want to do a FPU operation here. */
				fregs = (unsigned long long *)
					&child->tss.fpu.hard.fp_regs[0];
				fregs[(addr - 32)] = (unsigned long long) data;
			} else {
				addr -= 64;
				switch(addr) {
				case 0:
					regs->cp0_epc = data;
					break;
				case 3:
					regs->lo = data;
					break;
				case 4:
					regs->hi = data;
					break;
				case 5:
					child->tss.fpu.hard.control = data;
					break;
				default:
					/* The rest are not allowed. */
					res = -EIO;
					break;
				};
			}
			goto out;
		}

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			if ((unsigned long) data > NSIG) {
				res = -EIO;
				goto out;
			}
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			wake_up_process(child);
			res = data;
			goto out;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			if (child->state != TASK_ZOMBIE) {
				wake_up_process(child);
				child->exit_code = SIGKILL;
			}
			res = 0;
			goto out;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			if ((unsigned long) data > NSIG) {
				res = -EIO;
				goto out;
			}
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			wake_up_process(child);
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			res = 0;
			goto out;
		}

		default:
			res = -EIO;
			goto out;
	}
out:
	unlock_kernel();
	return res;
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
	if (current->exit_code) {
		spin_lock_irq(&current->sigmask_lock);
		current->signal |= (1 << (current->exit_code - 1));
		spin_unlock_irq(&current->sigmask_lock);
	}
	current->exit_code = 0;
}
