/* checksum.S: Sparc V9 optimized checksum code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
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
#define CSUMCOPY_ECACHE_LOAD(off, t0, t1, t2, t3, t4, t5, t6, t7)			\
	ldxa		[%src + off + 0x00] %asi, t0;					\
	ldxa		[%src + off + 0x08] %asi, t1;					\
	ldxa		[%src + off + 0x10] %asi, t2;					\
	ldxa		[%src + off + 0x18] %asi, t3;					\
	ldxa		[%src + off + 0x20] %asi, t4;					\
	ldxa		[%src + off + 0x28] %asi, t5;					\
	ldxa		[%src + off + 0x30] %asi, t6;					\
	ldxa		[%src + off + 0x38] %asi, t7;					\
	nop; nop; /* DO NOT TOUCH THIS!!!!! */

#define CSUMCOPY_EC_STALIGNED_LDNXT(off, t0, t1, t2, t3, t4, t5, t6, t7)		\
	stx		t0, [%dst + off - 0x40];					\
	addcc		%sum, t0, %sum;							\
	bcc,pt		%xcc, 11f;							\
	 ldxa		[%src + off + 0x00] %asi, t0;					\
	add		%sum, 1, %sum;							\
11:	stx		t1, [%dst + off - 0x38];					\
	addcc		%sum, t1, %sum;							\
	bcc,pt		%xcc, 12f;							\
	 ldxa		[%src + off + 0x08] %asi, t1;					\
	add		%sum, 1, %sum;							\
12:	stx		t2, [%dst + off - 0x30];					\
	addcc		%sum, t2, %sum;							\
	bcc,pt		%xcc, 13f;							\
	 ldxa		[%src + off + 0x10] %asi, t2;					\
	add		%sum, 1, %sum;							\
13:	stx		t3, [%dst + off - 0x28];					\
	addcc		%sum, t3, %sum;							\
	bcc,pt		%xcc, 14f;							\
	 ldxa		[%src + off + 0x18] %asi, t3;					\
	add		%sum, 1, %sum;							\
14:	stx		t4, [%dst + off - 0x20];					\
	addcc		%sum, t4, %sum;							\
	bcc,pt		%xcc, 15f;							\
	 ldxa		[%src + off + 0x20] %asi, t4;					\
	add		%sum, 1, %sum;							\
15:	stx		t5, [%dst + off - 0x18];					\
	addcc		%sum, t5, %sum;							\
	bcc,pt		%xcc, 16f;							\
	 ldxa		[%src + off + 0x28] %asi, t5;					\
	add		%sum, 1, %sum;							\
16:	stx		t6, [%dst + off - 0x10];					\
	addcc		%sum, t6, %sum;							\
	bcc,pt		%xcc, 17f;							\
	 ldxa		[%src + off + 0x30] %asi, t6;					\
	add		%sum, 1, %sum;							\
17:	stx		t7, [%dst + off - 0x08];					\
	addcc		%sum, t7, %sum;							\
	bcc,pt		%xcc, 18f;							\
	 ldxa		[%src + off + 0x38] %asi, t7;					\
	add		%sum, 1, %sum;							\
18:

#define CSUMCOPY_EC_STUNALIGN_LDNXT(off, t0, t1, t2, t3, t4, t5, t6, t7)		\
	stw		t0, [%dst + off - 0x3c];					\
	addcc		%sum, t0, %sum;							\
	srlx		t0, 32, t0;							\
	stw		t0, [%dst + off - 0x40];					\
	bcc,pt		%xcc, 21f;							\
	 ldxa		[%src + off + 0x00] %asi, t0;					\
	add		%sum, 1, %sum;							\
21:	stw		t1, [%dst + off - 0x34];					\
	addcc		%sum, t1, %sum;							\
	srlx		t1, 32, t1;							\
	stw		t1, [%dst + off - 0x38];					\
	bcc,pt		%xcc, 22f;							\
	 ldxa		[%src + off + 0x08] %asi, t1;					\
	add		%sum, 1, %sum;							\
22:	stw		t2, [%dst + off - 0x2c];					\
	addcc		%sum, t2, %sum;							\
	srlx		t2, 32, t2;							\
	stw		t2, [%dst + off - 0x30];					\
	bcc,pt		%xcc, 23f;							\
	 ldxa		[%src + off + 0x10] %asi, t2;					\
	add		%sum, 1, %sum;							\
23:	stw		t3, [%dst + off - 0x24];					\
	addcc		%sum, t3, %sum;							\
	srlx		t3, 32, t3;							\
	stw		t3, [%dst + off - 0x28];					\
	bcc,pt		%xcc, 24f;							\
	 ldxa		[%src + off + 0x18] %asi, t3;					\
	add		%sum, 1, %sum;							\
