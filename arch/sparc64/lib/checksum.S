/* checksum.S: Sparc V9 optimized checksum code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996, 2000 David S. Miller
 *  Copyright(C) 1997 Jakub Jelinek
 *
 * derived from:
 *	Linux/Alpha checksum c-code
 *      Linux/ix86 inline checksum assembly
 *      RFC1071 Computing the Internet Checksum (esp. Jacobsons m68k code)
 *	David Mosberger-Tang for optimized reference c-code
 *	BSD4.4 portable checksum routine
 */

#include <asm/errno.h>
#include <asm/head.h>
#include <asm/ptrace.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/thread_info.h>

	/* The problem with the "add with carry" instructions on Ultra
	 * are two fold.  Firstly, they cannot pair with jack shit,
	 * and also they only add in the 32-bit carry condition bit
	 * into the accumulated sum.  The following is much better.
	 * For larger chunks we use VIS code, which is faster ;)
	 */

#define src o0
#define dst o1
#define len o2
#define sum o3

	.text
	/* I think I have an erection...  Once _AGAIN_ the SunSoft
	 * engineers are caught asleep at the keyboard, tsk tsk...
	 */

#define CSUMCOPY_LASTCHUNK(off, t0, t1)							\
	ldxa		[%src - off - 0x08] %asi, t0;					\
	ldxa		[%src - off - 0x00] %asi, t1;					\
	nop; nop;									\
	addcc		t0, %sum, %sum;							\
	stw		t0, [%dst - off - 0x04];					\
	srlx		t0, 32, t0;							\
	bcc,pt		%xcc, 51f;							\
	 stw		t0, [%dst - off - 0x08];					\
	add		%sum, 1, %sum;							\
51:	addcc		t1, %sum, %sum;							\
	stw		t1, [%dst - off + 0x04];					\
	srlx		t1, 32, t1;							\
	bcc,pt		%xcc, 52f;							\
	 stw		t1, [%dst - off - 0x00];					\
	add		%sum, 1, %sum;							\
52:

cpc_start:
cc_end_cruft:
	andcc		%g7, 8, %g0		! IEU1	Group
	be,pn		%icc, 1f		! CTI
	 and		%g7, 4, %g5		! IEU0
	ldxa		[%src + 0x00] %asi, %g2	! Load	Group
	add		%dst, 8, %dst		! IEU0
	add		%src, 8, %src		! IEU1
	addcc		%g2, %sum, %sum		! IEU1	Group + 2 bubbles
	stw		%g2, [%dst - 0x04]	! Store
	srlx		%g2, 32, %g2		! IEU0
	bcc,pt		%xcc, 1f		! CTI	Group
	 stw		%g2, [%dst - 0x08]	! Store
	add		%sum, 1, %sum		! IEU0
1:	brz,pt		%g5, 1f			! CTI	Group
	 clr		%g2			! IEU0
	lduwa		[%src + 0x00] %asi, %g2	! Load
	add		%dst, 4, %dst		! IEU0	Group
	add		%src, 4, %src		! IEU1
	stw		%g2, [%dst - 0x04]	! Store	Group + 2 bubbles
	sllx		%g2, 32, %g2		! IEU0
1:	andcc		%g7, 2, %g0		! IEU1
	be,pn		%icc, 1f		! CTI	Group
	 clr		%o4			! IEU1
	lduha		[%src + 0x00] %asi, %o4	! Load
	add		%src, 2, %src		! IEU0	Group
	add		%dst, 2, %dst		! IEU1
	sth		%o4, [%dst - 0x2]	! Store Group + 2 bubbles
	sll		%o4, 16, %o4		! IEU0
1:	andcc		%g7, 1, %g0		! IEU1
	be,pn		%icc, 1f		! CTI	Group
	 clr		%o5			! IEU0
	lduba		[%src + 0x00] %asi, %o5	! Load
	stb		%o5, [%dst + 0x00]	! Store	Group + 2 bubbles
	sll		%o5, 8, %o5		! IEU0
