/* $Id: system.h,v 1.43 1996/12/10 06:06:37 davem Exp $ */
#ifndef __SPARC_SYSTEM_H
#define __SPARC_SYSTEM_H

#include <linux/kernel.h>

#include <asm/segment.h>

#ifdef __KERNEL__
#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/psr.h>
#endif

#define EMPTY_PGT       (&empty_bad_page)
#define EMPTY_PGE	(&empty_bad_page_table)

#ifndef __ASSEMBLY__

/*
 * Sparc (general) CPU types
 */
enum sparc_cpu {
  sun4        = 0x00,
  sun4c       = 0x01,
  sun4m       = 0x02,
  sun4d       = 0x03,
  sun4e       = 0x04,
  sun4u       = 0x05, /* V8 ploos ploos */
  sun_unknown = 0x06,
};

extern enum sparc_cpu sparc_cpu_model;

extern unsigned long empty_bad_page;
extern unsigned long empty_bad_page_table;
extern unsigned long empty_zero_page;

extern struct linux_romvec *romvec;
#define halt() romvec->pv_halt()

/* When a context switch happens we must flush all user windows so that
 * the windows of the current process are flushed onto its stack. This
 * way the windows are all clean for the next process and the stack
 * frames are up to date.
 */
extern void flush_user_windows(void);
extern void synchronize_user_stack(void);
extern void fpsave(unsigned long *fpregs, unsigned long *fsr,
		   void *fpqueue, unsigned long *fpqdepth);

#ifdef __SMP__
#define SWITCH_ENTER \
	cli(); \
	if(prev->flags & PF_USEDFPU) { \
		fpsave(&prev->tss.float_regs[0], &prev->tss.fsr, \
		       &prev->tss.fpqueue[0], &prev->tss.fpqdepth); \
		prev->flags &= ~PF_USEDFPU; \
		prev->tss.kregs->psr &= ~PSR_EF; \
	} \
	prev->lock_depth = syscall_count; \
	kernel_counter += (next->lock_depth - prev->lock_depth); \
	syscall_count = next->lock_depth;

#define SWITCH_EXIT sti();
#define SWITCH_DO_LAZY_FPU
#else
#define SWITCH_ENTER
#define SWITCH_EXIT
#define SWITCH_DO_LAZY_FPU if(last_task_used_math != next) next->tss.kregs->psr&=~PSR_EF;
#endif

	/* Much care has gone into this code, do not touch it. */
