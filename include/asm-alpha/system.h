#ifndef __ALPHA_SYSTEM_H
#define __ALPHA_SYSTEM_H

#include <linux/config.h>
#include <asm/pal.h>
#include <asm/page.h>

/*
 * System defines.. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.
 */

/*
 * We leave one page for the initial stack page, and one page for
 * the initial process structure. Also, the console eats 3 MB for
 * the initial bootloader (one of which we can reclaim later).
 * With a few other pages for various reasons, we'll use an initial
 * load address of PAGE_OFFSET+0x310000UL
 */
#define BOOT_PCB	0x20000000
#define BOOT_ADDR	0x20000000
/* Remove when official MILO sources have ELF support: */
#define BOOT_SIZE	(16*1024)

#define KERNEL_START	(PAGE_OFFSET+0x300000)
#define SWAPPER_PGD	(PAGE_OFFSET+0x300000)
#define INIT_STACK	(PAGE_OFFSET+0x302000)
#define EMPTY_PGT	(PAGE_OFFSET+0x304000)
#define EMPTY_PGE	(PAGE_OFFSET+0x308000)
#define ZERO_PGE	(PAGE_OFFSET+0x30A000)

#define START_ADDR	(PAGE_OFFSET+0x310000)

#ifndef __ASSEMBLY__

/*
 * This is the logout header that should be common to all platforms
 * (assuming they are running OSF/1 PALcode, I guess).
 */
struct el_common {
	unsigned int	size;		/* size in bytes of logout area */
	int		sbz1	: 30;	/* should be zero */
	int		err2	:  1;	/* second error */
	int		retry	:  1;	/* retry flag */
	unsigned int	proc_offset;	/* processor-specific offset */
	unsigned int	sys_offset;	/* system-specific offset */
	unsigned long	code;		/* machine check code */
};

/* Machine Check Frame for uncorrectable errors (Large format)
 *      --- This is used to log uncorrectable errors such as
 *          double bit ECC errors.
 *      --- These errors are detected by both processor and systems.
 */
struct el_common_EV5_uncorrectable_mcheck {
        unsigned long   shadow[8];        /* Shadow reg. 8-14, 25           */
        unsigned long   paltemp[24];      /* PAL TEMP REGS.                 */
        unsigned long   exc_addr;         /* Address of excepting instruction*/
        unsigned long   exc_sum;          /* Summary of arithmetic traps.   */
        unsigned long   exc_mask;         /* Exception mask (from exc_sum). */
        unsigned long   pal_base;         /* Base address for PALcode.      */
        unsigned long   isr;              /* Interrupt Status Reg.          */
        unsigned long   icsr;             /* CURRENT SETUP OF EV5 IBOX      */
        unsigned long   ic_perr_stat;     /* I-CACHE Reg. <11> set Data parity
                                                         <12> set TAG parity*/
        unsigned long   dc_perr_stat;     /* D-CACHE error Reg. Bits set to 1:
                                                     <2> Data error in bank 0
                                                     <3> Data error in bank 1
                                                     <4> Tag error in bank 0
                                                     <5> Tag error in bank 1 */
        unsigned long   va;               /* Effective VA of fault or miss. */
        unsigned long   mm_stat;          /* Holds the reason for D-stream 
                                             fault or D-cache parity errors */
        unsigned long   sc_addr;          /* Address that was being accessed
                                             when EV5 detected Secondary cache
                                             failure.                 */
        unsigned long   sc_stat;          /* Helps determine if the error was
                                             TAG/Data parity(Secondary Cache)*/
        unsigned long   bc_tag_addr;      /* Contents of EV5 BC_TAG_ADDR    */
        unsigned long   ei_addr;          /* Physical address of any transfer
                                             that is logged in EV5 EI_STAT */
        unsigned long   fill_syndrome;    /* For correcting ECC errors.     */
        unsigned long   ei_stat;          /* Helps identify reason of any 
                                             processor uncorrectable error
                                             at its external interface.     */
        unsigned long   ld_lock;          /* Contents of EV5 LD_LOCK register*/
};

