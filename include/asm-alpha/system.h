#ifndef __ALPHA_SYSTEM_H
#define __ALPHA_SYSTEM_H

#include <asm/pal.h>	/* for backwards compatibility... */

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * We leave one page for the initial stack page, and one page for
 * the initial process structure. Also, the console eats 3 MB for
 * the initial bootloader (one of which we can reclaim later).
 * With a few other pages for various reasons, we'll use an initial
 * load address of 0xfffffc0000310000UL
 */
#define BOOT_PCB	0x20000000
#define BOOT_ADDR	0x20000000
#define BOOT_SIZE	(16*1024)

#define KERNEL_START	0xfffffc0000300000
#define SWAPPER_PGD	0xfffffc0000300000
#define INIT_STACK	0xfffffc0000302000
#define EMPTY_PGT	0xfffffc0000304000
#define EMPTY_PGE	0xfffffc0000308000
#define ZERO_PGE	0xfffffc000030A000

#define START_ADDR	0xfffffc0000310000
#define START_SIZE	(2*1024*1024)

#ifndef __ASSEMBLY__

/*
 * This is the logout header that should be common to all platforms
 * (assuming they are running OSF/1 PALcode, I guess).
 */
struct el_common {
        unsigned int	size;		/* size in bytes of logout area */
        int		sbz1	: 31;	/* should be zero */
        char		retry	:  1;	/* retry flag */
        unsigned int	proc_offset;	/* processor-specific offset */
        unsigned int	sys_offset;	/* system-specific offset */
};

extern void wrent(void *, unsigned long);
extern void wrkgp(unsigned long);
extern void wrusp(unsigned long);
extern unsigned long rdusp(void);
extern unsigned long rdmces (void);
extern void wrmces (unsigned long);

#define halt() __asm__ __volatile__ ("call_pal %0" : : "i" (PAL_halt) : "memory")

#define switch_to(prev,next) do { \
	current_set[0] = next; \
	alpha_switch_to((unsigned long) &(next)->tss - 0xfffffc0000000000); \
} while (0)

extern void alpha_switch_to(unsigned long pctxp);

extern void imb(void);

#define mb() \
__asm__ __volatile__("mb": : :"memory")

#define draina() \
__asm__ __volatile__ ("call_pal %0" : : "i" (PAL_draina) : "memory")

#define getipl() \
({ unsigned long __old_ipl; \
__asm__ __volatile__( \
	"call_pal 54\n\t" \
	"bis $0,$0,%0" \
	: "=r" (__old_ipl) \
	: : "$0", "$1", "$16", "$22", "$23", "$24", "$25"); \
__old_ipl; })

#define setipl(__new_ipl) \
__asm__ __volatile__( \
	"bis %0,%0,$16\n\t" \
	"call_pal 53" \
	: : "r" (__new_ipl) \
	: "$0", "$1", "$16", "$22", "$23", "$24", "$25", "memory")

#define swpipl(__new_ipl) \
({ unsigned long __old_ipl; \
__asm__ __volatile__( \
	"bis %1,%1,$16\n\t" \
	"call_pal 53\n\t" \
	"bis $0,$0,%0" \
	: "=r" (__old_ipl) \
	: "r" (__new_ipl) \
	: "$0", "$1", "$16", "$22", "$23", "$24", "$25", "memory"); \
__old_ipl; })

#define cli()			setipl(7)
#define sti()			setipl(0)
#define save_flags(flags)	do { flags = getipl(); } while (0)
#define restore_flags(flags)	setipl(flags)

/*
 * TB routines..
 */
extern void tbi(long type, ...);

#define tbisi(x)	tbi(1,(x))
#define tbisd(x)	tbi(2,(x))
#define tbis(x)		tbi(3,(x))
#define tbiap()		tbi(-1)
#define tbia()		tbi(-2)

/*
 * Give prototypes to shut up gcc.
 */
extern __inline__ unsigned long xchg_u32 (volatile int * m, unsigned long val);
extern __inline__ unsigned long xchg_u64 (volatile long * m, unsigned long val);

extern __inline__ unsigned long xchg_u32(volatile int * m, unsigned long val)
{
	unsigned long dummy;
	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%2\n\t"
		"bis %3,%3,%1\n\t"
		"stl_c %1,%2\n\t"
		"beq %1,1b\n"
		: "=&r" (val), "=&r" (dummy), "=m" (*m)
		: "r" (val), "m" (*m));
	return val;
}

extern __inline__ unsigned long xchg_u64(volatile long * m, unsigned long val)
{
	unsigned long dummy;
	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%2\n\t"
		"bis %3,%3,%1\n\t"
		"stq_c %1,%2\n\t"
		"beq %1,1b\n"
		: "=&r" (val), "=&r" (dummy), "=m" (*m)
		: "r" (val), "m" (*m));
	return val;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but as that doesn't work
 * too well on the alpha anyway..
 */
extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return xchg_u32(ptr, x);
		case 8:
			return xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#endif /* __ASSEMBLY__ */

#endif
