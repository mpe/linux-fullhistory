/* $Id: system.h,v 1.2 1996/12/02 00:01:07 davem Exp $ */
#ifndef __SPARC64_SYSTEM_H
#define __SPARC64_SYSTEM_H

#define setipl(__new_ipl) \
	__asm__ __volatile__("wrpr	%0, %%pil"  : : "r" (__new_ipl) : "memory")

#define cli() \
	__asm__ __volatile__("wrpr	15, %%pil" : : : "memory")

#define sti() \
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

#define save_flags(flags)	((flags) = getipl())
#define save_and_cli(flags)	((flags) = read_pil_and_cli())
#define restore_flags(flags)	setipl((flags))

#define mb()  		__asm__ __volatile__ ("stbar" : : : "memory")

#define nop() 		__asm__ __volatile__ ("nop")

#define membar(type)	__asm__ __volatile__ ("membar " type : : : "memory");

/* Unlike the hybrid v7/v8 kernel, we can assume swap exists under V9. */
extern __inline__ unsigned long xchg_u32(__volatile__ unsigned int *m, unsigned int val)
{
	__asm__ __volatile__("swap	[%1], %0"
			     : "=&r" (val)
			     : "r" (m), "0" (val));
	return val;
}

/* Bolix, must use casx for 64-bit values. */
extern __inline__ unsigned long xchg_u64(__volatile__ unsigned long *m,
					 unsigned long val)
{
	unsigned long temp;
	__asm__ __volatile__("
	ldx		[%2], %1
1:
	casx		[%2], %1, %0
	cmp		%1, %0
	bne,a,pn	%%xcc, 1b
	 ldx		[%2], %1
"	: "=&r" (val), "=&r" (temp)
	: "r" (m), "0" (val));
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

#endif /* !(__SPARC64_SYSTEM_H) */
