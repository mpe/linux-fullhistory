/*
 * include/asm-alpha/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_ALPHA_PROCESSOR_H
#define __ASM_ALPHA_PROCESSOR_H

/*
 * We have a 41-bit user address space: 2TB user VM...
 */
#define TASK_SIZE (0x40000000000UL)

/*
 * Bus types
 */
#define EISA_bus 1
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * The alpha has no problems with write protection
 */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

struct thread_struct {
	unsigned long ksp;
	unsigned long usp;
	unsigned long ptbr;
	unsigned int pcc;
	unsigned int asn;
	unsigned long unique;
	/*
	 * bit  0.. 0: floating point enable (used by PALcode)
	 * bit  1.. 5: IEEE_TRAP_ENABLE bits (see fpu.h)
	 */
	unsigned long flags;
	unsigned long res1, res2;
};

#define INIT_MMAP { &init_mm, 0xfffffc0000000000,  0xfffffc0010000000, \
	PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
	0, 0, 0, \
	0, 0, 0, \
	0, 0, 0, \
}

#define alloc_kernel_stack()    get_free_page(GFP_KERNEL)
#define free_kernel_stack(page) free_page((page))

#include <asm/ptrace.h>

/*
 * Return saved PC of a blocked thread.  This assumes the frame
 * pointer is the 6th saved long on the kernel stack and that the
 * saved return address is the first long in the frame.  This all
 * holds provided the thread blocked through a call to schedule() ($15
 * is the frame pointer in schedule() and $15 is saved at offset 48 by
 * entry.S:do_switch_stack).
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	unsigned long fp;

	fp = ((unsigned long*)t->ksp)[6];
	return *(unsigned long*)fp;
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
static inline void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	regs->pc = pc;
	regs->ps = 8;
	wrusp(sp);
}

#endif /* __ASM_ALPHA_PROCESSOR_H */
