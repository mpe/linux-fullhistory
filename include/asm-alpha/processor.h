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
#define TASK_SIZE (0x20000000000UL)

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
	unsigned long flags;
	unsigned long res1, res2;
};

#define INIT_MMAP { &init_task, 0xfffffc0000300000,  0xfffffc0010000000, \
	PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
	0, 0, 0, \
	0, 0, 0, \
	0, 0, 0, \
}

/*
 * These are the "cli()" and "sti()" for software interrupts
 * They work by increasing/decreasing the "intr_count" value, 
 * and as such can be nested arbitrarily.
 */
extern inline void start_bh_atomic(void)
{
	unsigned long dummy;
	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%1\n\t"
		"addq %0,1,%0\n\t"
		"stq_c %0,%1\n\t"
		"beq %0,1b\n"
		: "=r" (dummy), "=m" (intr_count)
		: "0" (0));
}

extern inline void end_bh_atomic(void)
{
	unsigned long dummy;
	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%1\n\t"
		"subq %0,1,%0\n\t"
		"stq_c %0,%1\n\t"
		"beq %0,1b\n"
		: "=r" (dummy), "=m" (intr_count)
		: "0" (0));
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
static inline void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp)
{
	regs->pc = pc;
	wrusp(sp);
}

#endif /* __ASM_ALPHA_PROCESSOR_H */
