/*
 *  linux/arch/alpha/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

unsigned long asn_cache = ASN_FIRST_VERSION;

#ifndef BROKEN_ASN
/*
 * Select a new ASN and reload the context. This is
 * not inlined as this expands to a pretty large
 * function.
 */
void get_new_asn_and_reload(struct task_struct *tsk, struct mm_struct *mm)
{
	get_new_mmu_context(tsk, mm, asn_cache);
	reload_context(tsk);
}
#endif

extern void die_if_kernel(char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to handle_mm_fault().
 *
 * mmcsr:
 *	0 = translation not valid
 *	1 = access violation
 *	2 = fault-on-read
 *	3 = fault-on-execute
 *	4 = fault-on-write
 *
 * cause:
 *	-1 = instruction fetch
 *	0 = load
 *	1 = store
 */
asmlinkage void do_page_fault(unsigned long address, unsigned long mmcsr, long cause,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	struct vm_area_struct * vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	down(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	if (cause < 0) {
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else if (!cause) {
		/* Allow reads even for write-only mappings */
		if (!(vma->vm_flags & (VM_READ | VM_WRITE)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}
	handle_mm_fault(vma, address, cause > 0);
	up(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);
	/* Did we have an exception handler installed? */
	if (current->tss.ex.count == 1) {
		current->tss.ex.count = 0;
		__handle_exception(&current->tss.ex);
	}
	if (user_mode(&regs)) {
		printk("%s: memory violation at pc=%08lx rp=%08lx (bad address = %08lx)\n",
			tsk->comm, regs.pc, regs.r26, address);
		die_if_kernel("oops", &regs, cause);
		force_sig(SIGSEGV, tsk);
		return;
	}
/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	printk(KERN_ALERT 
	       "Unable to handle kernel paging request at virtual address %016lx\n", address);
	die_if_kernel("Oops", &regs, cause);
	do_exit(SIGKILL);
}
