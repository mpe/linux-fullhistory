/* $Id: system.h,v 1.16 1997/05/14 20:48:07 davem Exp $ */
#ifndef __SPARC64_SYSTEM_H
#define __SPARC64_SYSTEM_H

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/asm_offsets.h>

#define NCPUS	4	/* No SMP yet */

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
  ap1000      = 0x07, /* almost a sun4m */
};
                  
#define sparc_cpu_model sun4u
                  

extern unsigned long empty_bad_page;
extern unsigned long empty_bad_pmd_table;
extern unsigned long empty_bad_pte_table;
extern unsigned long empty_zero_page;
#endif

#define setipl(__new_ipl) \
	__asm__ __volatile__("wrpr	%0, %%pil"  : : "r" (__new_ipl) : "memory")

#define __cli() \
	__asm__ __volatile__("wrpr	15, %%pil" : : : "memory")

#define __sti() \
	__asm__ __volatile__("wrpr	0, %%pil" : : : "memory")

#define getipl() \
({ int retval; __asm__ __volatile__("rdpr	%%pil, %0" : "=r" (retval)); retval; })

#define swap_pil(__new_pil) \
({	int retval; \
	__asm__ __volatile__("rdpr	%%pil, %0\n\t" \
			     "wrpr	%1, %%pil" \
			     : "=r" (retval) \
			     : "r" (__new_pil) \
			     : "memory"); \
	retval; \
})

#define read_pil_and_cli() \
({	int retval; \
	__asm__ __volatile__("rdpr	%%pil, %0\n\t" \
			     "wrpr	15, %%pil" \
			     : "=r" (retval) \
			     : : "memory"); \
	retval; \
})

#define __save_flags(flags)	((flags) = getipl())
#define __save_and_cli(flags)	((flags) = read_pil_and_cli())
#define __restore_flags(flags)	setipl((flags))

#ifndef __SMP__
#define cli() __cli()
#define sti() __sti()
#define save_flags(x) __save_flags(x)
#define restore_flags(x) __restore_flags(x)
#define save_and_cli(x) __save_and_cli(x)
#else
#error SMP not supported on sparc64
#endif

#define mb()  		__asm__ __volatile__ ("stbar" : : : "memory")

#define nop() 		__asm__ __volatile__ ("nop")

#define membar(type)	__asm__ __volatile__ ("membar " type : : : "memory");

#define flushi(addr)	__asm__ __volatile__ ("flush %0" : : "r" (addr))

#define flushw_all()	__asm__ __volatile__("flushw")

#ifndef __ASSEMBLY__

extern __inline__ void flushw_user(void)
{
	__asm__ __volatile__("
		rdpr		%%otherwin, %%g1
1:
		rdpr		%%otherwin, %%g2
		brnz,pn		%%g2, 1b
		 save		%%sp, %0, %%sp
1:
		subcc		%%g1, 1, %%g1
		bne,pn		%%xcc, 1b
		 restore	%%g0, %%g0, %%g0
	" : : "i" (-REGWIN_SZ)
	  : "g1", "g2", "cc");
}

#define flush_user_windows flushw_user

#ifdef __SMP__
#error SMP not supported on sparc64
#else
#if 0
#define SWITCH_DO_LAZY_FPU(next)			\
	if(last_task_used_math != (next))		\
		(next)->tss.kregs->tstate&=~TSTATE_PEF
#else
/* XXX FIX ME BIG TIME XXX -DaveM */
#define SWITCH_DO_LAZY_FPU(next)	do { } while(0)
#endif
#endif

	/* See what happens when you design the chip correctly?
	 * NOTE NOTE NOTE this is extremely non-trivial what I
	 * am doing here.  GCC needs only two registers to stuff
	 * things into ('next' and &current_set[cpu])  So I "claim"
	 * that I do not clobber them, when in fact I do.  Please,
	 * when modifying this code inspect output of sched.s very
	 * carefully to make sure things still work.  -DaveM
	 */
#define switch_to(prev, next)								\
do {											\
	__label__ switch_continue;							\
	register unsigned long task_pc asm("o7");					\
	extern struct task_struct *current_set[NR_CPUS];				\
	SWITCH_DO_LAZY_FPU(next);							\
	task_pc = ((unsigned long) &&switch_continue) - 0x8;				\
	__asm__ __volatile__(								\
	"flushw\n\t"									\
	"stx	%%i6, [%%sp + 2047 + 0x70]\n\t"						\
	"stx	%%i7, [%%sp + 2047 + 0x78]\n\t"						\
	"stx	%%o6, [%%g6 + %3]\n\t"							\
	"rdpr	%%wstate, %%o5\n\t"							\
	"stx	%%o7, [%%g6 + %4]\n\t"							\
	"mov	%6, %%o4\n\t"								\
	"stx	%%o5, [%%g6 + %2]\n\t"							\
	"st	%%o4, [%%g6 + %7]\n\t"							\
	"rdpr	%%cwp, %%o5\n\t"							\
	"stx	%%o5, [%%g6 + %8]\n\t"							\
	"mov	%1, %%g6\n\t"								\
	"stx	%%g6, [%0]\n\t"								\
	"ldx	[%%g6 + %8], %%g1\n\t"							\
	"wrpr	%%g1, %%cwp\n\t"							\
	"ldx	[%%g6 + %2], %%o5\n\t"							\
	"ldx	[%%g6 + %3], %%o6\n\t"							\
	"ldx	[%%g6 + %4], %%o7\n\t"							\
	"wrpr	%%o5, 0x0, %%wstate\n\t"						\
	"ldx	[%%sp + 2047 + 0x70], %%i6\n\t"						\
	"jmpl	%%o7 + 0x8, %%g0\n\t"							\
	" ldx	[%%sp + 2047 + 0x78], %%i7\n\t"						\
	: /* No outputs */								\
	: "r" (&(current_set[smp_processor_id()])), "r" (next),				\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.wstate)),		\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ksp)),		\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.kpc)),		\
	  "r" (task_pc), "i" (255),							\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->processor)),		\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.cwp))		\
	: "cc", "g1", "g2", "g3", "g5", "g7",						\
	  "l2", "l3", "l4", "l5", "l6", "l7",						\
	  "i0", "i1", "i2", "i3", "i4", "i5",						\
	  "o0", "o1", "o2", "o3", "o4", "o5");						\
switch_continue: } while(0)

/* Unlike the hybrid v7/v8 kernel, we can assume swap exists under V9. */
extern __inline__ unsigned long xchg_u32(__volatile__ unsigned int *m, unsigned int val)
{
	__asm__ __volatile__("swap	[%2], %0"
			     : "=&r" (val)
			     : "0" (val), "r" (m));
	return val;
}

/* Bolix, must use casx for 64-bit values. */
extern __inline__ unsigned long xchg_u64(__volatile__ unsigned long *m,
					 unsigned long val)
{
	unsigned long temp;
	__asm__ __volatile__("
	ldx		[%3], %1
1:
	casx		[%3], %1, %0
	cmp		%1, %0
	bne,a,pn	%%xcc, 1b
	 ldx		[%3], %1
"	: "=&r" (val), "=&r" (temp)
	: "0" (val), "r" (m)
	: "cc");
	return val;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long __xchg(unsigned long x, __volatile__ void * ptr,
				       int size)
{
	switch (size) {
	case 4:
		return xchg_u32(ptr, x);
	case 8:
		return xchg_u64(ptr, x);
	};
	__xchg_called_with_bad_pointer();
	return x;
}

extern void die_if_kernel(char *str, struct pt_regs *regs) __attribute__ ((noreturn));

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SYSTEM_H) */
