#ifndef __ALPHA_SYSTEM_H
#define __ALPHA_SYSTEM_H

/*
 * Common PAL-code
 */
#define PAL_halt	  0
#define PAL_cflush	  1
#define PAL_draina	  2
#define PAL_cobratt	  9
#define PAL_bpt		128
#define PAL_bugchk	129
#define PAL_chmk	131
#define PAL_callsys	131
#define PAL_imb		134
#define PAL_rduniq	158
#define PAL_wruniq	159
#define PAL_gentrap	170
#define PAL_nphalt	190

/*
 * OSF specific PAL-code
 */
#define PAL_mtpr_mces	17
#define PAL_wrfen	43
#define PAL_wrvptptr	45
#define PAL_jtopal	46
#define PAL_swpctx	48
#define PAL_wrval	49
#define PAL_rdval	50
#define PAL_tbi		51
#define PAL_wrent	52
#define PAL_swpipl	53
#define PAL_rdps	54
#define PAL_wrkgp	55
#define PAL_wrusp	56
#define PAL_wrperfmon	57
#define PAL_rdusp	58
#define PAL_whami	60
#define PAL_rtsys	61
#define PAL_rti		63

#define invalidate_all() \
__asm__ __volatile__( \
	"lda $16,-2($31)\n\t" \
	".long 51" \
	: : :"$1", "$16", "$17", "$22","$23","$24","$25")

#define invalidate() \
__asm__ __volatile__( \
	"lda $16,-1($31)\n\t" \
	".long 51" \
	: : :"$1", "$16", "$17", "$22","$23","$24","$25")

#define swpipl(__new_ipl) \
({ unsigned long __old_ipl; \
__asm__ __volatile__( \
	"bis %1,%1,$16\n\t" \
	".long 53\n\t" \
	"bis $0,$0,%0" \
	: "=r" (__old_ipl) \
	: "r" (__new_ipl) \
	: "$0", "$1", "$16", "$22", "$23", "$24", "$25"); \
__old_ipl; })

#endif
