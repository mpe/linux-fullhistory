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
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

#ifdef __SMP__
unsigned long last_asn[NR_CPUS] = { /* gag */
  ASN_FIRST_VERSION +  (0 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (1 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (2 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (3 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (4 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (5 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (6 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (7 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (8 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION +  (9 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (10 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (11 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (12 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (13 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (14 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (15 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (16 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (17 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (18 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (19 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (20 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (21 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (22 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (23 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (24 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (25 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (26 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (27 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (28 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (29 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (30 << WIDTH_HARDWARE_ASN),
  ASN_FIRST_VERSION + (31 << WIDTH_HARDWARE_ASN)
};
#else
unsigned long asn_cache = ASN_FIRST_VERSION;
#endif /* __SMP__ */

#ifndef BROKEN_ASN
/*
 * Select a new ASN and reload the context. This is
 * not inlined as this expands to a pretty large
 * function.
 */
void get_new_asn_and_reload(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context = 0;
	get_new_mmu_context(tsk, mm);
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

	/* As of EV6, a load into $31/$f31 is a prefetch, and never faults
	   (or is suppressed by the PALcode).  Support that for older CPUs
	   by ignoring such an instruction.  */
	if (cause == 0) {
		unsigned int insn;
		__get_user(insn, (unsigned int *)regs->pc);
		if ((insn >> 21 & 0x1f) == 0x1f &&
		    /* ldq ldl ldt lds ldg ldf ldwu ldbu */
		    (1ul << (insn >> 26) & 0x30f00001400ul)) {
			regs->pc += 4;
			return;
		}
	}

	down(&mm->mmap_sem);
	lock_kernel();
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
	goto out;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);

	if (user_mode(regs)) {
		force_sig(SIGSEGV, current);
		goto out;
	}

	/* Are we prepared to handle this fault as an exception?  */
	if ((fixup = search_exception_table(regs->pc)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(dpf_reg, fixup, regs->pc);
		printk("%s: Exception at [<%lx>] (%lx)\n",
		       current->comm, regs->pc, newpc);
		regs->pc = newpc;
		goto out;
	}

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	printk(KERN_ALERT "Unable to handle kernel paging request at "
	       "virtual address %016lx\n", address);
	die_if_kernel("Oops", regs, cause, (unsigned long*)regs - 16);
	do_exit(SIGKILL);
 out:
	unlock_kernel();
}

