/* $Id: system.h,v 1.47 1998/10/21 03:21:20 davem Exp $ */
#ifndef __SPARC64_SYSTEM_H
#define __SPARC64_SYSTEM_H

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/asm_offsets.h>
#include <asm/visasm.h>

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

/* This cannot ever be a sun4c nor sun4 :) That's just history. */
#define ARCH_SUN4C_SUN4 0
#define ARCH_SUN4 0

extern unsigned long empty_bad_page;
extern unsigned long empty_zero_page;
#endif

#define setipl(__new_ipl) \
	__asm__ __volatile__("wrpr	%0, %%pil"  : : "r" (__new_ipl) : "memory")

#define __cli() \
	__asm__ __volatile__("wrpr	15, %%pil" : : : "memory")

#define __sti() \
	__asm__ __volatile__("wrpr	0, %%pil" : : : "memory")

#define getipl() \
({ unsigned long retval; __asm__ __volatile__("rdpr	%%pil, %0" : "=r" (retval)); retval; })

#define swap_pil(__new_pil) \
({	unsigned long retval; \
	__asm__ __volatile__("rdpr	%%pil, %0\n\t" \
			     "wrpr	%1, %%pil" \
			     : "=r" (retval) \
			     : "r" (__new_pil) \
			     : "memory"); \
	retval; \
})

#define read_pil_and_cli() \
({	unsigned long retval; \
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

#ifndef __ASSEMBLY__
extern void __global_cli(void);
extern void __global_sti(void);
extern unsigned long __global_save_flags(void);
extern void __global_restore_flags(unsigned long flags);
#endif

#define cli()			__global_cli()
#define sti()			__global_sti()
#define save_flags(x)		((x) = __global_save_flags())
#define restore_flags(flags)	__global_restore_flags(flags)
#define save_and_cli(flags)	do { save_flags(flags); cli(); } while(0)

#endif

#define mb()  		__asm__ __volatile__ ("stbar" : : : "memory")

#define nop() 		__asm__ __volatile__ ("nop")

#define membar(type)	__asm__ __volatile__ ("membar " type : : : "memory");

#define flushi(addr)	__asm__ __volatile__ ("flush %0" : : "r" (addr) : "memory")

#define flushw_all()	__asm__ __volatile__("flushw")

/* Performance counter register access. */
#define read_pcr(__p)  __asm__ __volatile__("rd	%%pcr, %0" : "=r" (__p))
#define write_pcr(__p) __asm__ __volatile__("wr	%0, 0x0, %%pcr" : : "r" (__p));
#define read_pic(__p)  __asm__ __volatile__("rd %%pic, %0" : "=r" (__p))
#define reset_pic()    __asm__ __volatile__("wr	%g0, 0x0, %pic");

#ifndef __ASSEMBLY__

extern void synchronize_user_stack(void);

extern __inline__ void flushw_user(void)
{
	__asm__ __volatile__("
		rdpr		%%otherwin, %%g1
		brz,pt		%%g1, 1f
		 mov		%%o7, %%g3
		call		__flushw_user
		 clr		%%g2
1:"	: : : "g1", "g2", "g3");
}

#define flush_user_windows flushw_user

	/* See what happens when you design the chip correctly?
	 *
	 * XXX What we are doing here assumes a lot about gcc reload
	 * XXX internals, it heavily risks compiler aborts due to
	 * XXX forbidden registers being spilled.  Rewrite me...  -DaveM
	 *
	 * SMP NOTE: At first glance it looks like there is a tiny
	 *           race window here at the end.  The possible problem
	 *           would be if a tlbcachesync MONDO vector got delivered
	 *           to us right before we set the final %g6 thread reg
	 *           value.  But that is impossible since only the holder
	 *           of scheduler_lock can send a tlbcachesync MONDO and
	 *           by definition we hold it right now.  Normal tlb
	 *           flush xcalls can come in, but those are safe and do
	 *           not reference %g6.
	 */
#define switch_to(prev, next)							\
do {	if (current->tss.flags & SPARC_FLAG_PERFCTR) {				\
		unsigned long __tmp;						\
		read_pcr(__tmp);						\
		current->tss.pcr_reg = __tmp;					\
		read_pic(__tmp);						\
		current->tss.kernel_cntd0 += (unsigned int)(__tmp);		\
		current->tss.kernel_cntd1 += ((__tmp) >> 32);			\
	}									\
	save_and_clear_fpu();							\
	__asm__ __volatile__(							\
	"flushw\n\t"								\
	"wrpr	%g0, 0x94, %pstate\n\t");					\
	__get_mmu_context(next);						\
	(next)->mm->cpu_vm_mask |= (1UL << smp_processor_id());			\
	__asm__ __volatile__(							\
	"wrpr	%%g0, 0x95, %%pstate\n\t"					\
	"stx	%%l0, [%%sp + 2047 + 0x60]\n\t"					\
	"stx	%%l1, [%%sp + 2047 + 0x68]\n\t"					\
	"stx	%%i6, [%%sp + 2047 + 0x70]\n\t"					\
	"stx	%%i7, [%%sp + 2047 + 0x78]\n\t"					\
	"rdpr	%%wstate, %%o5\n\t"						\
	"stx	%%o6, [%%g6 + %2]\n\t"						\
	"sth	%%o5, [%%g6 + %1]\n\t"						\
	"rdpr	%%cwp, %%o5\n\t"						\
	"sth	%%o5, [%%g6 + %4]\n\t"						\
	"mov	%0, %%g6\n\t"							\
	"lduh	[%0 + %4], %%g1\n\t"						\
	"wrpr	%%g1, %%cwp\n\t"						\
	"ldx	[%%g6 + %2], %%o6\n\t"						\
	"lduh	[%%g6 + %1], %%o5\n\t"						\
	"lduh	[%%g6 + %3], %%o7\n\t"						\
	"mov	%%g6, %%l2\n\t"							\
	"wrpr	%%o5, 0x0, %%wstate\n\t"					\
	"ldx	[%%sp + 2047 + 0x60], %%l0\n\t"					\
	"ldx	[%%sp + 2047 + 0x68], %%l1\n\t"					\
	"ldx	[%%sp + 2047 + 0x70], %%i6\n\t"					\
	"ldx	[%%sp + 2047 + 0x78], %%i7\n\t"					\
	"wrpr	%%g0, 0x94, %%pstate\n\t"					\
	"mov	%%l2, %%g6\n\t"							\
	"wrpr	%%g0, 0x96, %%pstate\n\t"					\
	"andcc	%%o7, 0x100, %%g0\n\t"						\
	"bne,pn	%%icc, ret_from_syscall\n\t"					\
	" nop\n\t"								\
	: 									\
	: "r" (next),								\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.wstate)),	\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.ksp)),	\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.flags)),	\
	  "i" ((const unsigned long)(&((struct task_struct *)0)->tss.cwp))	\
	: "cc", "g1", "g2", "g3", "g5", "g7",					\
	  "l2", "l3", "l4", "l5", "l6", "l7",					\
	  "i0", "i1", "i2", "i3", "i4", "i5",					\
	  "o0", "o1", "o2", "o3", "o4", "o5", "o7");				\
	/* If you fuck with this, update ret_from_syscall code too. */		\
	if (current->tss.flags & SPARC_FLAG_PERFCTR) {				\
		write_pcr(current->tss.pcr_reg);				\
		reset_pic();							\
	}									\
} while(0)