1:	or		%g2, %o4, %o4		! IEU1
	or		%o5, %o4, %o4		! IEU0	Group
	addcc		%o4, %sum, %sum		! IEU1
	bcc,pt		%xcc, ccfold		! CTI
	 nop					! IEU0	Group
	b,pt		%xcc, ccfold		! CTI
	 add		%sum, 1, %sum		! IEU1

cc_fixit:
	cmp		%len, 6			! IEU1	Group
	bl,a,pn		%icc, ccte		! CTI
	 andcc		%len, 0xf, %g7		! IEU1	Group
	andcc		%src, 2, %g0		! IEU1	Group
	be,pn		%icc, 1f		! CTI
	 andcc		%src, 0x4, %g0		! IEU1	Group
	lduha		[%src + 0x00] %asi, %g4	! Load
	sub		%len, 2, %len		! IEU0
	add		%src, 2, %src		! IEU0	Group
	add		%dst, 2, %dst		! IEU1
	sll		%g4, 16, %g3		! IEU0	Group + 1 bubble
	addcc		%g3, %sum, %sum		! IEU1
	bcc,pt		%xcc, 0f		! CTI
	 srl		%sum, 16, %g3		! IEU0	Group
	add		%g3, 1, %g3		! IEU0	4 clocks (mispredict)
0:	andcc		%src, 0x4, %g0		! IEU1	Group
	sth		%g4, [%dst - 0x2]	! Store
	sll		%sum, 16, %sum		! IEU0
	sll		%g3, 16, %g3		! IEU0	Group
	srl		%sum, 16, %sum		! IEU0	Group
	or		%g3, %sum, %sum		! IEU0	Group (regdep)
1:	be,pt		%icc, ccmerge		! CTI
	 andcc		%len, 0xf0, %g1		! IEU1
	lduwa		[%src + 0x00] %asi, %g4	! Load	Group
	sub		%len, 4, %len		! IEU0
	add		%src, 4, %src		! IEU1
	add		%dst, 4, %dst		! IEU0	Group
	addcc		%g4, %sum, %sum		! IEU1	Group + 1 bubble
	stw		%g4, [%dst - 0x4]	! Store
	bcc,pt		%xcc, ccmerge		! CTI
	 andcc		%len, 0xf0, %g1		! IEU1	Group
	b,pt		%xcc, ccmerge		! CTI	4 clocks (mispredict)
	 add		%sum, 1, %sum		! IEU0

	.align		32
	.globl		csum_partial_copy_sparc64
csum_partial_copy_sparc64:			/* %o0=src, %o1=dest, %o2=len, %o3=sum */
	xorcc		%src, %dst, %o4		! IEU1	Group
	srl		%sum, 0, %sum		! IEU0
	andcc		%o4, 3, %g0		! IEU1	Group
	srl		%len, 0, %len		! IEU0
	bne,pn		%icc, ccslow		! CTI
	 andcc		%src, 1, %g0		! IEU1	Group
	bne,pn		%icc, ccslow		! CTI
	 cmp		%len, 256		! IEU1	Group
	bgeu,pt		%icc, csum_partial_copy_vis ! CTI
	 andcc		%src, 7, %g0		! IEU1	Group
	bne,pn		%icc, cc_fixit		! CTI
	 andcc		%len, 0xf0, %g1		! IEU1	Group
ccmerge:be,pn		%icc, ccte		! CTI
	 andcc		%len, 0xf, %g7		! IEU1	Group
	sll		%g1, 2, %o4		! IEU0
13:	sethi		%hi(12f), %o5		! IEU0	Group
	add		%src, %g1, %src		! IEU1	
	sub		%o5, %o4, %o5		! IEU0	Group
	jmpl		%o5 + %lo(12f), %g0	! CTI	Group brk forced
	 add		%dst, %g1, %dst		! IEU0	Group
cctbl:	CSUMCOPY_LASTCHUNK(0xe8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0xd8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0xc8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0xb8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0xa8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x98,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x88,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x78,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x68,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x58,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x48,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x38,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x28,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x18,%g2,%g3)
	CSUMCOPY_LASTCHUNK(0x08,%g2,%g3)
