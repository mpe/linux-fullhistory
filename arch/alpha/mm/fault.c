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
#include <asm/uaccess.h>
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

extern void die_if_kernel(char *,struct pt_regs *,long, unsigned long *);

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
 *
 * Registers $9 through $15 are saved in a block just prior to `regs' and
 * are saved and restored around the call to allow exception code to
 * modify them.
 */

/* Macro for exception fixup code to access integer registers.  */
#define dpf_reg(r) \
	(((unsigned long *)regs)[(r) <= 8 ? (r) : (r) <= 15 ? (r)-16 : \
				 (r) <= 18 ? (r)+8 : (r)-10])

asmlinkage void do_page_fault(unsigned long address, unsigned long mmcsr,
			      long cause, struct pt_regs *regs)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	unsigned fixup;

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
	handle_mm_fault(current, vma, address, cause > 0);
	up(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);

	if (user_mode(regs)) {
		force_sig(SIGSEGV, current);
		return;
	}

	/* Are we prepared to handle this fault as an exception?  */
	if ((fixup = search_exception_table(regs->pc)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(dpf_reg, fixup, regs->pc);
		printk("%s: Exception at [<%lx>] (%lx)\n", current->comm, regs->pc, newpc);
		regs->pc = newpc;
		return;
	}

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	printk(KERN_ALERT "Unable to handle kernel paging request at "
	       "virtual address %016lx\n", address);
	die_if_kernel("Oops", regs, cause, (unsigned long*)regs - 16);
	do_exit(SIGKILL);
}
