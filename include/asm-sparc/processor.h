/* include/asm-sparc/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC_PROCESSOR_H
#define __ASM_SPARC_PROCESSOR_H

#include <linux/sched.h>  /* For intr_count */

#include <asm/ptrace.h>   /* For pt_regs declaration */

/*
 * Bus types
 */
#define EISA_bus 0
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * Write Protection works right in supervisor mode on the Sparc
 */
#if 0  /* Let's try this out ;) */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */
#else
extern char wp_works_ok;
#endif

/*
 * User space process size: 3GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 *
 * With the way identity mapping works on the sun4c, this is the best
 * value to use.
 *
 * This has to be looked into for a unified sun4c/sun4m task size.
 */
#define TASK_SIZE	(0xC000000UL)

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

/* The first four entries here MUST be the first four. This allows me to
 * do %lo(offset) loads and stores in entry.S. See TRAP_WIN_CLEAN to see
 * why.
 */

struct thread_struct {
	unsigned long uwindows;       /* how many user windows are in the set */
	unsigned long wim;            /* user's window invalid mask */
	unsigned long w_saved;        /* how many windows saved in reg_window[] */
	unsigned long ksp;            /* kernel stack pointer */
	unsigned long usp;            /* user's sp, throw reg windows here */
	unsigned long psr;            /* save for condition codes */
	unsigned long pc;             /* program counter */
	unsigned long npc;            /* next program counter */
	unsigned long yreg;
	unsigned long align;          /* to get 8-byte alignment  XXX  */
	unsigned long reg_window[16];
	unsigned long pgd_ptr;
	int context;                  /* The context allocated to this thread */

/* 8 local registers + 8 in registers * 24 register windows.
 * Most sparcs I know of only have 7 or 8 windows implemented,
 * we determine how many at boot time and store that value
 * in nwindows.
 */
	unsigned long float_regs[64]; /* V8 and below have 32, V9 has 64 */
};

#define INIT_MMAP { &init_task, (PAGE_OFFSET), (0xff000000UL), \
		    0x0 , VM_READ | VM_WRITE | VM_EXEC }

#define INIT_TSS  { \
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, }, \
	(long) &swapper_pg_dir, -1,  \
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
 * about the next time flow control goes back to that process.
 */

/* Sparc stack save area allocated for each save, not very exciting. */
struct sparc_save_stack {
  unsigned int locals[8];
  unsigned int ins[8];
  unsigned int padd[8];
};

/*
 * These are the "cli()" and "sti()" for software interrupts
 * They work by increasing/decreasing the "intr_count" value, 
 * and as such can be nested arbitrarily.
 */
extern inline void start_bh_atomic(void)
{
	__asm__ __volatile__("rd %%psr, %%g2\n\t"
			     "wr %%g2, 0x20, %%psr\n\t"  /* disable traps */
			     "ld %0,%%g3\n\t"
			     "add %%g3,1,%%g3\n\t"
			     "st %%g3,%0\n\t"
			     "wr %%g2, 0x0, %%psr\n\t"   /* enable traps */
			     : "=m" (intr_count)
			     : : "g2", "g3", "memory");
}

extern inline void end_bh_atomic(void)
{
	__asm__ __volatile__("rd %%psr, %%g2\n\t"
			     "wr %%g2, 0x20, %%psr\n\t"
			     "ld %0,%%g3\n\t"
			     "sub %%g3,1,%%g3\n\t"
			     "st %%g3,%0\n\t"
			     "wr %%g2, 0x0, %%psr\n\t"
			     : "=m" (intr_count)
			     : : "g2", "g3", "memory");
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
static inline void start_thread(struct pt_regs * regs, unsigned long sp,
				unsigned long fp)
{
  printk("start_thread called, halting..n");
  halt();
}

#endif /* __ASM_SPARC_PROCESSOR_H */