#define switch_to(prev, next) do {							\
	__label__ here;									\
	register unsigned long task_pc asm("o7");					\
	SWITCH_ENTER									\
	SWITCH_DO_LAZY_FPU								\
	__asm__ __volatile__(								\
	".globl\tflush_patch_switch\nflush_patch_switch:\n\t"				\
	"save %sp, -0x40, %sp; save %sp, -0x40, %sp; save %sp, -0x40, %sp\n\t"		\
	"save %sp, -0x40, %sp; save %sp, -0x40, %sp; save %sp, -0x40, %sp\n\t"		\
	"save %sp, -0x40, %sp\n\t"							\
	"restore; restore; restore; restore; restore; restore; restore");		\
	if(!(next->tss.flags & SPARC_FLAG_KTHREAD) &&					\
	   !(next->flags & PF_EXITING))							\
		switch_to_context(next);						\
	task_pc = ((unsigned long) &&here) - 0x8;					\
	__asm__ __volatile__(								\
	"rd\t%%psr, %%g4\n\t"								\
	"nop\n\t"									\
	"nop\n\t"									\
	"nop\n\t"									\
	"std\t%%sp, [%%g6 + %3]\n\t"							\
	"rd\t%%wim, %%g5\n\t"								\
	"wr\t%%g4, 0x20, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"nop\n\t"									\
	"std\t%%g4, [%%g6 + %2]\n\t"							\
	"mov\t%1, %%g6\n\t"								\
	"ldd\t[%%g6 + %2], %%g4\n\t"							\
	"st\t%1, [%0]\n\t"								\
	"wr\t%%g4, 0x20, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"nop\n\t"									\
	"ldd\t[%%g6 + %3], %%sp\n\t"							\
	"wr\t%%g5, 0x0, %%wim\n\t"							\
	"ldd\t[%%sp + 0x00], %%l0\n\t"							\
	"ldd\t[%%sp + 0x08], %%l2\n\t"							\
	"ldd\t[%%sp + 0x10], %%l4\n\t"							\
	"ldd\t[%%sp + 0x18], %%l6\n\t"							\
	"ldd\t[%%sp + 0x20], %%i0\n\t"							\
	"ldd\t[%%sp + 0x28], %%i2\n\t"							\
	"ldd\t[%%sp + 0x30], %%i4\n\t"							\
	"ldd\t[%%sp + 0x38], %%i6\n\t"							\
	"wr\t%%g4, 0x0, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"nop\n\t"									\
	"jmpl\t%%o7 + 0x8, %%g0\n\t"							\
	" nop\n\t" : : "r" (&(current_set[smp_processor_id()])), "r" (next),		\
	"i" ((const unsigned long)(&((struct task_struct *)0)->tss.kpsr)),		\
	"i" ((const unsigned long)(&((struct task_struct *)0)->tss.ksp)),		\
	"r" (task_pc) : "g4", "g5");							\
here: SWITCH_EXIT } while(0)

/* Changing the IRQ level on the Sparc.   We now avoid writing the psr
 * whenever possible.
 */
extern __inline__ void setipl(unsigned long __orig_psr)
{
	__asm__ __volatile__("
		wr	%0, 0x0, %%psr
		nop
		nop
		nop
"		: /* no outputs */
		: "r" (__orig_psr)
		: "memory");
}

extern __inline__ void cli(void)
{
	unsigned long tmp;

	__asm__ __volatile__("
		rd	%%psr, %0
		nop
		nop
		nop
		andcc	%0, %1, %%g0
		bne	1f
		 nop
		wr	%0, %1, %%psr
		nop
		nop
		nop
1:
"		: "=r" (tmp)
		: "i" (PSR_PIL)
		: "memory");
}

extern __inline__ void sti(void)
{
	unsigned long tmp;

	__asm__ __volatile__("
		rd	%%psr, %0
		nop
		nop
		nop
		andcc	%0, %1, %%g0
		be	1f
		 nop
		wr	%0, %1, %%psr
		nop
		nop
		nop
1:
"		: "=r" (tmp)
		: "i" (PSR_PIL)
		: "memory");
}

extern __inline__ unsigned long getipl(void)
{
	unsigned long retval;

	__asm__ __volatile__("
		rd	%%psr, %0
		nop
		nop
		nop
"	: "=r" (retval));
	return retval;
}

extern __inline__ unsigned long swap_pil(unsigned long __new_psr)
{
	unsigned long retval, tmp1, tmp2;

	__asm__ __volatile__("
		rd	%%psr, %0
		nop
		nop
		nop
		and	%0, %4, %1
		and	%3, %4, %2
		xorcc	%1, %2, %%g0
		be	1f
		 nop
		wr %0, %4, %%psr
		nop
		nop
		nop
1:
"		: "=r" (retval), "=r" (tmp1), "=r" (tmp2)
		: "r" (__new_psr), "i" (PSR_PIL)
		: "memory");

	return retval;
}

extern __inline__ unsigned long read_psr_and_cli(void)
{
	unsigned long retval;

	__asm__ __volatile__("
		rd	%%psr, %0
		nop
		nop
		nop
		andcc	%0, %1, %%g0
		bne	1f
		 nop
		wr	%0, %1, %%psr
		nop
		nop
		nop
1:
"		: "=r" (retval)
		: "i" (PSR_PIL)
		: "memory");

	return retval;
}

extern char spdeb_buf[256];

#define save_flags(flags)	((flags) = getipl())
#define save_and_cli(flags)	((flags) = read_psr_and_cli())
#define restore_flags(flags)	setipl((flags))

/* XXX Change this if we ever use a PSO mode kernel. */
#define mb()  __asm__ __volatile__ ("" : : : "memory")

#define nop() __asm__ __volatile__ ("nop");

extern __inline__ unsigned long xchg_u32(__volatile__ unsigned long *m, unsigned long val)
{
	__asm__ __volatile__("
	rd	%%psr, %%g3
	nop
	nop
	nop
	andcc	%%g3, %3, %%g0
	bne	1f
	 nop
	wr	%%g3, %3, %%psr
	nop
	nop
	nop
1:
	ld	[%1], %%g2
	andcc	%%g3, %3, %%g0
	st	%2, [%1]
	bne	1f
	 nop
	wr	%%g3, 0x0, %%psr
	nop
	nop
	nop
1:
	mov	%%g2, %0
	"
        : "=&r" (val)
        : "r" (m), "0" (val), "i" (PSR_PIL)
        : "g2", "g3");

	return val;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long __xchg(unsigned long x, __volatile__ void * ptr, int size)
{
	switch (size) {
	case 4:
		return xchg_u32(ptr, x);
	};
	__xchg_called_with_bad_pointer();
	return x;
}

#endif /* __ASSEMBLY__ */

#endif /* !(__SPARC_SYSTEM_H) */