extern __inline__ unsigned long xchg32(__volatile__ unsigned int *m, unsigned int val)
{
	__asm__ __volatile__("
	mov		%0, %%g5
1:	lduw		[%2], %%g7
	cas		[%2], %%g7, %0
	cmp		%%g7, %0
	bne,a,pn	%%icc, 1b
	 mov		%%g5, %0
	membar		#StoreLoad | #StoreStore
"	: "=&r" (val)
	: "0" (val), "r" (m)
	: "g5", "g7", "cc", "memory");
	return val;
}

extern __inline__ unsigned long xchg64(__volatile__ unsigned long *m, unsigned long val)
{
	__asm__ __volatile__("
	mov		%0, %%g5
1:	ldx		[%2], %%g7
	casx		[%2], %%g7, %0
	cmp		%%g7, %0
	bne,a,pn	%%xcc, 1b
	 mov		%%g5, %0
	membar		#StoreLoad | #StoreStore
"	: "=&r" (val)
	: "0" (val), "r" (m)
	: "g5", "g7", "cc", "memory");
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
		return xchg32(ptr, x);
	case 8:
		return xchg64(ptr, x);
	};
	__xchg_called_with_bad_pointer();
	return x;
}

extern void die_if_kernel(char *str, struct pt_regs *regs) __attribute__ ((noreturn));

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_SYSTEM_H) */
