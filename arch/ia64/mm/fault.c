/*
 * MMU fault handling support.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

extern void die (char *, struct pt_regs *, long);

/*
 * This routine is analogous to expand_stack() but instead grows the
 * register backing store (which grows towards higher addresses).
 * Since the register backing store is access sequentially, we
 * disallow growing the RBS by more than a page at a time.  Note that
 * the VM_GROWSUP flag can be set on any VM area but that's fine
 * because the total process size is still limited by RLIMIT_STACK and
 * RLIMIT_AS.
 */
static inline long
expand_backing_store (struct vm_area_struct *vma, unsigned long address)
{
	unsigned long grow;

	grow = PAGE_SIZE >> PAGE_SHIFT;
	if (address - vma->vm_start > current->rlim[RLIMIT_STACK].rlim_cur
	    || (((vma->vm_mm->total_vm + grow) << PAGE_SHIFT) > current->rlim[RLIMIT_AS].rlim_cur))
		return -ENOMEM;
	vma->vm_end += PAGE_SIZE;
	vma->vm_mm->total_vm += grow;
	if (vma->vm_flags & VM_LOCKED)
		vma->vm_mm->locked_vm += grow;
	return 0;
}

void
ia64_do_page_fault (unsigned long address, unsigned long isr, struct pt_regs *regs)
{
	int signal = SIGSEGV, code = SEGV_MAPERR;
	struct vm_area_struct *vma, *prev_vma;
	struct mm_struct *mm = current->mm;
	struct exception_fixup fix;
	struct siginfo si;
	unsigned long mask;

	/*
	 * If we're in an interrupt or have no user context, we must not take the fault..
	 */
	if (in_interrupt() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);

	vma = find_vma_prev(mm, address, &prev_vma);
	if (!vma)
		goto bad_area;

	/* find_vma_prev() returns vma such that address < vma->vm_end or NULL */
	if (address < vma->vm_start)
		goto check_expansion;

  good_area:
	code = SEGV_ACCERR;

	/* OK, we've got a good vm_area for this memory area.  Check the access permissions: */

#	define VM_READ_BIT	0
#	define VM_WRITE_BIT	1
#	define VM_EXEC_BIT	2

#	if (((1 << VM_READ_BIT) != VM_READ || (1 << VM_WRITE_BIT) != VM_WRITE) \
	    || (1 << VM_EXEC_BIT) != VM_EXEC)
#		error File is out of sync with <linux/mm.h>.  Pleaes update.
#	endif

	mask = (  (((isr >> IA64_ISR_X_BIT) & 1UL) << VM_EXEC_BIT)
		| (((isr >> IA64_ISR_W_BIT) & 1UL) << VM_WRITE_BIT)
		| (((isr >> IA64_ISR_R_BIT) & 1UL) << VM_READ_BIT));

	if ((vma->vm_flags & mask) != mask)
		goto bad_area;

  survive:
	/*
	 * If for any reason at all we couldn't handle the fault, make
	 * sure we exit gracefully rather than endlessly redo the
	 * fault.
	 */
	switch (handle_mm_fault(mm, vma, address, mask)) {
	      case 1:
		++current->min_flt;
		break;
	      case 2:
		++current->maj_flt;
		break;
	      case 0:
		/*
		 * We ran out of memory, or some other thing happened
		 * to us that made us unable to handle the page fault
		 * gracefully.
		 */
		signal = SIGBUS;
		goto bad_area;
	      default:
		goto out_of_memory;
	}
	up_read(&mm->mmap_sem);
	return;

  check_expansion:
	if (!(prev_vma && (prev_vma->vm_flags & VM_GROWSUP) && (address == prev_vma->vm_end))) {
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto bad_area;
		if (rgn_index(address) != rgn_index(vma->vm_start)
		    || rgn_offset(address) >= RGN_MAP_LIMIT)
			goto bad_area;
		if (expand_stack(vma, address))
			goto bad_area;
	} else {
		vma = prev_vma;
		if (rgn_index(address) != rgn_index(vma->vm_start)
		    || rgn_offset(address) >= RGN_MAP_LIMIT)
			goto bad_area;
		if (expand_backing_store(vma, address))
			goto bad_area;
	}
	goto good_area;

  bad_area:
	up_read(&mm->mmap_sem);
	if (isr & IA64_ISR_SP) {
		/*
		 * This fault was due to a speculative load set the "ed" bit in the psr to
		 * ensure forward progress (target register will get a NaT).
		 */
		ia64_psr(regs)->ed = 1;
		return;
	}
	if (user_mode(regs)) {
		si.si_signo = signal;
		si.si_errno = 0;
		si.si_code = code;
		si.si_addr = (void *) address;
		force_sig_info(signal, &si, current);
		return;
	}

  no_context:
	if (isr & IA64_ISR_SP) {
		/*
		 * This fault was due to a speculative load set the "ed" bit in the psr to
		 * ensure forward progress (target register will get a NaT).
		 */
		ia64_psr(regs)->ed = 1;
		return;
	}

#ifdef GAS_HAS_LOCAL_TAGS
	fix = search_exception_table(regs->cr_iip + ia64_psr(regs)->ri);
#else
	fix = search_exception_table(regs->cr_iip);
#endif
	if (fix.cont) {
		handle_exception(regs, fix);
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to terminate things
	 * with extreme prejudice.
	 */
	bust_spinlocks(1);

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request at "
		       "virtual address %016lx\n", address);
	die("Oops", regs, isr);
	bust_spinlocks(0);
	do_exit(SIGKILL);
	return;

  out_of_memory:
	up_read(&mm->mmap_sem);
	if (current->pid == 1) {
		current->policy |= SCHED_YIELD;
		schedule();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", current->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	goto no_context;
}
