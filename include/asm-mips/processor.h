/*
 * include/asm-mips/processor.h
 *
 * Copyright (C) 1994  Waldorf Electronics
 * written by Ralf Baechle
 */

#ifndef __ASM_MIPS_PROCESSOR_H
#define __ASM_MIPS_PROCESSOR_H

#if !defined (__ASSEMBLY__)
#include <asm/system.h>

/*
 * System setup and hardware bug flags..
 */
extern char wait_available;		/* only available on R4[26]00 */

extern unsigned long intr_count;
extern unsigned long event;

#if defined (__R4000__)

#define start_bh_atomic()        \
__asm__ __volatile__(            \
	".set\tnoreorder\n\t"    \
	".set\tnoat\n\t"         \
	"ll\t$1,(%0)\n"          \
	"1:\taddiu\t$1,$1,1\n\t" \
	"sc\t$1,(%0)\n\t"        \
	"beqzl\t$1,1b\n\t"       \
	"ll\t$1,(%0)\n\t"        \
	".set\tat\n\t"           \
	".set\treorder"          \
	: /* no outputs */       \
	: "r" (&intr_count));

#define end_bh_atomic()          \
__asm__ __volatile__(            \
	".set\tnoreorder\n\t"    \
	".set\tnoat\n\t"         \
	"ll\t$1,(%0)\n"          \
	"1:\tsubu\t$1,$1,1\n\t"  \
	"sc\t$1,(%0)\n\t"        \
	"beqzl\t$1,1b\n\t"       \
	"ll\t$1,(%0)\n\t"        \
	".set\tat\n\t"           \
	".set\treorder"          \
	: /* no outputs */       \
	: "r" (&intr_count));

#else /* !defined (__R4000__) */

#define start_bh_atomic() \
{int flags; save_flags(flags); cli(); intr_count++; restore_flags(flags)}

#define end_bh_atomic() \
{int flags; save_flags(flags); cli(); intr_count--; restore_flags(flags)}

#endif

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
 * so don't change it unless you know what you are doing.
 */
#define TASK_SIZE	(0x80000000UL)

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
	0, \
}

/*
 * If you change thread_struct remember to change the #defines below too!
 */
struct thread_struct {
        /*
         * saved main processor registers
         */
        unsigned long   reg16, reg17, reg18, reg19, reg20, reg21, reg22, reg23;
        unsigned long                               reg28, reg29, reg30, reg31;
	/*
	 * saved cp0 stuff
	 */
	unsigned long cp0_status;
	/*
	 * saved fpu/fpu emulator stuff
	 */
	union mips_fpu_union fpu;
	/*
	 * Other stuff associated with the process
	 */
	unsigned long cp0_badvaddr;
	unsigned long error_code;
	unsigned long trap_no;
	unsigned long ksp;		/* Top of kernel stack   */
	unsigned long fs;		/* "Segment" pointer     */
	unsigned long pg_dir;		/* L1 page table pointer */
};

#endif /* !defined (__ASSEMBLY__) */

/*
 * If you change the #defines remember to change thread_struct above too!
 */
#define TOFF_REG16		0
#define TOFF_REG17		(TOFF_REG16+4)
#define TOFF_REG18		(TOFF_REG17+4)
#define TOFF_REG19		(TOFF_REG18+4)
#define TOFF_REG20		(TOFF_REG19+4)
#define TOFF_REG21		(TOFF_REG20+4)
#define TOFF_REG22		(TOFF_REG21+4)
#define TOFF_REG23		(TOFF_REG22+4)
#define TOFF_REG28		(TOFF_REG23+4)
#define TOFF_REG29		(TOFF_REG28+4)
#define TOFF_REG30		(TOFF_REG29+4)
#define TOFF_REG31		(TOFF_REG30+4)
#define TOFF_CP0_STATUS		(TOFF_REG31+4)
/*
 * Pad for 8 byte boundary!
 */
#define TOFF_FPU		(((TOFF_CP0_STATUS+4)+(8-1))&~(8-1))
#define TOFF_CP0_BADVADDR	(TOFF_FPU+264)
#define TOFF_ERROR_CODE		(TOFF_CP0_BADVADDR+4)
#define TOFF_TRAP_NO		(TOFF_ERROR_CODE+4)
#define TOFF_KSP		(TOFF_TRAP_NO+4)
#define TOFF_FS			(TOFF_KSP+4)
#define TOFF_PG_DIR		(TOFF_FS+4)

#if !defined (__ASSEMBLY__)

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
	 * Other stuff associated with the process\
	 */ \
	0, 0, 0, (((unsigned long)init_kernel_stack)+4096-8), \
	KERNEL_DS, (unsigned long) swapper_pg_dir \
}

#ifdef __KERNEL__

/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
asmlinkage void resume(struct task_struct *tsk, int offset);

#define switch_to(n) \
	resume(n, ((int)(&((struct task_struct *)0)->tss)))

/*
 * Does the process account for user or for system time?
 */
#if defined (__R4000__)

#define USES_USER_TIME(regs) (!((regs)->cp0_status & 0x18))

#else /* !defined (__R4000__) */

#error "#define USES_USER_TIME(regs)!"

#endif /* !defined (__R4000__) */

#endif /* __KERNEL__ */

#endif /* !defined (__ASSEMBLY__) */

#endif /* __ASM_MIPS_PROCESSOR_H */
