/*
 *  linux/arch/arm/mm/fault-common.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-1999 Russell King
 */
#include <linux/config.h>

extern void die(const char *msg, struct pt_regs *regs, int err);

/*
 * This is useful to dump out the page tables associated with
 * 'addr' in mm 'mm'.
 */
static void show_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	if (!mm)
		mm = &init_mm;

	printk(KERN_ALERT "pgd = %p\n", mm->pgd);
	pgd = pgd_offset(mm, addr);
	printk(KERN_ALERT "*pgd = %08lx", pgd_val(*pgd));

	do {
		pmd_t *pmd;
		pte_t *pte;

		if (pgd_none(*pgd))
			break;

		if (pgd_bad(*pgd)) {
			printk("(bad)\n");
			break;
		}

		pmd = pmd_offset(pgd, addr);
		printk(", *pmd = %08lx", pmd_val(*pmd));

		if (pmd_none(*pmd))
			break;

		if (pmd_bad(*pmd)) {
			printk("(bad)\n");
			break;
		}

		pte = pte_offset(pmd, addr);
		printk(", *pte = %08lx", pte_val(*pte));
#ifdef CONFIG_CPU_32
		printk(", *ppte = %08lx", pte_val(pte[-PTRS_PER_PTE]));
#endif
	} while(0);

	printk("\n");
}

static int __do_page_fault(struct mm_struct *mm, unsigned long addr, int mode, struct task_struct *tsk)
{
	struct vm_area_struct *vma;
	int fault, mask;

	vma = find_vma(mm, addr);
	fault = -2; /* bad map area */
	if (!vma)
		goto out;
	if (vma->vm_start > addr)
		goto check_stack;

	/*
	 * Ok, we have a good vm_area for this
	 * memory access, so we can handle it.
	 */
good_area:
	if (READ_FAULT(mode)) /* read? */
		mask = VM_READ|VM_EXEC;
	else
		mask = VM_WRITE;

	fault = -1; /* bad access type */
	if (!(vma->vm_flags & mask))
		goto out;

	/*
	 * If for any reason at all we couldn't handle
	 * the fault, make sure we exit gracefully rather
	 * than endlessly redo the fault.
	 */
survive:
	fault = handle_mm_fault(mm, vma, addr & PAGE_MASK, DO_COW(mode));

	/*
	 * Handle the "normal" cases first - successful and sigbus
	 */
	switch (fault) {
	case 2:
		tsk->maj_flt++;
		return fault;
	case 1:
		tsk->min_flt++;
	case 0:
		return fault;
	}

	fault = -3; /* out of memory */
	if (tsk->pid != 1)
		goto out;

	/*
	 * If we are out of memory for pid1,
	 * sleep for a while and retry
	 */
	tsk->policy |= SCHED_YIELD;
	schedule();
	goto survive;

check_stack:
	if (vma->vm_flags & VM_GROWSDOWN && !expand_stack(vma, addr))
		goto good_area;
out:
	return fault;
}

static int do_page_fault(unsigned long addr, int mode, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	unsigned long fixup;
	int fault;

	tsk = current;
	mm  = tsk->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || !mm)
		goto no_context;

	down(&mm->mmap_sem);
	fault = __do_page_fault(mm, addr, mode, tsk);
	up(&mm->mmap_sem);

	/*
	 * Handle the "normal" case first
	 */
	if (fault > 0)
		return 0;

	/*
	 * We had some memory, but were unable to
	 * successfully fix up this page fault.
	 */
	if (fault == 0)
		goto do_sigbus;

	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (!user_mode(regs))
		goto no_context;

	if (fault == -3) {
		/*
		 * We ran out of memory, or some other thing happened to
		 * us that made us unable to handle the page fault gracefully.
		 */
		printk("VM: killing process %s\n", tsk->comm);
		do_exit(SIGKILL);
	} else {
		/*
		 * Something tried to access memory that isn't in our memory map..
		 * User mode accesses just cause a SIGSEGV
		 */
		struct siginfo si;

#ifdef CONFIG_DEBUG_USER
		printk(KERN_DEBUG "%s: unhandled page fault at pc=0x%08lx, "
		       "lr=0x%08lx (bad address=0x%08lx, code %d)\n",
		       tsk->comm, regs->ARM_pc, regs->ARM_lr, addr, mode);
#endif

		tsk->thread.address = addr;
		tsk->thread.error_code = mode;
		tsk->thread.trap_no = 14;
		si.si_signo = SIGSEGV;
		si.si_errno = 0;
		si.si_code = fault == -1 ? SEGV_ACCERR : SEGV_MAPERR;
		si.si_addr = (void *)addr;
		force_sig_info(SIGSEGV, &si, tsk);
	}
	return 0;


/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
do_sigbus:
	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.address = addr;
	tsk->thread.error_code = mode;
	tsk->thread.trap_no = 14;
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (user_mode(regs))
		return 0;

no_context:
	/* Are we prepared to handle this kernel fault?  */
	if ((fixup = search_exception_table(instruction_pointer(regs))) != 0) {
#ifdef DEBUG
		printk(KERN_DEBUG "%s: Exception at [<%lx>] addr=%lx (fixup: %lx)\n",
			tsk->comm, regs->ARM_pc, addr, fixup);
#endif
		regs->ARM_pc = fixup;
		return 0;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	printk(KERN_ALERT "Unable to handle kernel %s at virtual address %08lx\n",
		(addr < PAGE_SIZE) ? "NULL pointer dereference" : "paging request", addr);

	show_pte(mm, addr);
	die("Oops", regs, mode);
	do_exit(SIGKILL);

	return 0;
}
