/*
 *  linux/arch/alpha/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>

#include <asm/system.h>
#include <asm/segment.h>

extern void die_if_kernel(char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * mmcsr:
 *	0 = translation not valid (= do_no_page())
 *	1 = access violation (= user tries to access kernel pages)
 *	2 = fault-on-read
 *	3 = fault-on-execute
 *	4 = fault-on-write
 *
 * cause:
 *	-1 = instruction fetch
 *	0 = load
 *	1 = store
 */
asmlinkage void do_page_fault(unsigned long address, unsigned long mmcsr,
	long cause, struct pt_regs * regs)
{
	struct vm_area_struct * vma;

	if (mmcsr == 1)
		goto bad_area;
	for (vma = current->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			goto bad_area;
		if (vma->vm_end > address)
			break;
	}
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur)
		goto bad_area;
	vma->vm_offset -= vma->vm_start - (address & PAGE_MASK);
	vma->vm_start = (address & PAGE_MASK);
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	if (!(vma->vm_page_prot & PAGE_USER))
		goto bad_area;
	if (mmcsr) {
		if (!(vma->vm_page_prot & (PAGE_RW | PAGE_COW)))
			goto bad_area;
		do_wp_page(vma, address, cause > 0);
		return;
	}
	do_no_page(vma, address, cause > 0);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	if (user_mode(regs)) {
		send_sig(SIGSEGV, current, 1);
		return;
	}
/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	printk(KERN_ALERT "Unable to handle kernel paging request at virtual address %08lx\n",address);
	die_if_kernel("Oops", regs, cause);
	do_exit(SIGKILL);
}