12:
	andcc		%len, 0xf, %g7		! IEU1	Group
ccte:	bne,pn		%icc, cc_end_cruft	! CTI
	 nop					! IEU0
ccfold:	sllx		%sum, 32, %o0		! IEU0	Group
	addcc		%sum, %o0, %o0		! IEU1	Group (regdep)
	srlx		%o0, 32, %o0		! IEU0	Group (regdep)
	bcs,a,pn	%xcc, 1f		! CTI
	 add		%o0, 1, %o0		! IEU1	4 clocks (mispredict)
1:	retl					! CTI	Group brk forced
	 ldx		[%g6 + TI_TASK], %g4	! Load

ccslow:	mov	0, %g5
	brlez,pn %len, 4f
	 andcc	%src, 1, %o5		
	be,a,pt	%icc, 1f
	 srl	%len, 1, %g7		
	sub	%len, 1, %len	
	lduba [%src] %asi, %g5
	add	%src, 1, %src	
	stb	%g5, [%dst]
	srl	%len, 1, %g7
	add	%dst, 1, %dst
1:	brz,a,pn %g7, 3f
	 andcc	%len, 1, %g0
	andcc	%src, 2, %g0	
	be,a,pt	%icc, 1f
	 srl	%g7, 1, %g7
	lduha [%src] %asi, %o4
	sub	%len, 2, %len	
	srl	%o4, 8, %g2
	sub	%g7, 1, %g7	
	stb	%g2, [%dst]
	add	%o4, %g5, %g5
	stb	%o4, [%dst + 1]
	add	%src, 2, %src	
	srl	%g7, 1, %g7
	add	%dst, 2, %dst
1:	brz,a,pn %g7, 2f		
	 andcc	%len, 2, %g0
	lduwa	[%src] %asi, %o4
5:	srl	%o4, 24, %g2
	srl	%o4, 16, %g3
	stb	%g2, [%dst]
	srl	%o4, 8, %g2
	stb	%g3, [%dst + 1]
	add	%src, 4, %src
	stb	%g2, [%dst + 2]
	addcc	%o4, %g5, %g5
	stb	%o4, [%dst + 3]
	addc	%g5, %g0, %g5
	add	%dst, 4, %dst
	subcc	%g7, 1, %g7
	bne,a,pt %icc, 5b
	 lduwa [%src] %asi, %o4
	sll	%g5, 16, %g2
	srl	%g5, 16, %g5
	srl	%g2, 16, %g2
	andcc	%len, 2, %g0
	add	%g2, %g5, %g5 
2:	be,a,pt	%icc, 3f		
	 andcc	%len, 1, %g0
	lduha [%src] %asi, %o4
	andcc	%len, 1, %g0
	srl	%o4, 8, %g2
	add	%src, 2, %src	
	stb	%g2, [%dst]
	add	%g5, %o4, %g5
	stb	%o4, [%dst + 1]
	add	%dst, 2, %dst
3:	be,a,pt	%icc, 1f		
	 sll	%g5, 16, %o4
	lduba [%src] %asi, %g2
	sll	%g2, 8, %o4	
	stb	%g2, [%dst]
	add	%g5, %o4, %g5
	sll	%g5, 16, %o4
1:	addcc	%o4, %g5, %g5
	srl	%g5, 16, %o4
	addc	%g0, %o4, %g5
	brz,pt	%o5, 4f
	 srl	%g5, 8, %o4
	and	%g5, 0xff, %g2
	and	%o4, 0xff, %o4
	sll	%g2, 8, %g2
	or	%g2, %o4, %g5
4:	addcc	%sum, %g5, %sum
	addc	%g0, %sum, %o0
	retl	
	 srl	%o0, 0, %o0
cpc_end:

	/* Now the version with userspace as the destination */
#define CSUMCOPY_LASTCHUNK_USER(off, t0, t1)						\
	ldx		[%src - off - 0x08], t0;					\
	ldx		[%src - off - 0x00], t1;					\
	nop; nop;									\
	addcc		t0, %sum, %sum;							\
	stwa		t0, [%dst - off - 0x04] %asi;					\
	srlx		t0, 32, t0;							\
	bcc,pt		%xcc, 51f;							\
	 stwa		t0, [%dst - off - 0x08] %asi;					\
	add		%sum, 1, %sum;							\
