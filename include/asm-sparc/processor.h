/* include/asm-sparc/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC_PROCESSOR_H
#define __ASM_SPARC_PROCESSOR_H

/*
 * Bus types
 */
#define EISA_bus 1
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * Write Protection works right in supervisor mode on the Sparc
 */

#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/*
 * User space process size: 3GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 *
 * "this is gonna have to change to 1gig for the sparc" - David S. Miller
 */
#define TASK_SIZE	(0xC0000000UL)

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

/* The first five entries here MUST be the first four. This allows me to
 * do %lo(offset) loads and stores in entry.S. See TRAP_WIN_CLEAN to see
 * why.
 */

struct thread_struct {
	unsigned long uwindows;       /* how many user windows are in the set */
	unsigned long wim;            /* user's window invalid mask */
	unsigned long w_saved;        /* how many windows saved in reg_window[] */
	unsigned long ksp;          /* kernel stack pointer */
	unsigned long usp;          /* user's sp, throw reg windows here */
	unsigned long psr;          /* save for condition codes */
	unsigned long reg_window[16*24];
	unsigned long cr3;          /* why changed from ptbr? */
	unsigned int pcc;
	unsigned int asn;
	unsigned long unique;
	unsigned long flags;
	unsigned long res1, res2;
	unsigned long pc;           /* program counter */
	unsigned long npc;          /* next program counter */

/* 8 local registers + 8 in registers * 24 register windows.
 * Most sparcs I know of only have 8 windows implemented,
 * we determine how many at boot time and store that value
 * in nwindows.
 */
	unsigned long globl_regs[8];  /* global regs need to be saved too */
	unsigned long yreg;
	unsigned long float_regs[64]; /* V8 and below have 32, V9 has 64 */
};

#define INIT_MMAP { &init_task, 0x0, 0x40000000, \
		      PAGE_SHARED , VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
	0, 0, 0, 0, 0, 0, \
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }, \
	0, 0, 0, 0, 0, 0, 0, 0, 0, \
        { 0, 0, 0, 0, 0, 0, 0, 0, }, \
        0, \
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }, \
}

/* The thread_frame is what needs to be set up in certain circumstances
 * upon entry to a trap. It is also loaded sometimes during a window
 * spill if things don't go right (bad user stack pointer). In reality
 * it is not per-process per se, it just sits in the kernel stack while
 * the current process is in a handler then it is basically forgotten
 * about.
 */

struct thread_frame {
  unsigned int thr_psr;
  unsigned int thr_pc;
  unsigned int thr_npc;
  unsigned int thr_y;
  unsigned int thr_globals[8];
  unsigned int thr_outs[8];
};

/*
 * These are the "cli()" and "sti()" for software interrupts
 * They work by increasing/decreasing the "intr_count" value, 
 * and as such can be nested arbitrarily.
 */
extern inline void start_bh_atomic(void)
{
	unsigned long dummy, psr;
	__asm__ __volatile__("rd %%psr, %2\n\t"
			     "wr %2, 0x20, %%psr\n\t"  /* disable traps */
			     "ld %1,%0\n\t"
			     "add %0,1,%0\n\t"
			     "st %0,%1\n\t"
			     "wr %2, 0x0, %%psr\n\t"   /* enable traps */
			     : "=r" (dummy), "=m" (intr_count)
			     : "0" (0), "r" (psr=0));
}

extern inline void end_bh_atomic(void)
{
	unsigned long dummy, psr;
	__asm__ __volatile__("rd %%psr, %2\n\t"
			     "wr %2, 0x20, %%psr\n\t"
			     "ld %1,%0\n\t"
			     "sub %0,1,%0\n\t"
			     "st %0,%1\n\t"
			     "wr %2, 0x0, %%psr\n\t"
			     : "=r" (dummy), "=m" (intr_count)
			     : "0" (0), "r" (psr=0));
}

#endif /* __ASM_SPARC_PROCESSOR_H */

