/*
 * include/asm-alpha/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_ALPHA_PROCESSOR_H
#define __ASM_ALPHA_PROCESSOR_H

/*
 * We have a 8GB user address space to start with: 33 bits of vm
 * can be handled with just 2 page table levels.
 *
 * Eventually, this should be bumped to 40 bits or so..
 */
#define TASK_SIZE (0x200000000UL)

/*
 * Bus types
 */
extern int EISA_bus;
#define MCA_bus 0

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

#endif /* __ASM_ALPHA_PROCESSOR_H */