51:	addcc		t1, %sum, %sum;							\
	stwa		t1, [%dst - off + 0x04] %asi;					\
	srlx		t1, 32, t1;							\
	bcc,pt		%xcc, 52f;							\
	 stwa		t1, [%dst - off - 0x00] %asi;					\
	add		%sum, 1, %sum;							\
52:

cpc_user_start:
cc_user_end_cruft:
	andcc		%g7, 8, %g0		! IEU1	Group
	be,pn		%icc, 1f		! CTI
	 and		%g7, 4, %g5		! IEU0
	ldx		[%src + 0x00], %g2	! Load	Group
	add		%dst, 8, %dst		! IEU0
	add		%src, 8, %src		! IEU1
	addcc		%g2, %sum, %sum		! IEU1	Group + 2 bubbles
	stwa		%g2, [%dst - 0x04] %asi	! Store
	srlx		%g2, 32, %g2		! IEU0
	bcc,pt		%xcc, 1f		! CTI	Group
	 stwa		%g2, [%dst - 0x08] %asi	! Store
	add		%sum, 1, %sum		! IEU0
1:	brz,pt		%g5, 1f			! CTI	Group
	 clr		%g2			! IEU0
	lduw		[%src + 0x00], %g2	! Load
	add		%dst, 4, %dst		! IEU0	Group
	add		%src, 4, %src		! IEU1
	stwa		%g2, [%dst - 0x04] %asi	! Store	Group + 2 bubbles
	sllx		%g2, 32, %g2		! IEU0
1:	andcc		%g7, 2, %g0		! IEU1
	be,pn		%icc, 1f		! CTI	Group
	 clr		%o4			! IEU1
	lduh		[%src + 0x00], %o4	! Load
	add		%src, 2, %src		! IEU0	Group
	add		%dst, 2, %dst		! IEU1
	stha		%o4, [%dst - 0x2] %asi	! Store Group + 2 bubbles
	sll		%o4, 16, %o4		! IEU0
1:	andcc		%g7, 1, %g0		! IEU1
	be,pn		%icc, 1f		! CTI	Group
	 clr		%o5			! IEU0
	ldub		[%src + 0x00], %o5	! Load
	stba		%o5, [%dst + 0x00] %asi	! Store	Group + 2 bubbles
	sll		%o5, 8, %o5		! IEU0
1:	or		%g2, %o4, %o4		! IEU1
	or		%o5, %o4, %o4		! IEU0	Group
	addcc		%o4, %sum, %sum		! IEU1
	bcc,pt		%xcc, ccuserfold	! CTI
	 nop					! IEU0	Group
	b,pt		%xcc, ccuserfold	! CTI
	 add		%sum, 1, %sum		! IEU1

cc_user_fixit:
	cmp		%len, 6			! IEU1	Group
	bl,a,pn		%icc, ccuserte		! CTI
	 andcc		%len, 0xf, %g7		! IEU1	Group
	andcc		%src, 2, %g0		! IEU1	Group
	be,pn		%icc, 1f		! CTI
	 andcc		%src, 0x4, %g0		! IEU1	Group
	lduh		[%src + 0x00], %g4	! Load
	sub		%len, 2, %len		! IEU0
	add		%src, 2, %src		! IEU0	Group
	add		%dst, 2, %dst		! IEU1
	sll		%g4, 16, %g3		! IEU0	Group + 1 bubble
	addcc		%g3, %sum, %sum		! IEU1
	bcc,pt		%xcc, 0f		! CTI
	 srl		%sum, 16, %g3		! IEU0	Group
	add		%g3, 1, %g3		! IEU0	4 clocks (mispredict)
