#include <asm/asmmacro.h>

GLOBAL_ENTRY(bcopy)
	.regstk 3,0,0,0
	mov r8=in0
	mov in0=in1
	;;
	mov in1=r8
END(bcopy)
	// FALL THROUGH
GLOBAL_ENTRY(memcpy)

#	define MEM_LAT	4

#	define N	MEM_LAT-1
#	define Nrot	((MEM_LAT + 7) & ~7)

#	define dst	r2
#	define src	r3
#	define len	r9
#	define saved_pfs r10
#	define saved_lc	r11
#	define saved_pr	r16
#	define t0	r17
#	define cnt	r18

	UNW(.prologue)
	UNW(.save ar.pfs, saved_pfs)
	alloc saved_pfs=ar.pfs,3,Nrot,0,Nrot
	lfetch [in1]

	.rotr val[MEM_LAT]
	.rotp p[MEM_LAT]

	UNW(.save ar.lc, saved_lc)
	mov saved_lc=ar.lc

	or t0=in0,in1
	UNW(.save pr, saved_pr)
	mov saved_pr=pr

	UNW(.body)

	mov ar.ec=MEM_LAT

	mov r8=in0		// return dst
	shr cnt=in2,3		// number of 8-byte words to copy
	mov pr.rot=1<<16
	;;
	cmp.eq p6,p0=in2,r0	// zero length?
	or t0=t0,in2
(p6)	br.ret.spnt.many rp	// yes, return immediately

	mov dst=in0		// copy because of rotation
	mov src=in1		// copy because of rotation
	adds cnt=-1,cnt		// br.ctop is repeat/until
	;;
	and t0=0x7,t0
	mov ar.lc=cnt
	;;
	cmp.ne p6,p0=t0,r0
(p6)	br.cond.spnt.few slow_memcpy

1:
(p[0])	ld8 val[0]=[src],8
(p[N])	st8 [dst]=val[N],8
	br.ctop.sptk.few 1b
	;;
.exit:
	mov ar.lc=saved_lc
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp

slow_memcpy:
	adds cnt=-1,in2
	;;
	mov ar.lc=cnt
	;;
1:
(p[0])	ld1 val[0]=[src],1
(p[N])	st1 [dst]=val[N],1
	br.ctop.sptk.few 1b
	br.sptk.few .exit

END(memcpy)