extern void halt(void) __attribute__((noreturn));

#define switch_to(prev,next,last)			\
do {							\
	unsigned long pcbb;				\
	current = (next);				\
	pcbb = virt_to_phys(&current->thread);		\
	(last) = alpha_switch_to(pcbb, (prev));		\
} while (0)

extern struct task_struct* alpha_switch_to(unsigned long, struct task_struct*);

#define mb() \
__asm__ __volatile__("mb": : :"memory")

#define rmb() \
__asm__ __volatile__("mb": : :"memory")

#define wmb() \
__asm__ __volatile__("wmb": : :"memory")

#define imb() \
__asm__ __volatile__ ("call_pal %0 #imb" : : "i" (PAL_imb) : "memory")

#define draina() \
__asm__ __volatile__ ("call_pal %0 #draina" : : "i" (PAL_draina) : "memory")

enum implver_enum {
	IMPLVER_EV4,
	IMPLVER_EV5,
	IMPLVER_EV6
};

#ifdef CONFIG_ALPHA_GENERIC
#define implver()				\
({ unsigned long __implver;			\
   __asm__ ("implver %0" : "=r"(__implver));	\
   (enum implver_enum) __implver; })
#else
/* Try to eliminate some dead code.  */
#ifdef CONFIG_ALPHA_EV4
#define implver() IMPLVER_EV4
#endif
#ifdef CONFIG_ALPHA_EV5
#define implver() IMPLVER_EV5
#endif
#ifdef CONFIG_ALPHA_EV6
#define implver() IMPLVER_EV6
#endif
#endif

enum amask_enum {
	AMASK_BWX = (1UL << 0),
	AMASK_FIX = (1UL << 1),
	AMASK_MAX = (1UL << 8),
	AMASK_PRECISE_TRAP = (1UL << 9),
};

#define amask(mask)						\
({ unsigned long __amask, __input = (mask);			\
   __asm__ ("amask %1,%0" : "=r"(__amask) : "rI"(__input));	\
   __amask; })

#define __CALL_PAL_R0(NAME, TYPE)				\
static inline TYPE NAME(void)					\
{								\
	register TYPE __r0 __asm__("$0");			\
	__asm__ __volatile__(					\
		"call_pal %1 # " #NAME				\
		:"=r" (__r0)					\
		:"i" (PAL_ ## NAME)				\
		:"$1", "$16", "$22", "$23", "$24", "$25");	\
	return __r0;						\
}

#define __CALL_PAL_W1(NAME, TYPE0)				\
static inline void NAME(TYPE0 arg0)				\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %1 # "#NAME				\
		: "=r"(__r16)					\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_W2(NAME, TYPE0, TYPE1)			\
static inline void NAME(TYPE0 arg0, TYPE1 arg1)			\
{								\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r17)			\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
}

