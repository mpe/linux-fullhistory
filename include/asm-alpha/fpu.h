#ifndef __ASM_ALPHA_FPU_H
#define __ASM_ALPHA_FPU_H

/*
 * Alpha floating-point control register defines:
 */
#define FPCR_INVD	(1UL<<49)	/* invalid op disable (opt.) */
#define FPCR_DZED	(1UL<<50)	/* division by zero disable (opt.) */
#define FPCR_OVFD	(1UL<<51)	/* overflow disable (optional) */
#define FPCR_INV	(1UL<<52)	/* invalid operation */
#define FPCR_DZE	(1UL<<53)	/* division by zero */
#define FPCR_OVF	(1UL<<54)	/* overflow */
#define FPCR_UNF	(1UL<<55)	/* underflow */
#define FPCR_INE	(1UL<<56)	/* inexact */
#define FPCR_IOV	(1UL<<57)	/* integer overflow */
#define FPCR_UNDZ	(1UL<<60)	/* underflow to zero (opt.) */
#define FPCR_UNFD	(1UL<<61)	/* underflow disable (opt.) */
#define FPCR_INED	(1UL<<62)	/* inexact disable (opt.) */
#define FPCR_SUM	(1UL<<63)	/* summary bit */

#define FPCR_DYN_SHIFT	58		/* first dynamic rounding mode bit */
#define FPCR_DYN_CHOPPED (0x0UL << FPCR_DYN_SHIFT)	/* towards 0 */
#define FPCR_DYN_MINUS	 (0x1UL << FPCR_DYN_SHIFT)	/* towards -INF */
#define FPCR_DYN_NORMAL	 (0x2UL << FPCR_DYN_SHIFT)	/* towards nearest */
#define FPCR_DYN_PLUS	 (0x3UL << FPCR_DYN_SHIFT)	/* towards +INF */
#define FPCR_DYN_MASK	 (0x3UL << FPCR_DYN_SHIFT)

#define FPCR_MASK	0xfffe000000000000

/*
 * IEEE trap enables are implemented in software.  These per-thread
 * bits are stored in the "flags" field of "struct thread_struct".
 * Thus, the bits are defined so as not to conflict with the
 * floating-point enable bit (which is architected).  On top of that,
 * we want to make these bits compatible with OSF/1 so
 * ieee_set_fp_control() etc. can be implemented easily and
 * compatibly.  The corresponding definitions are in
 * /usr/include/machine/fpu.h under OSF/1.
 */
#define IEEE_TRAP_ENABLE_INV	(1<<1)	/* invalid op */
#define IEEE_TRAP_ENABLE_DZE	(1<<2)	/* division by zero */
#define IEEE_TRAP_ENABLE_OVF	(1<<3)	/* overflow */
#define IEEE_TRAP_ENABLE_UNF	(1<<4)	/* underflow */
#define IEEE_TRAP_ENABLE_INE	(1<<5)	/* inexact */
#define IEEE_TRAP_ENABLE_MASK	(IEEE_TRAP_ENABLE_INV | IEEE_TRAP_ENABLE_DZE |\
				 IEEE_TRAP_ENABLE_OVF | IEEE_TRAP_ENABLE_UNF |\
				 IEEE_TRAP_ENABLE_INE)

/* status bits coming from fpcr: */
#define IEEE_STATUS_INV		(1<<17)
#define IEEE_STATUS_DZE		(1<<18)
#define IEEE_STATUS_OVF		(1<<19)
#define IEEE_STATUS_UNF		(1<<20)
#define IEEE_STATUS_INE		(1<<21)

#define IEEE_STATUS_MASK	(IEEE_STATUS_INV | IEEE_STATUS_DZE |	\
				 IEEE_STATUS_OVF | IEEE_STATUS_UNF |	\
				 IEEE_STATUS_INE)

#define IEEE_STATUS_TO_EXCSUM_SHIFT	16

#define IEEE_INHERIT    (1UL<<63)	/* inherit on thread create? */

#endif /* __ASM_ALPHA_FPU_H */