0:	andcc		%src, 0x4, %g0		! IEU1	Group
	stha		%g4, [%dst - 0x2] %asi	! Store
	sll		%sum, 16, %sum		! IEU0
	sll		%g3, 16, %g3		! IEU0	Group
	srl		%sum, 16, %sum		! IEU0	Group
	or		%g3, %sum, %sum		! IEU0	Group (regdep)
1:	be,pt		%icc, ccusermerge	! CTI
	 andcc		%len, 0xf0, %g1		! IEU1
	lduw		[%src + 0x00], %g4	! Load	Group
	sub		%len, 4, %len		! IEU0
	add		%src, 4, %src		! IEU1
	add		%dst, 4, %dst		! IEU0	Group
	addcc		%g4, %sum, %sum		! IEU1	Group + 1 bubble
	stwa		%g4, [%dst - 0x4] %asi	! Store
	bcc,pt		%xcc, ccusermerge	! CTI
	 andcc		%len, 0xf0, %g1		! IEU1	Group
	b,pt		%xcc, ccusermerge	! CTI	4 clocks (mispredict)
	 add		%sum, 1, %sum		! IEU0

	.align		32
	.globl		csum_partial_copy_user_sparc64
csum_partial_copy_user_sparc64:			/* %o0=src, %o1=dest, %o2=len, %o3=sum */
	xorcc		%src, %dst, %o4		! IEU1	Group
	srl		%sum, 0, %sum		! IEU0
	andcc		%o4, 3, %g0		! IEU1	Group
	srl		%len, 0, %len		! IEU0
	bne,pn		%icc, ccuserslow	! CTI
	 andcc		%src, 1, %g0		! IEU1	Group
	bne,pn		%icc, ccuserslow	! CTI
	 cmp		%len, 256		! IEU1	Group
	bgeu,pt		%icc, csum_partial_copy_user_vis ! CTI
	 andcc		%src, 7, %g0		! IEU1	Group
	bne,pn		%icc, cc_user_fixit	! CTI
	 andcc		%len, 0xf0, %g1		! IEU1	Group
ccusermerge:
	be,pn		%icc, ccuserte		! CTI
	 andcc		%len, 0xf, %g7		! IEU1	Group
	sll		%g1, 2, %o4		! IEU0
13:	sethi		%hi(12f), %o5		! IEU0	Group
	add		%src, %g1, %src		! IEU1	
	sub		%o5, %o4, %o5		! IEU0	Group
	jmpl		%o5 + %lo(12f), %g0	! CTI	Group brk forced
	 add		%dst, %g1, %dst		! IEU0	Group
ccusertbl:
	CSUMCOPY_LASTCHUNK_USER(0xe8,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0xd8,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0xc8,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0xb8,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0xa8,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x98,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x88,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x78,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x68,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x58,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x48,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x38,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x28,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x18,%g2,%g3)
	CSUMCOPY_LASTCHUNK_USER(0x08,%g2,%g3)
12:
	andcc		%len, 0xf, %g7		! IEU1	Group
ccuserte:
	bne,pn		%icc, cc_user_end_cruft	! CTI
	 nop					! IEU0
ccuserfold:
	sllx		%sum, 32, %o0		! IEU0	Group
	addcc		%sum, %o0, %o0		! IEU1	Group (regdep)
	srlx		%o0, 32, %o0		! IEU0	Group (regdep)
	bcs,a,pn	%xcc, 1f		! CTI
	 add		%o0, 1, %o0		! IEU1	4 clocks (mispredict)
1:	retl					! CTI	Group brk forced
	 ldx		[%g6 + TI_TASK], %g4	! IEU0	Group

ccuserslow:
	mov	0, %g5
	brlez,pn %len, 4f
	 andcc	%src, 1, %o5		
	be,a,pt	%icc, 1f
	 srl	%len, 1, %g7		
	sub	%len, 1, %len	
	ldub [%src], %g5
	add	%src, 1, %src	
	stba	%g5, [%dst] %asi
	srl	%len, 1, %g7
	add	%dst, 1, %dst
