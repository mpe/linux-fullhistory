/*
 *  arch/mips/mm/fault.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to MIPS by Ralf Baechle
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
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/mipsconfig.h>

extern void die_if_kernel(char *, struct pt_regs *, long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	struct vm_area_struct * vma;
	unsigned long address;
	unsigned long page;

	/* get the address */
	__asm__("dmfc0\t%0,$8"
	        : "=r" (address));

	vma = find_vma(current, address);
	if (!vma)
		goto bad_area;
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
#if 0
	if (regs->eflags & VM_MASK) {
		unsigned long bit = (address - 0xA0000) >> PAGE_SHIFT;
		if (bit < 32)
			current->tss.screen_bitmap |= 1 << bit;
	}
#endif
	if (!(vma->vm_page_prot & PAGE_USER))
		goto bad_area;
	if (error_code & PAGE_PRESENT) {
		if (!(vma->vm_page_prot & (PAGE_RW | PAGE_COW)))
			goto bad_area;
		do_wp_page(vma, address, error_code);
		return;
	}
printk("do_page_fault: do_no_page(%x, %x, %d)", vma, address, error_code);
	do_no_page(vma, address, error_code);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
printk("Bad Area...\n");
	if (error_code & PAGE_USER) {
		current->tss.cp0_badvaddr = address;
		current->tss.error_code = error_code;
		current->tss.trap_no = 14;
		send_sig(SIGSEGV, current, 1);
		return;
	}
/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	printk("This processor honours the WP bit even when in supervisor mode. Good.\n");
	if ((unsigned long) (address-TASK_SIZE) < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
		pg0[0] = PAGE_SHARED;
	} else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	page = current->tss.pg_dir;
	printk(KERN_ALERT "current->tss.pg_dir = %08lx\n", page);
	page = ((unsigned long *) page)[address >> 22];
	printk(KERN_ALERT "*pde = %08lx\n", page);
	if (page & PAGE_PRESENT) {
		page &= PAGE_MASK;
		address &= 0x003ff000;
		page = ((unsigned long *) page)[address >> PAGE_SHIFT];
		printk(KERN_ALERT "*pte = %08lx\n", page);
	}
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
}
