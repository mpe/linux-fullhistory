/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */

#include <linux/config.h> /* for CONFIG_MATH_EMULATION */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/debugreg.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

/* set's the trap flag. */
#define TRAP_FLAG 0x100

/*
 * Offset of eflags on child stack..
 */
#define EFL_OFFSET ((EFL-2)*4-sizeof(struct pt_regs))

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->tss.esp0;
	stack += offset;
	return (*((int *)stack));
}

/*
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->tss.esp0;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

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
	unsigned long page;

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
	if (MAP_NR(page) < max_mapnr)
		*(unsigned long *) (page + (addr & ~PAGE_MASK)) = data;
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb();
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

static int putreg(struct task_struct *child,
	unsigned long regno, unsigned long value)
{
	switch (regno >> 2) {
		case ORIG_EAX:
			return -EIO;
		case FS:
			if (value && (value & 3) != 3)
				return -EIO;
			child->tss.fs = value;
			return 0;
		case GS:
			if (value && (value & 3) != 3)
				return -EIO;
			child->tss.gs = value;
			return 0;
		case DS:
		case ES:
			if (value && (value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
		case SS:
		case CS:
			if ((value & 3) != 3)
				return -EIO;
			value &= 0xffff;
			break;
		case EFL:
			value &= FLAG_MASK;
			value |= get_stack_long(child, EFL_OFFSET) & ~FLAG_MASK;
	}
	if (regno > GS*4)
		regno -= 2*4;
	put_stack_long(child, regno - sizeof(struct pt_regs), value);
	return 0;
}

static unsigned long getreg(struct task_struct *child,
	unsigned long regno)
{
	unsigned long retval = ~0UL;

	switch (regno >> 2) {
		case FS:
			retval = child->tss.fs;
			break;
		case GS:
			retval = child->tss.gs;
			break;
		case DS:
		case ES:
		case SS:
		case CS:
			retval = 0xffff;
			/* fall through */
		default:
			if (regno > GS*4)
				regno -= 2*4;
			regno = regno - sizeof(struct pt_regs);
			retval &= get_stack_long(child, regno);
	}
	return retval;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	struct user * dummy = NULL;
	int i, ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);	/* FIXME!!! */
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			unsigned long flags;

			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
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
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			down(&child->mm->mmap_sem);
			ret = read_long(child, addr, &tmp);
			up(&child->mm->mmap_sem);
			if (ret >= 0)
				ret = put_user(tmp,(unsigned long *) data);
			goto out;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;

			ret = -EIO;
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				goto out;

			tmp = 0;  /* Default return condition */
			if(addr < 17*sizeof(long))
				tmp = getreg(child, addr);
			if(addr >= (long) &dummy->u_debugreg[0] &&
			   addr <= (long) &dummy->u_debugreg[7]){
				addr -= (long) &dummy->u_debugreg[0];
				addr = addr >> 2;
				tmp = child->tss.debugreg[addr];
			};
			ret = put_user(tmp,(unsigned long *) data);
			goto out;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			down(&child->mm->mmap_sem);
			ret = write_long(child,addr,data);
			up(&child->mm->mmap_sem);
			goto out;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				goto out;

			if (addr < 17*sizeof(long)) {
				ret = putreg(child, addr, data);
				goto out;
			}

		  /* We need to be very careful here.  We implicitly
		     want to modify a portion of the task_struct, and we
		     have to be selective about what portions we allow someone
		     to modify. */

		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) return -EIO;
			  if(addr == (long) &dummy->u_debugreg[5]) return -EIO;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= TASK_SIZE-3) return -EIO;
			  
			  ret = -EIO;
			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5f54 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  goto out;
			  };

			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->tss.debugreg[addr] = data;
			  ret = 0;
			  goto out;
		  };
		  ret = -EIO;
		  goto out;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET,tmp);
			wake_up_process(child);
			ret = 0;
			goto out;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			long tmp;

			ret = 0;
			if (child->state == TASK_ZOMBIE)	/* already dead */
				goto out;
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET, tmp);
			wake_up_process(child);
			goto out;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->flags &= ~PF_TRACESYS;
			if ((child->flags & PF_DTRACE) == 0) {
				/* Spurious delayed TF traps may occur */
				child->flags |= PF_DTRACE;
			}
			tmp = get_stack_long(child, EFL_OFFSET) | TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET, tmp);
			child->exit_code = data;
	/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			goto out;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			unsigned long flags;
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			child->exit_code = data;
			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, EFL_OFFSET) & ~TRAP_FLAG;
			put_stack_long(child, EFL_OFFSET, tmp);
			wake_up_process(child);
			ret = 0;
			goto out;
		}

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	if (!access_ok(VERIFY_WRITE, (unsigned *)data,
				       17*sizeof(long)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			for ( i = 0; i < 17*sizeof(long); i += sizeof(long) )
			  {
			    __put_user(getreg(child, i),(unsigned long *) data);
			    data += sizeof(long);
			  }
			ret = 0;
			goto out;
		  };

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			unsigned long tmp;
		  	if (!access_ok(VERIFY_READ, (unsigned *)data,
				       17*sizeof(long)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			for ( i = 0; i < 17*sizeof(long); i += sizeof(long) )
			  {
			    __get_user(tmp, (unsigned long *) data);
			    putreg(child, i, tmp);
			    data += sizeof(long);
			  }
			ret = 0;
			goto out;
		  };

		case PTRACE_GETFPREGS: { /* Get the child FPU state. */
			if (!access_ok(VERIFY_WRITE, (unsigned *)data,
				       sizeof(struct user_i387_struct)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			ret = 0;
			if ( !child->used_math ) {
			  /* Simulate an empty FPU. */
			  child->tss.i387.hard.cwd = 0xffff037f;
			  child->tss.i387.hard.swd = 0xffff0000;
			  child->tss.i387.hard.twd = 0xffffffff;
			}
#ifdef CONFIG_MATH_EMULATION
			if ( boot_cpu_data.hard_math ) {
#endif
				__copy_to_user((void *)data, &child->tss.i387.hard,
						sizeof(struct user_i387_struct));
#ifdef CONFIG_MATH_EMULATION
			} else {
			  save_i387_soft(&child->tss.i387.soft,
					 (struct _fpstate *)data);
			}
#endif
			goto out;
		  };

		case PTRACE_SETFPREGS: { /* Set the child FPU state. */
			if (!access_ok(VERIFY_READ, (unsigned *)data,
				       sizeof(struct user_i387_struct)))
			  {
			    ret = -EIO;
			    goto out;
			  }
			child->used_math = 1;
#ifdef CONFIG_MATH_EMULATION
			if ( boot_cpu_data.hard_math ) {
#endif
			  __copy_from_user(&child->tss.i387.hard, (void *)data,
					   sizeof(struct user_i387_struct));
#ifdef CONFIG_MATH_EMULATION
			} else {
			  restore_i387_soft(&child->tss.i387.soft,
					    (struct _fpstate *)data);
			}
#endif
			ret = 0;
			goto out;
		  };

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
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
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
}
