/*
 * include/asm-alpha/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_ALPHA_PROCESSOR_H
#define __ASM_ALPHA_PROCESSOR_H

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

#endif /* __ASM_ALPHA_PROCESSOR_H */
