/*
 * include/asm-mips/processor.h
 *
 * Copyright (C) 1994  Waldorf Electronics
 * written by Ralf Baechle
 * Modified further for R[236]000 compatibility by Paul M. Antoine
 *
 * $Id: processor.h,v 1.5 1997/12/01 16:48:39 ralf Exp $
 */
#ifndef __ASM_MIPS_PROCESSOR_H
#define __ASM_MIPS_PROCESSOR_H

#if !defined (__LANGUAGE_ASSEMBLY__)
#include <asm/cachectl.h>
#include <asm/mipsregs.h>
#include <asm/reg.h>
#include <asm/system.h>

/*
 * System setup and hardware flags..
 */
extern char wait_available;		/* only available on R4[26]00 */
extern char cyclecounter_available;	/* only available from R4000 upwards. */
extern char dedicated_iv_available;	/* some embedded MIPS like Nevada */

/*
 * Bus types (default is ISA, but people can check others with these..)
 * MCA_bus hardcoded to 0 for now.
 *
 * This needs to be extended since MIPS systems are being delivered with
 * numerous different types of bus systems.
 */
extern int EISA_bus;
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

/*
 * MIPS has no problems with write protection
 */
#define wp_works_ok 1
#define wp_works_ok__is_a_macro /* for versions in ksyms.c */

/*
 * User space process size: 2GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.  TASK_SIZE
 * for a 64 bit kernel expandable to 8192EB, of which the current MIPS
 * implementations will "only" be able to use 1TB ...
 */
#define TASK_SIZE	(0x80000000UL)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32

#define NUM_FPU_REGS	32

struct mips_fpu_hard_struct {
	double fp_regs[NUM_FPU_REGS];
	unsigned int control;
};

/*
 * FIXME: no fpu emulator yet (but who cares anyway?)
 */
struct mips_fpu_soft_struct {
	long	dummy;
};

union mips_fpu_union {
        struct mips_fpu_hard_struct hard;
        struct mips_fpu_soft_struct soft;
};

#define INIT_FPU { \
	{{0,},} \
}

typedef struct {
	unsigned long seg;
} mm_segment_t;

/*
 * If you change thread_struct remember to change the #defines below too!
 */
struct thread_struct {
        /* Saved main processor registers. */
        unsigned long reg16 __attribute__ ((aligned (8)));
	unsigned long reg17, reg18, reg19, reg20, reg21, reg22, reg23;
        unsigned long reg28, reg29, reg30, reg31;

	/* Saved cp0 stuff. */
	unsigned long cp0_status;

	/* Saved fpu/fpu emulator stuff. */
	union mips_fpu_union fpu __attribute__ ((aligned (8)));

	/* Other stuff associated with the thread. */
	unsigned long cp0_badvaddr;
	unsigned long error_code;
	unsigned long trap_no;
	unsigned long ksp;			/* Top of kernel stack   */
	unsigned long pg_dir;                   /* used in tlb refill    */
#define MF_FIXADE 1			/* Fix address errors in software */
#define MF_LOGADE 2			/* Log address errors to syslog */
	unsigned long mflags;
	mm_segment_t current_ds;
	unsigned long irix_trampoline;  /* Wheee... */
	unsigned long irix_oldctx;
};

#endif /* !defined (__LANGUAGE_ASSEMBLY__) */

#define INIT_MMAP { &init_mm, KSEG0, KSEG1, PAGE_SHARED, \
                    VM_READ | VM_WRITE | VM_EXEC, NULL, &init_mm.mmap }

#define INIT_TSS  { \
        /* \
         * saved main processor registers \
         */ \
	0, 0, 0, 0, 0, 0, 0, 0, \
	            0, 0, 0, 0, \
	/* \
	 * saved cp0 stuff \
	 */ \
	0, \
	/* \
	 * saved fpu/fpu emulator stuff \
	 */ \
	INIT_FPU, \
	/* \
	 * Other stuff associated with the process \
	 */ \
	0, 0, 0, (unsigned long)&init_task_union + KERNEL_STACK_SIZE - 32, \
	(unsigned long) swapper_pg_dir, \
	/* \
	 * For now the default is to fix address errors \
	 */ \
	MF_FIXADE, 0, 0, 0 \
}

#ifdef __KERNEL__

#define KERNEL_STACK_SIZE 8192

#if !defined (__LANGUAGE_ASSEMBLY__)

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/*
 * Return saved PC of a blocked thread.
 */
extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	extern void ret_from_sys_call(void);

	/* New born processes are a special case */
	if (t->reg31 == (unsigned long) ret_from_sys_call)
		return t->reg31;

	return ((unsigned long*)t->reg29)[17];
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
extern void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long sp);

/*
 * Does the process account for user or for system time?
 */
extern int (*running_in_user_mode)(void);
#define USES_USER_TIME(regs) running_in_user_mode()

/* Allocation and freeing of basic task resources. */
/*
 * NOTE! The task struct and the stack go together
 */
#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL,1,0))
#define free_task_struct(p)	free_pages((unsigned long)(p),1)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

#endif /* !defined (__LANGUAGE_ASSEMBLY__) */
#endif /* __KERNEL__ */

/*
 * Return_address is a replacement for __builtin_return_address(count)
 * which on certain architectures cannot reasonably be implemented in GCC
 * (MIPS, Alpha) or is unuseable with -fomit-frame-pointer (i386).
 * Note that __builtin_return_address(x>=1) is forbidden because GCC
 * aborts compilation on some CPUs.  It's simply not possible to unwind
 * some CPU's stackframes.
 */
#if (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8))
/*
 * __builtin_return_address works only for non-leaf functions.  We avoid the
 * overhead of a function call by forcing the compiler to save the return
 * address register on the stack.
 */
#define return_address() ({__asm__ __volatile__("":::"$31");__builtin_return_address(0);})
#else
/*
 * __builtin_return_address is not implemented at all.  Calling it
 * will return senseless values.  Return NULL which at least is an obviously
 * senseless value.
 */
#define return_address() NULL
#endif

#endif /* __ASM_MIPS_PROCESSOR_H */