1:	brz,a,pn %g7, 3f
	 andcc	%len, 1, %g0
	andcc	%src, 2, %g0	
	be,a,pt	%icc, 1f
	 srl	%g7, 1, %g7
	lduh [%src], %o4
	sub	%len, 2, %len	
	srl	%o4, 8, %g2
	sub	%g7, 1, %g7	
	stba	%g2, [%dst] %asi
	add	%o4, %g5, %g5
	stba	%o4, [%dst + 1] %asi
	add	%src, 2, %src	
	srl	%g7, 1, %g7
	add	%dst, 2, %dst
1:	brz,a,pn %g7, 2f		
	 andcc	%len, 2, %g0
	lduw	[%src], %o4
5:	srl	%o4, 24, %g2
	srl	%o4, 16, %g3
	stba	%g2, [%dst] %asi
	srl	%o4, 8, %g2
	stba	%g3, [%dst + 1] %asi
	add	%src, 4, %src
	stba	%g2, [%dst + 2] %asi
	addcc	%o4, %g5, %g5
	stba	%o4, [%dst + 3] %asi
	addc	%g5, %g0, %g5
	add	%dst, 4, %dst
	subcc	%g7, 1, %g7
	bne,a,pt %icc, 5b
	 lduw [%src], %o4
	sll	%g5, 16, %g2
	srl	%g5, 16, %g5
	srl	%g2, 16, %g2
	andcc	%len, 2, %g0
	add	%g2, %g5, %g5 
2:	be,a,pt	%icc, 3f		
	 andcc	%len, 1, %g0
	lduh [%src], %o4
	andcc	%len, 1, %g0
	srl	%o4, 8, %g2
	add	%src, 2, %src	
	stba	%g2, [%dst] %asi
	add	%g5, %o4, %g5
	stba	%o4, [%dst + 1] %asi
	add	%dst, 2, %dst
3:	be,a,pt	%icc, 1f		
	 sll	%g5, 16, %o4
	ldub [%src], %g2
	sll	%g2, 8, %o4	
	stba 	%g2, [%dst] %asi
	add	%g5, %o4, %g5
	sll	%g5, 16, %o4
1:	addcc	%o4, %g5, %g5
	srl	%g5, 16, %o4
	addc	%g0, %o4, %g5
	brz,pt	%o5, 4f
	 srl	%g5, 8, %o4
	and	%g5, 0xff, %g2
	and	%o4, 0xff, %o4
	sll	%g2, 8, %g2
	or	%g2, %o4, %g5
4:	addcc	%sum, %g5, %sum
	addc	%g0, %sum, %o0
	retl	
	 srl	%o0, 0, %o0
cpc_user_end:

	.globl	cpc_handler
cpc_handler:
	ldx	[%sp + 0x7ff + 128], %g1
	ldub	[%g6 + TI_CURRENT_DS], %g3
	sub	%g0, EFAULT, %g2
	brnz,a,pt %g1, 1f
	 st	%g2, [%g1]
1:	wr	%g3, %g0, %asi
	retl
	 ldx	[%g6 + TI_TASK], %g4

csum_partial_fix_alignment:
	/* We checked for zero length already, so there must be
	 * at least one byte.
	 */
	be,pt		%icc, 1f
	 nop
	ldub		[%o0 + 0x00], %o4
	add		%o0, 1, %o0
	sub		%o1, 1, %o1
1:	andcc		%o0, 0x2, %g0
	be,pn		%icc, csum_partial_post_align
	 cmp		%o1, 2
	blu,pn		%icc, csum_partial_end_cruft
	 nop
	lduh		[%o0 + 0x00], %o5
	add		%o0, 2, %o0
	sub		%o1, 2, %o1
	ba,pt		%xcc, csum_partial_post_align
	 add		%o5, %o4, %o4

	.align		32
	.globl		csum_partial
csum_partial:		/* %o0=buff, %o1=len, %o2=sum */
	prefetch	[%o0 + 0x000], #one_read
	clr		%o4
	prefetch	[%o0 + 0x040], #one_read
	brz,pn		%o1, csum_partial_finish
	 andcc		%o0, 0x3, %g0

	/* We "remember" whether the lowest bit in the address
	 * was set in %g7.  Because if it is, we have to swap
	 * upper and lower 8 bit fields of the sum we calculate.
	*/
	bne,pn		%icc, csum_partial_fix_alignment
	 andcc		%o0, 0x1, %g7

