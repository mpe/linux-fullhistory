/*
 *  arch/mips/mm/fault.c
 *
 *  Copyright (C) 1995 by Ralf Baechle
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

extern void die_if_kernel(char *, struct pt_regs *, long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void
do_page_fault(struct pt_regs *regs, unsigned long writeaccess, unsigned long address)
{
	struct vm_area_struct * vma;

#if 0
	printk("do_page_fault() #1: %s %08lx (epc == %08lx, ra == %08lx)\n",
	       writeaccess ? "writeaccess to" : "readaccess from",
	       address, regs->cp0_epc, regs->reg31);
#endif
	vma = find_vma(current, address);
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
	handle_mm_fault(vma, address, writeaccess);
	/* FIXME: This flushes the cache far to often */
	sys_cacheflush(address, PAGE_SIZE, BCACHE);

        return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	if (user_mode(regs)) {
		current->tss.cp0_badvaddr = address;
		current->tss.error_code = writeaccess;
		send_sig(SIGSEGV, current, 1);
		return;
	}
	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	printk(KERN_ALERT "Unable to handle kernel paging request at virtual "
	       "address %08lx\n", address);
	die_if_kernel("Oops", regs, writeaccess);
	do_exit(SIGKILL);
}
