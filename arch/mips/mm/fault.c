/* $Id: fault.c,v 1.12 1998/10/19 21:27:37 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 */
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/version.h>

#include <asm/hardirq.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/softirq.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define development_version (LINUX_VERSION_CODE & 0x100)

extern void die(char *, struct pt_regs *, unsigned long write);

unsigned long asid_cache;

/*
 * Macro for exception fixup code to access integer registers.
 */
#define dpf_reg(r) (regs->regs[r])

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long writeaccess,
			      unsigned long address)
{
	struct vm_area_struct * vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	unsigned long fixup;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || mm == &init_mm)
		goto no_context;
#if 0
	printk("[%s:%d:%08lx:%ld:%08lx]\n", current->comm, current->pid,
	       address, writeaccess, regs->cp0_epc);
#endif
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
	if (writeaccess) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	handle_mm_fault(tsk, vma, address, writeaccess);
	up(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);

	if (user_mode(regs)) {
		tsk->tss.cp0_badvaddr = address;
		tsk->tss.error_code = writeaccess;
#if 0
		printk("do_page_fault() #2: sending SIGSEGV to %s for illegal %s\n"
		       "%08lx (epc == %08lx, ra == %08lx)\n",
		       tsk->comm,
		       writeaccess ? "writeaccess to" : "readaccess from",
		       address,
		       (unsigned long) regs->cp0_epc,
		       (unsigned long) regs->regs[31]);
#endif
		force_sig(SIGSEGV, tsk);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_table(regs->cp0_epc);
	if (fixup) {
		long new_epc;

		tsk->tss.cp0_baduaddr = address;
		new_epc = fixup_exception(dpf_reg, fixup, regs->cp0_epc);
		if (development_version)
			printk(KERN_DEBUG "%s: Exception at [<%lx>] (%lx)\n",
			       tsk->comm, regs->cp0_epc, new_epc);
		regs->cp0_epc = new_epc;
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	printk(KERN_ALERT "Unable to handle kernel paging request at virtual "
	       "address %08lx, epc == %08lx, ra == %08lx\n",
	       address, regs->cp0_epc, regs->regs[31]);
	die("Oops", regs, writeaccess);
	do_exit(SIGKILL);
}