24:	stw		t4, [%dst + off - 0x1c];					\
	addcc		%sum, t4, %sum;							\
	srlx		t4, 32, t4;							\
	stw		t4, [%dst + off - 0x20];					\
	bcc,pt		%xcc, 25f;							\
	 ldxa		[%src + off + 0x20] %asi, t4;					\
	add		%sum, 1, %sum;							\
25:	stw		t5, [%dst + off - 0x14];					\
	addcc		%sum, t5, %sum;							\
	srlx		t5, 32, t5;							\
	stw		t5, [%dst + off - 0x18];					\
	bcc,pt		%xcc, 26f;							\
	 ldxa		[%src + off + 0x28] %asi, t5;					\
	add		%sum, 1, %sum;							\
26:	stw		t6, [%dst + off - 0x0c];					\
	addcc		%sum, t6, %sum;							\
	srlx		t6, 32, t6;							\
	stw		t6, [%dst + off - 0x10];					\
	bcc,pt		%xcc, 27f;							\
	 ldxa		[%src + off + 0x30] %asi, t6;					\
	add		%sum, 1, %sum;							\
27:	stw		t7, [%dst + off - 0x04];					\
	addcc		%sum, t7, %sum;							\
	srlx		t7, 32, t7;							\
	stw		t7, [%dst + off - 0x08];					\
	bcc,pt		%xcc, 28f;							\
	 ldxa		[%src + off + 0x38] %asi, t7;					\
	add		%sum, 1, %sum;							\
28:

#define CSUMCOPY_EC_STALIGNED(off, t0, t1, t2, t3, t4, t5, t6, t7)			\
	addcc		%sum, t0, %sum;							\
	bcc,pt		%xcc, 31f;							\
	 stx		t0, [%dst + off + 0x00];					\
	add		%sum, 1, %sum;							\
31:	addcc		%sum, t1, %sum;							\
	bcc,pt		%xcc, 32f;							\
	 stx		t1, [%dst + off + 0x08];					\
	add		%sum, 1, %sum;							\
32:	addcc		%sum, t2, %sum;							\
	bcc,pt		%xcc, 33f;							\
	 stx		t2, [%dst + off + 0x10];					\
	add		%sum, 1, %sum;							\
33:	addcc		%sum, t3, %sum;							\
	bcc,pt		%xcc, 34f;							\
	 stx		t3, [%dst + off + 0x18];					\
	add		%sum, 1, %sum;							\
34:	addcc		%sum, t4, %sum;							\
	bcc,pt		%xcc, 35f;							\
	 stx		t4, [%dst + off + 0x20];					\
	add		%sum, 1, %sum;							\
35:	addcc		%sum, t5, %sum;							\
	bcc,pt		%xcc, 36f;							\
	 stx		t5, [%dst + off + 0x28];					\
	add		%sum, 1, %sum;							\
36:	addcc		%sum, t6, %sum;							\
	bcc,pt		%xcc, 37f;							\
	 stx		t6, [%dst + off + 0x30];					\
	add		%sum, 1, %sum;							\
37:	addcc		%sum, t7, %sum;							\
	bcc,pt		%xcc, 38f;							\
	 stx		t7, [%dst + off + 0x38];					\
	add		%sum, 1, %sum;							\
38:

#define CSUMCOPY_EC_STUNALIGN(off, t0, t1, t2, t3, t4, t5, t6, t7)			\
	stw		t0, [%dst + off + 0x04];					\
	addcc		%sum, t0, %sum;							\
	srlx		t0, 32, t0;							\
	bcc,pt		%xcc, 41f;							\
	 stw		t0, [%dst + off + 0x00];					\
	add		%sum, 1, %sum;							\
41:	stw		t1, [%dst + off + 0x0c];					\
	addcc		%sum, t1, %sum;							\
	srlx		t1, 32, t1;							\
	bcc,pt		%xcc, 42f;							\
	 stw		t1, [%dst + off + 0x08];					\
	add		%sum, 1, %sum;							\
42:	stw		t2, [%dst + off + 0x14];					\
	addcc		%sum, t2, %sum;							\
	srlx		t2, 32, t2;							\
	bcc,pt		%xcc, 43f;							\
	 stw		t2, [%dst + off + 0x10];					\
	add		%sum, 1, %sum;							\
43:	stw		t3, [%dst + off + 0x1c];					\
	addcc		%sum, t3, %sum;							\
	srlx		t3, 32, t3;							\
	bcc,pt		%xcc, 44f;							\
	 stw		t3, [%dst + off + 0x18];					\
	add		%sum, 1, %sum;							\
44:	stw		t4, [%dst + off + 0x24];					\
	addcc		%sum, t4, %sum;							\
	srlx		t4, 32, t4;							\
	bcc,pt		%xcc, 45f;							\
	 stw		t4, [%dst + off + 0x20];					\
	add		%sum, 1, %sum;							\