csum_partial_post_align:
	prefetch	[%o0 + 0x080], #one_read
	andncc		%o1, 0x3f, %o3

	prefetch	[%o0 + 0x0c0], #one_read
	sub		%o1, %o3, %o1
	brz,pn		%o3, 2f
	 prefetch	[%o0 + 0x100], #one_read

	/* So that we don't need to use the non-pairing
	 * add-with-carry instructions we accumulate 32-bit
	 * values into a 64-bit register.  At the end of the
	 * loop we fold it down to 32-bits and so on.
	 */
	prefetch	[%o0 + 0x140], #one_read
1:	lduw		[%o0 + 0x00], %o5
	lduw		[%o0 + 0x04], %g1
	lduw		[%o0 + 0x08], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x0c], %g3
	add		%o4, %g1, %o4
	lduw		[%o0 + 0x10], %o5
	add		%o4, %g2, %o4
	lduw		[%o0 + 0x14], %g1
	add		%o4, %g3, %o4
	lduw		[%o0 + 0x18], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x1c], %g3
	add		%o4, %g1, %o4
	lduw		[%o0 + 0x20], %o5
	add		%o4, %g2, %o4
	lduw		[%o0 + 0x24], %g1
	add		%o4, %g3, %o4
	lduw		[%o0 + 0x28], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x2c], %g3
	add		%o4, %g1, %o4
	lduw		[%o0 + 0x30], %o5
	add		%o4, %g2, %o4
	lduw		[%o0 + 0x34], %g1
	add		%o4, %g3, %o4
	lduw		[%o0 + 0x38], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x3c], %g3
	add		%o4, %g1, %o4
	prefetch	[%o0 + 0x180], #one_read
	add		%o4, %g2, %o4
	subcc		%o3, 0x40, %o3
	add		%o0, 0x40, %o0
	bne,pt		%icc, 1b
	 add		%o4, %g3, %o4

2:	and		%o1, 0x3c, %o3
	brz,pn		%o3, 2f
	 sub		%o1, %o3, %o1
1:	lduw		[%o0 + 0x00], %o5
	subcc		%o3, 0x4, %o3
	add		%o0, 0x4, %o0
	bne,pt		%icc, 1b
	 add		%o4, %o5, %o4

2:
	/* fold 64-->32 */
	srlx		%o4, 32, %o5
	srl		%o4, 0, %o4
	add		%o4, %o5, %o4
	srlx		%o4, 32, %o5
	srl		%o4, 0, %o4
	add		%o4, %o5, %o4

	/* fold 32-->16 */
	sethi		%hi(0xffff0000), %g1
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4

csum_partial_end_cruft:
	/* %o4 has the 16-bit sum we have calculated so-far.  */
	cmp		%o1, 2
	blu,pt		%icc, 1f
	 nop
	lduh		[%o0 + 0x00], %o5
	sub		%o1, 2, %o1
	add		%o0, 2, %o0
	add		%o4, %o5, %o4
1:	brz,pt		%o1, 1f
	 nop
	ldub		[%o0 + 0x00], %o5
	sub		%o1, 1, %o1
	add		%o0, 1, %o0
	sllx		%o5, 8, %o5
	add		%o4, %o5, %o4
1:
	/* fold 32-->16 */
	sethi		%hi(0xffff0000), %g1
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4

1:	brz,pt		%g7, 1f
	 nop

	/* We started with an odd byte, byte-swap the result.  */
	srl		%o4, 8, %o5
	and		%o4, 0xff, %g1
	sll		%g1, 8, %g1
	or		%o5, %g1, %o4

1:	add		%o2, %o4, %o2

csum_partial_finish:
	retl
	 mov		%o2, %o0


	.section __ex_table
	.align  4
	.word	cpc_start, 0, cpc_end, cpc_handler
	.word	cpc_user_start, 0, cpc_user_end, cpc_handler
