/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* edited for ARM by Russell King */

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

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Breakpoint SWI instruction: SWI &9F0001
 */
#define BREAKINST	0xef9f0001

/*
 * this routine will get a word off of the processes privileged stack.
 * the offset is how far from the base addr as stored in the TSS.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)((unsigned long)task + 8192 - sizeof(struct pt_regs));
	stack += offset << 2;
	return *(unsigned long *)stack;
}

/*
 * this routine will put a word on the processes privileged stack.
 * the offset is how far from the base addr as stored in the TSS.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char *stack;

	stack = (unsigned char *)((unsigned long)task + 8192 - sizeof(struct pt_regs));
	stack += offset << 2;
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
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
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
 
	if(MAP_NR(page) >= max_mapnr)
		return 0;
	page += addr & ~PAGE_MASK;
	return *(unsigned long *)page;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of the page so that a text page stays readonly
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
	
	if (MAP_NR(page) < max_mapnr) {
		page += addr & ~PAGE_MASK;
		*(unsigned long *)page = data;
		__flush_entry_to_ram(page);
	}
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

/*
 * Get value of register `rn' (in the instruction)
 */
static unsigned long ptrace_getrn (struct task_struct *child, unsigned long insn)
{
	unsigned int reg = (insn >> 16) & 15;
	unsigned long val;

	if (reg == 15)
		val = pc_pointer (get_stack_long (child, reg));
	else
		val = get_stack_long (child, reg);

printk ("r%02d=%08lX ", reg, val);
	return val;
}

/*
 * Get value of operand 2 (in an ALU instruction)
 */
static unsigned long ptrace_getaluop2 (struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

printk ("op2=");
	if (insn & 1 << 25) {
		val = insn & 255;
		shift = (insn >> 8) & 15;
		type = 3;
printk ("(imm)");
	} else {
		val = get_stack_long (child, insn & 15);

		if (insn & (1 << 4))
			shift = (int)get_stack_long (child, (insn >> 8) & 15);
		else
			shift = (insn >> 7) & 31;

		type = (insn >> 5) & 3;
printk ("(r%02ld)", insn & 15);
	}
printk ("sh%dx%d", type, shift);
	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
		__asm__ __volatile__("mov %0, %0, ror %1" : "=r" (val) : "0" (val), "r" (shift));
		break;
	}
printk ("=%08lX ", val);
	return val;
}

/*
 * Get value of operand 2 (in a LDR instruction)
 */
static unsigned long ptrace_getldrop2 (struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

	val = get_stack_long (child, insn & 15);
	shift = (insn >> 7) & 31;
	type = (insn >> 5) & 3;

printk ("op2=r%02ldsh%dx%d", insn & 15, shift, type);
	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
		__asm__ __volatile__("mov %0, %0, ror %1" : "=r" (val) : "0" (val), "r" (shift));
		break;
	}
