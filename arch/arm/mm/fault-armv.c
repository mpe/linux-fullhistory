/*
 *  linux/arch/arm/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995, 1996 Russell King
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

#define FAULT_CODE_READ		0x02
#define FAULT_CODE_USER		0x01

struct pgtable_cache_struct quicklists;

void __bad_pmd(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	set_pmd(pmd, mk_user_pmd(BAD_PAGETABLE));
}

void __bad_pmd_kernel(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	set_pmd(pmd, mk_kernel_pmd(BAD_PAGETABLE));
}

pgd_t *get_pgd_slow(void)
{
	/*
	 * need to get a 16k page for level 1
	 */
	pgd_t *pgd = (pgd_t *) __get_free_pages(GFP_KERNEL,2);
	pgd_t *init;
	
	if (pgd) {
		init = pgd_offset(&init_mm, 0);
		memzero ((void *)pgd, USER_PTRS_PER_PGD * BYTES_PER_PTR);
		memcpy (pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * BYTES_PER_PTR);
	}
	return pgd;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) get_small_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero (pte, PTRS_PER_PTE * BYTES_PER_PTR);
			set_pmd(pmd, mk_user_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_user_pmd(BAD_PAGETABLE));
		return NULL;
	}
	free_small_page ((unsigned long) pte);
	if (pmd_bad(*pmd)) {
		__bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) get_small_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero (pte, PTRS_PER_PTE * BYTES_PER_PTR);
			set_pmd(pmd, mk_kernel_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_kernel_pmd(BAD_PAGETABLE));
		return NULL;
	}
	free_small_page ((unsigned long) pte);
	if (pmd_bad(*pmd)) {
		__bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

extern void die_if_kernel(char *msg, struct pt_regs *regs, unsigned int err, unsigned int ret);

#ifdef DEBUG
static int sp_valid (unsigned long *sp)
{
	unsigned long addr = (unsigned long) sp;

	if (addr >= 0xb0000000 && addr < 0xd0000000)
		return 1;
	if (addr >= 0x03ff0000 && addr < 0x04000000)
		return 1;
	return 0;
}
#endif

static void kernel_page_fault (unsigned long addr, int mode, struct pt_regs *regs,
			       struct task_struct *tsk, struct mm_struct *mm)
{
	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	pgd_t *pgd;
	if (addr < PAGE_SIZE)
	    printk (KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
	    printk (KERN_ALERT "Unable to handle kernel paging request");
	printk (" at virtual address %08lx\n", addr);
	printk (KERN_ALERT "current->tss.memmap = %08lX\n", tsk->tss.memmap);
	pgd = pgd_offset (mm, addr);
	printk (KERN_ALERT "*pgd = %08lx", pgd_val (*pgd));
	if (!pgd_none (*pgd)) {
		pmd_t *pmd;
		pmd = pmd_offset (pgd, addr);
		printk (", *pmd = %08lx", pmd_val (*pmd));
		if (!pmd_none (*pmd))
			printk (", *pte = %08lx", pte_val (*pte_offset (pmd, addr)));
	}
	printk ("\n");
	die_if_kernel ("Oops", regs, mode, SIGKILL);
	do_exit (SIGKILL);
}

static void page_fault (unsigned long addr, int mode, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long fixup;

	lock_kernel();
	tsk = current;
	mm = tsk->mm;

	down(&mm->mmap_sem);
	vma = find_vma (mm, addr);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= addr)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN) || expand_stack (vma, addr))
		goto bad_area;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	if (mode & FAULT_CODE_READ) { /* read? */
		if (!(vma->vm_flags & (VM_READ|VM_EXEC)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}
	handle_mm_fault (tsk, vma, addr & PAGE_MASK, !(mode & FAULT_CODE_READ));
	up(&mm->mmap_sem);
	goto out;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up(&mm->mmap_sem);
	if (mode & FAULT_CODE_USER) {
		tsk->tss.error_code = mode;
		tsk->tss.trap_no = 14;
		printk ("%s: memory violation at pc=0x%08lx, lr=0x%08lx (bad address=0x%08lx, code %d)\n",
			tsk->comm, regs->ARM_pc, regs->ARM_lr, addr, mode);
#ifdef DEBUG
		{
			unsigned int i, j;
			unsigned long *sp = (unsigned long *) (regs->ARM_sp - 128);
			for (j = 0; j < 20 && sp_valid (sp); j++) {
				printk ("%p: ", sp);
				for (i = 0; i < 8 && sp_valid (sp); i += 1, sp++)
					printk ("%08lx ", *sp);
				printk ("\n");
			}
		}
		show_regs (regs);
		c_backtrace (regs->ARM_fp, regs->ARM_cpsr);
#endif
		force_sig(SIGSEGV, tsk);
		goto out;
	}

	/* Are we prepared to handle this kernel fault?  */
	if ((fixup = search_exception_table(instruction_pointer(regs))) != 0) {
		printk(KERN_DEBUG "%s: Exception at [<%lx>] addr=%lx (fixup: %lx)\n",
			tsk->comm, regs->ARM_pc, addr, fixup);
		regs->ARM_pc = fixup;
		goto out;
	}

	kernel_page_fault (addr, mode, regs, tsk, mm);
out:
	unlock_kernel();
}

/*
 * Handle a data abort.  Note that we have to handle a range of addresses
 * on ARM2/3 for ldm.  If both pages are zero-mapped, then we have to force
 * a copy-on-write
 */
asmlinkage void
do_DataAbort (unsigned long addr, int fsr, int error_code, struct pt_regs *regs)
{
	if (user_mode(regs))
		error_code |= FAULT_CODE_USER;
#define DIE(signr,nam)\
		force_sig(signr, current);\
		die_if_kernel(nam, regs, fsr, signr);\
		break;

	switch (fsr & 15) {
	case 2:
		DIE(SIGKILL, "Terminal exception")
	case 0:
		DIE(SIGSEGV, "Vector exception")
	case 1:
	case 3:
		DIE(SIGBUS, "Alignment exception")
	case 12:
	case 14:
		DIE(SIGBUS, "External abort on translation")
	case 9:
	case 11:
		DIE(SIGSEGV, "Domain fault")
	case 13:/* permission fault on section */
#ifdef DEBUG
		{
			unsigned int i, j;
			unsigned long *sp;

			printk ("%s: section permission fault (bad address=0x%08lx, code %d)\n",
				current->comm, addr, error_code);
			sp = (unsigned long *) (regs->ARM_sp - 128);
			for (j = 0; j < 20 && sp_valid (sp); j++) {
				printk ("%p: ", sp);
				for (i = 0; i < 8 && sp_valid (sp); i += 1, sp++)
					printk ("%08lx ", *sp);
				printk ("\n");
			}
			show_regs (regs);
			c_backtrace(regs->ARM_fp, regs->ARM_cpsr);
		}
#endif
		DIE(SIGSEGV, "Permission fault")

	case 15:/* permission fault on page */
	case 5:	/* page-table entry descriptor fault */
	case 7:	/* first-level descriptor fault */
		page_fault (addr, error_code, regs);
		break;
	case 4:
	case 6:
		DIE(SIGBUS, "External abort on linefetch")
	case 8:
	case 10:
		DIE(SIGBUS, "External abort on non-linefetch")
	}
}

asmlinkage int
do_PrefetchAbort (unsigned long addr, struct pt_regs *regs)
{
#if 0
	/* does this still apply ? */
	if (the memc mapping for this page exists - can check now...) {
		printk ("Page in, but got abort (undefined instruction?)\n");
		return 0;
	}
#endif
	page_fault (addr, FAULT_CODE_USER|FAULT_CODE_READ, regs);
	return 1;
}