#define __CALL_PAL_RW1(NAME, RTYPE, TYPE0)			\
static inline RTYPE NAME(TYPE0 arg0)				\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	__asm__ __volatile__(					\
		"call_pal %2 # "#NAME				\
		: "=r"(__r16), "=r"(__r0)			\
		: "i"(PAL_ ## NAME), "0"(__r16)			\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

#define __CALL_PAL_RW2(NAME, RTYPE, TYPE0, TYPE1)		\
static inline RTYPE NAME(TYPE0 arg0, TYPE1 arg1)		\
{								\
	register RTYPE __r0 __asm__("$0");			\
	register TYPE0 __r16 __asm__("$16") = arg0;		\
	register TYPE1 __r17 __asm__("$17") = arg1;		\
	__asm__ __volatile__(					\
		"call_pal %3 # "#NAME				\
		: "=r"(__r16), "=r"(__r17), "=r"(__r0)		\
		: "i"(PAL_ ## NAME), "0"(__r16), "1"(__r17)	\
		: "$1", "$22", "$23", "$24", "$25");		\
	return __r0;						\
}

__CALL_PAL_R0(rdmces, unsigned long);
__CALL_PAL_R0(rdps, unsigned long);
__CALL_PAL_R0(rdusp, unsigned long);
__CALL_PAL_RW1(swpipl, unsigned long, unsigned long);
__CALL_PAL_R0(whami, unsigned long);
__CALL_PAL_W2(wrent, void*, unsigned long);
__CALL_PAL_W1(wripir, unsigned long);
__CALL_PAL_W1(wrkgp, unsigned long);
__CALL_PAL_W1(wrmces, unsigned long);
__CALL_PAL_RW2(wrperfmon, unsigned long, unsigned long, unsigned long);
__CALL_PAL_W1(wrusp, unsigned long);
__CALL_PAL_W1(wrvptptr, unsigned long);

#define __cli()			((void) swpipl(7))
#define __sti()			((void) swpipl(0))
#define __save_flags(flags)	((flags) = rdps())
#define __save_and_cli(flags)	((flags) = swpipl(7))
#define __restore_flags(flags)	((void) swpipl(flags))

#define local_irq_save(flags)		__save_and_cli(flags)
#define local_irq_restore(flags)	__restore_flags(flags)
#define local_irq_disable()		__cli()
#define local_irq_enable()		__sti()

#ifdef __SMP__

extern int global_irq_holder;

#define save_and_cli(flags)     (save_flags(flags), cli())

extern void __global_cli(void);
extern void __global_sti(void);
extern unsigned long __global_save_flags(void);
extern void __global_restore_flags(unsigned long flags);

#define cli()                   __global_cli()
#define sti()                   __global_sti()
#define save_flags(flags)	((flags) = __global_save_flags())
#define restore_flags(flags)    __global_restore_flags(flags)

#else /* __SMP__ */

#define cli()			__cli()
#define sti()			__sti()
#define save_flags(flags)	__save_flags(flags)
#define save_and_cli(flags)	__save_and_cli(flags)
#define restore_flags(flags)	__restore_flags(flags)

#endif /* __SMP__ */

/*
 * TB routines..
 */
#define __tbi(nr,arg,arg1...)					\
({								\
	register unsigned long __r16 __asm__("$16") = (nr);	\
	register unsigned long __r17 __asm__("$17"); arg;	\
	__asm__ __volatile__(					\
		"call_pal %3 #__tbi"				\
		:"=r" (__r16),"=r" (__r17)			\
		:"0" (__r16),"i" (PAL_tbi) ,##arg1		\
		:"$0", "$1", "$22", "$23", "$24", "$25");	\
})

#define tbi(x,y)	__tbi(x,__r17=(y),"1" (__r17))
#define tbisi(x)	__tbi(1,__r17=(x),"1" (__r17))
#define tbisd(x)	__tbi(2,__r17=(x),"1" (__r17))
#define tbis(x)		__tbi(3,__r17=(x),"1" (__r17))
#define tbiap()		__tbi(-1, /* no second argument */)
#define tbia()		__tbi(-2, /* no second argument */)

/*
 * Give prototypes to shut up gcc.
 */
extern __inline__ unsigned long xchg_u32(volatile int *m, unsigned long val);
extern __inline__ unsigned long xchg_u64(volatile long *m, unsigned long val);

extern __inline__ unsigned long xchg_u32(volatile int *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldl_l %0,%2\n"
	"	bis $31,%3,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,2f\n"
	"	mb\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m));

	return val;
}

extern __inline__ unsigned long xchg_u64(volatile long * m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldq_l %0,%2\n"
	"	bis $31,%3,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,2f\n"
	"	mb\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m));

	return val;
}

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but as that doesn't work
 * too well on the alpha anyway..
 */
extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__xchg(unsigned long x, volatile void * ptr, int size)
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

#define xchg(ptr,x) \
  ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

#endif /* __ASSEMBLY__ */

#endif