printk ("=%08lX ", val);
	return val;
}
#undef pc_pointer
#define pc_pointer(x) ((x) & 0x03fffffc)
int ptrace_set_bpt (struct task_struct *child)
{
	unsigned long insn, pc, alt;
	int i, nsaved = 0, res;

	pc = pc_pointer (get_stack_long (child, 15/*REG_PC*/));

	res = read_long (child, pc, &insn);
	if (res < 0)
		return res;

	child->tss.debug[nsaved++] = alt = pc + 4;
printk ("ptrace_set_bpt: insn=%08lX pc=%08lX ", insn, pc);
	switch (insn & 0x0e100000) {
	case 0x00000000:
	case 0x00100000:
	case 0x02000000:
	case 0x02100000: /* data processing */
		printk ("data ");
		switch (insn & 0x01e0f000) {
		case 0x0000f000:
			alt = ptrace_getrn(child, insn) & ptrace_getaluop2(child, insn);
			break;
		case 0x0020f000:
			alt = ptrace_getrn(child, insn) ^ ptrace_getaluop2(child, insn);
			break;
		case 0x0040f000:
			alt = ptrace_getrn(child, insn) - ptrace_getaluop2(child, insn);
			break;
		case 0x0060f000:
			alt = ptrace_getaluop2(child, insn) - ptrace_getrn(child, insn);
			break;
		case 0x0080f000:
			alt = ptrace_getrn(child, insn) + ptrace_getaluop2(child, insn);
			break;
		case 0x00a0f000:
			alt = ptrace_getrn(child, insn) + ptrace_getaluop2(child, insn) +
				(get_stack_long (child, 16/*REG_PSR*/) & CC_C_BIT ? 1 : 0);
			break;
		case 0x00c0f000:
			alt = ptrace_getrn(child, insn) - ptrace_getaluop2(child, insn) +
				(get_stack_long (child, 16/*REG_PSR*/) & CC_C_BIT ? 1 : 0);
			break;
		case 0x00e0f000:
			alt = ptrace_getaluop2(child, insn) - ptrace_getrn(child, insn) +
				(get_stack_long (child, 16/*REG_PSR*/) & CC_C_BIT ? 1 : 0);
			break;
		case 0x0180f000:
			alt = ptrace_getrn(child, insn) | ptrace_getaluop2(child, insn);
			break;
		case 0x01a0f000:
			alt = ptrace_getaluop2(child, insn);
			break;
		case 0x01c0f000:
			alt = ptrace_getrn(child, insn) & ~ptrace_getaluop2(child, insn);
			break;
		case 0x01e0f000:
			alt = ~ptrace_getaluop2(child, insn);
			break;
		}
		break;

	case 0x04100000: /* ldr */
		if ((insn & 0xf000) == 0xf000) {
printk ("ldr ");
			alt = ptrace_getrn(child, insn);
			if (insn & 1 << 24) {
				if (insn & 1 << 23)
					alt += ptrace_getldrop2 (child, insn);
				else
					alt -= ptrace_getldrop2 (child, insn);
			}
			if (read_long (child, alt, &alt) < 0)
				alt = pc + 4; /* not valid */
			else
				alt = pc_pointer (alt);
		}
		break;

	case 0x06100000: /* ldr imm */
		if ((insn & 0xf000) == 0xf000) {
printk ("ldrimm ");
			alt = ptrace_getrn(child, insn);
			if (insn & 1 << 24) {
				if (insn & 1 << 23)
					alt += insn & 0xfff;
				else
					alt -= insn & 0xfff;
			}
			if (read_long (child, alt, &alt) < 0)
				alt = pc + 4; /* not valid */
			else
				alt = pc_pointer (alt);
		}
		break;

	case 0x08100000: /* ldm */
		if (insn & (1 << 15)) {
			unsigned long base;
			int nr_regs;
printk ("ldm ");

			if (insn & (1 << 23)) {
				nr_regs = insn & 65535;

				nr_regs = (nr_regs & 0x5555) + ((nr_regs & 0xaaaa) >> 1);
				nr_regs = (nr_regs & 0x3333) + ((nr_regs & 0xcccc) >> 2);
				nr_regs = (nr_regs & 0x0707) + ((nr_regs & 0x7070) >> 4);
				nr_regs = (nr_regs & 0x000f) + ((nr_regs & 0x0f00) >> 8);
				nr_regs <<= 2;

				if (!(insn & (1 << 24)))
					nr_regs -= 4;
			} else {
				if (insn & (1 << 24))
					nr_regs = -4;
				else
					nr_regs = 0;
			}

			base = ptrace_getrn (child, insn);

			if (read_long (child, base + nr_regs, &alt) < 0)
				alt = pc + 4; /* not valid */
			else
				alt = pc_pointer (alt);
			break;
		}
		break;

	case 0x0a000000:
	case 0x0a100000: { /* bl or b */
		signed long displ;
printk ("b/bl ");
		/* It's a branch/branch link: instead of trying to
		 * figure out whether the branch will be taken or not,
		 * we'll put a breakpoint at either location.  This is
		 * simpler, more reliable, and probably not a whole lot
		 * slower than the alternative approach of emulating the
		 * branch.
		 */
		displ = (insn & 0x00ffffff) << 8;
		displ = (displ >> 6) + 8;
		if (displ != 0 && displ != 4)
			alt = pc + displ;
	    }
	    break;
	}
printk ("=%08lX\n", alt);
	if (alt != pc + 4)
		child->tss.debug[nsaved++] = alt;

	for (i = 0; i < nsaved; i++) {
		res = read_long (child, child->tss.debug[i], &insn);
		if (res >= 0) {
			child->tss.debug[i + 2] = insn;
			res = write_long (child, child->tss.debug[i], BREAKINST);
		}
		if (res < 0) {
			child->tss.debug[4] = 0;
			return res;
		}
	}
	child->tss.debug[4] = nsaved;
	return 0;
}

/* Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
int ptrace_cancel_bpt (struct task_struct *child)
{
	int i, nsaved = child->tss.debug[4];

	child->tss.debug[4] = 0;

	if (nsaved > 2) {
		printk ("ptrace_cancel_bpt: bogus nsaved: %d!\n", nsaved);
		nsaved = 2;
	}
	for (i = 0; i < nsaved; i++)
		write_long (child, child->tss.debug[i], child->tss.debug[i + 2]);
	return nsaved != 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

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
		case PTRACE_PEEKTEXT:				/* read word at location addr. */
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			ret = read_long(child, addr, &tmp);
			if (ret >= 0)
				ret = put_user(tmp, (unsigned long *)data);
			goto out;
		}

		case PTRACE_PEEKUSR: {				/* read the word at location addr in the USER area. */
			unsigned long tmp;

			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				goto out;

			tmp = 0;  /* Default return condition */
			if (addr < sizeof (struct pt_regs))
				tmp = get_stack_long(child, (int)addr >> 2);
			ret = put_user(tmp, (unsigned long *)data);
			goto out;
		}

		case PTRACE_POKETEXT:				/* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = write_long(child,addr,data);
			goto out;

		case PTRACE_POKEUSR:				/* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				goto out;

			if (addr < sizeof (struct pt_regs))
				ret = put_stack_long(child, (int)addr >> 2, data);
			goto out;

		case PTRACE_SYSCALL:				/* continue and stop at next (return from) syscall */
		case PTRACE_CONT:				/* restart after signal. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			wake_up_process (child);
			/* make sure single-step breakpoint is gone. */
			ptrace_cancel_bpt (child);
			ret = 0;
			goto out;

		/* make the child exit.  Best I can do is send it a sigkill.
		 * perhaps it should be put in the status that it wants to
		 * exit.
		 */
		case PTRACE_KILL:
			if (child->state == TASK_ZOMBIE)	/* already dead */
				return 0;
			wake_up_process (child);
			child->exit_code = SIGKILL;
			ptrace_cancel_bpt (child);
			/* make sure single-step breakpoint is gone. */
			ptrace_cancel_bpt (child);
			ret = 0;
			goto out;

		case PTRACE_SINGLESTEP:				/* execute single instruction. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->tss.debug[4] = -1;
			child->flags &= ~PF_TRACESYS;
			wake_up_process(child);
			child->exit_code = data;
			/* give it a chance to run. */
			ret = 0;
			goto out;

		case PTRACE_DETACH:				/* detach a process that was attached. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			wake_up_process (child);
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure single-step breakpoint is gone. */
			ptrace_cancel_bpt (child);
			ret = 0;
			goto out;

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
