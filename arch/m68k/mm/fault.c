/*
 *  linux/arch/m68k/mm/fault.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>

#include <asm/system.h>
#include <asm/pgtable.h>

extern void die_if_kernel(char *, struct pt_regs *, long);

/*
 * This routine handles page faults.  It determines the problem, and
 * then passes it off to one of the appropriate routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *
 * If this routine detects a bad access, it returns 1, otherwise it
 * returns 0.
 */
asmlinkage int do_page_fault(struct pt_regs *regs, unsigned long address,
			      unsigned long error_code)
{
	struct vm_area_struct * vma;

#ifdef DEBUG
	printk ("regs->sr=%#x, regs->pc=%#lx, address=%#lx, %ld, %p\n",
		regs->sr, regs->pc, address, error_code,
		current->tss.pagedir_v);
#endif

	vma = find_vma(current, address);
	if (!vma)
	  goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (user_mode(regs)) {
		/* Accessing the stack below usp is always a bug.  The
		   "+ 256" is there due to some instructions doing
		   pre-decrement on the stack and that doesn't show up
		   until later.  */
		if (address + 256 < rdusp())
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	/*
	 * was it a write?
	 */
	if (error_code & 2) {
	  if (!(vma->vm_flags & VM_WRITE))
	    goto bad_area;
	} else {
		/* read with protection fault? */
	  if (error_code & 1)
	    goto bad_area;
	  if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
	    goto bad_area;
	}
	if (error_code & 1) {
		do_wp_page(current, vma, address, error_code & 2);
		return 0;
	}
	do_no_page(current, vma, address, error_code & 2);

	/* There seems to be a missing invalidate somewhere in do_no_page.
	 * Until I found it, this one cures the problem and makes
	 * 1.2 run on the 68040 (Martin Apel).
	 */
	flush_tlb_all();

	return 0;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	if (user_mode(regs)) {
		/* User memory access */
		force_sig (SIGSEGV, current);
		return 1;
	}

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	if ((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	} else
		printk(KERN_ALERT "Unable to handle kernel access");
	printk(" at virtual address %08lx\n",address);
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);

	return 1;
}