45:	stw		t5, [%dst + off + 0x2c];					\
	addcc		%sum, t5, %sum;							\
	srlx		t5, 32, t5;							\
	bcc,pt		%xcc, 46f;							\
	 stw		t5, [%dst + off + 0x28];					\
	add		%sum, 1, %sum;							\
46:	stw		t6, [%dst + off + 0x34];					\
	addcc		%sum, t6, %sum;							\
	srlx		t6, 32, t6;							\
	bcc,pt		%xcc, 47f;							\
	 stw		t6, [%dst + off + 0x30];					\
	add		%sum, 1, %sum;							\
47:	stw		t7, [%dst + off + 0x3c];					\
	addcc		%sum, t7, %sum;							\
	srlx		t7, 32, t7;							\
	bcc,pt		%xcc, 48f;							\
	 stw		t7, [%dst + off + 0x38];					\
	add		%sum, 1, %sum;							\
48:

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
	 sethi		%uhi(PAGE_OFFSET), %g4	! IEU0	Group
	b,pt		%xcc, ccfold		! CTI
	 add		%sum, 1, %sum		! IEU1

cc_fixit:
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
1:	be,pt		%icc, cc_dword_aligned	! CTI
	 andn		%len, 0xff, %g2		! IEU1
	lduwa		[%src + 0x00] %asi, %g4	! Load	Group
	sub		%len, 4, %len		! IEU0
	add		%src, 4, %src		! IEU1
	add		%dst, 4, %dst		! IEU0	Group
	addcc		%g4, %sum, %sum		! IEU1	Group + 1 bubble
	stw		%g4, [%dst - 0x4]	! Store
	bcc,pt		%xcc, cc_dword_aligned	! CTI
	 andn		%len, 0xff, %g2		! IEU0	Group
	b,pt		%xcc, cc_dword_aligned	! CTI	4 clocks (mispredict)
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
	be,pt		%icc, cc_dword_aligned	! CTI
	 andn		%len, 0xff, %g2		! IEU0
	b,pt		%xcc, cc_fixit		! CTI	Group
	 cmp		%len, 6			! IEU1
cc_dword_aligned:
	brz,pn		%g2, 3f			! CTI	Group
	 andcc		%dst, 4, %g0		! IEU1	Group (brz uses IEU1)
	be,pn		%icc, ccdbl + 4		! CTI
5:	CSUMCOPY_ECACHE_LOAD(       0x00,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STUNALIGN_LDNXT(0x40,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STUNALIGN_LDNXT(0x80,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STUNALIGN_LDNXT(0xc0,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STUNALIGN(      0xc0,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
10:
	sub		%len, 256, %len		! IEU0	Group
	add		%src, 256, %src		! IEU1
	andncc		%len, 0xff, %g0		! IEU1	Group
	bne,pt		%icc, 5b		! CTI
	 add		%dst, 256, %dst		! IEU0
3:	andcc		%len, 0xf0, %g1		! IEU1	Group
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
	 sethi		%uhi(PAGE_OFFSET), %g4	! IEU0
ccfold:	sllx		%sum, 32, %o0		! IEU0	Group
	addcc		%sum, %o0, %o0		! IEU1	Group (regdep)
	srlx		%o0, 32, %o0		! IEU0	Group (regdep)
	bcs,a,pn	%xcc, 1f		! CTI
	 add		%o0, 1, %o0		! IEU1	4 clocks (mispredict)
1:	retl					! CTI	Group brk forced
	 sllx		%g4, 32,%g4		! IEU0	Group
ccdbl:	CSUMCOPY_ECACHE_LOAD(       0x00,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STALIGNED_LDNXT(0x40,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STALIGNED_LDNXT(0x80,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STALIGNED_LDNXT(0xc0,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
	CSUMCOPY_EC_STALIGNED(      0xc0,%o4,%o5,%g2,%g3,%g4,%g5,%g1,%g7)
11:
	sub		%len, 256, %len		! IEU0	Group
	add		%src, 256, %src		! IEU1
	andncc		%len, 0xff, %g0		! IEU1	Group	
	bne,pt		%icc, ccdbl		! CTI
	 add		%dst, 256, %dst		! IEU0
	b,pt		%xcc, ccmerge		! CTI	Group
	 andcc		%len, 0xf0, %g1		! IEU1

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

	.globl	cpc_handler
cpc_handler:
	ldx	[%sp + 0x7ff + 128], %g1
	sub	%g0, EFAULT, %g2
	brnz,a,pt %g1, 1f
	 st	%g2, [%g1]
1:	retl
	 nop

	.section __ex_table
	.align  4
	.word  cpc_start, 0, cpc_end, cpc_handler

