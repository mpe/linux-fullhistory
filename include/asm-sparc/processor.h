/* include/asm-sparc/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC_PROCESSOR_H
#define __ASM_SPARC_PROCESSOR_H

/*
 * Bus types
 */
extern int EISA_bus;
#define MCA_bus 0

/*
 * User space process size: 3GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 *
 * "this is gonna have to change to 1gig for the sparc" - David S. Miller
 */
#define TASK_SIZE	0xc0000000

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

struct thread_struct {
	unsigned long ksp;          /* kernel stack pointer */
	unsigned long usp;          /* user's sp, throw reg windows here */
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

#endif /* __ASM_SPARC_PROCESSOR_H */

