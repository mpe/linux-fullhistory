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
	 *
	 * This should run at max bandwidth for ecache hits, a better
	 * technique is to use VIS and fpu operations somehow, but
	 * this requires more reasoning on my part...
	 *
	 * Assuming ecache hits and branches predicted well, this
	 * can be expected to run at a rate of 16 cycles per 64-bytes
	 * of data summed.  (the old code summed 32 bytes in 20
	 * cycles, with numerous bubbles and unnecessary stalls)
	 */
#define CSUM_ECACHE_LOAD(buf, offset, t0, t1, t2, t3, t4, t5, t6, t7)			\
	ldx	[buf + offset + 0x00], t0;						\
	ldx	[buf + offset + 0x08], t1;						\
	ldx	[buf + offset + 0x10], t2;						\
	ldx	[buf + offset + 0x18], t3;						\
	ldx	[buf + offset + 0x20], t4;						\
	ldx	[buf + offset + 0x28], t5;						\
	ldx	[buf + offset + 0x30], t6;						\
	ldx	[buf + offset + 0x38], t7;						\
	nop; nop;	/* THIS IS CRITICAL!!!!!!!!! */

#define CSUM_ECACHE_BLOCK_LDNEXT(buf, offset, sum, t0, t1, t2, t3, t4, t5, t6, t7)	\
	addcc		sum, t0, sum;							\
	bcc,pt		%xcc, 11f;							\
	 ldx		[buf + offset + 0x00], t0;					\
	add		sum, 1, sum;							\
11:	addcc		sum, t1, sum;							\
	bcc,pt		%xcc, 12f;							\
	 ldx		[buf + offset + 0x08], t1;					\
	add		sum, 1, sum;							\
12:	addcc		sum, t2, sum;							\
	bcc,pt		%xcc, 13f;							\
	 ldx		[buf + offset + 0x10], t2;					\
	add		sum, 1, sum;							\
13:	addcc		sum, t3, sum;							\
	bcc,pt		%xcc, 14f;							\
	 ldx		[buf + offset + 0x18], t3;					\
	add		sum, 1, sum;							\
14:	addcc		sum, t4, sum;							\
	bcc,pt		%xcc, 15f;							\
	 ldx		[buf + offset + 0x20], t4;					\
	add		sum, 1, sum;							\
15:	addcc		sum, t5, sum;							\
	bcc,pt		%xcc, 16f;							\
	 ldx		[buf + offset + 0x28], t5;					\
	add		sum, 1, sum;							\
16:	addcc		sum, t6, sum;							\
	bcc,pt		%xcc, 17f;							\
	 ldx		[buf + offset + 0x30], t6;					\
	add		sum, 1, sum;							\
17:	addcc		sum, t7, sum;							\
	bcc,pt		%xcc, 18f;							\
	 ldx		[buf + offset + 0x38], t7;					\
	add		sum, 1, sum;							\
18:	nop; nop;	/* DO NOT TOUCH! */

#define CSUM_ECACHE_BLOCK(sum, t0, t1, t2, t3, t4, t5, t6, t7)				\
	addcc		sum, t0, sum;							\
	bcs,a,pn	%xcc, 21f;							\
	 add		sum, 1, sum;							\
21:	addcc		sum, t1, sum;							\
	bcs,a,pn	%xcc, 22f;							\
	 add		sum, 1, sum;							\
22:	addcc		sum, t2, sum;							\
	bcs,a,pn	%xcc, 23f;							\
	 add		sum, 1, sum;							\
23:	addcc		sum, t3, sum;							\
	bcs,a,pn	%xcc, 24f;							\
	 add		sum, 1, sum;							\
24:	addcc		sum, t4, sum;							\
	bcs,a,pn	%xcc, 25f;							\
	 add		sum, 1, sum;							\
25:	addcc		sum, t5, sum;							\
	bcs,a,pn	%xcc, 26f;							\
	 add		sum, 1, sum;							\
26:	addcc		sum, t6, sum;							\
	bcs,a,pn	%xcc, 27f;							\
	 add		sum, 1, sum;							\
27:	addcc		sum, t7, sum;							\
	bcs,a,pn	%xcc, 28f;							\
	 add		sum, 1, sum;							\
28:

#define CSUM_LASTCHUNK(buf, offset, sum, t0, t1)					\
	ldx		[buf - offset - 0x08], t0;					\
	ldx		[buf - offset - 0x00], t1;					\
	addcc		t0, sum, sum;							\
	bcs,a,pn	%xcc, 31f;							\
	 add		sum, 1, sum;							\
31:	addcc		t1, sum, sum;							\
	bcs,a,pn	%xcc, 32f;							\
	 add		sum, 1, sum;							\
32:

	.text
	/* Keep this garbage from swiping the icache. */
csum_partial_end_cruft:
	andcc		%o1, 8, %g0			! IEU1	Group
	be,pn		%icc, 1f			! CTI
	 and		%o1, 4, %g5			! IEU0
	ldx		[%o0 + 0x00], %g2		! Load	Group
	add		%o0, 0x8, %o0			! IEU0
	addcc		%g2, %o2, %o2			! IEU1	Group + 2 bubbles
	bcs,a,pn	%xcc, 1f			! CTI
	 add		%o2, 1, %o2			! IEU0	4 clocks (mispredict)
1:	andcc		%o1, 2, %g0			! IEU1	Group
	brz,pn		%g5, 1f				! CTI	Group (needs IEU1)
	 clr		%g2				! IEU0
	ld		[%o0], %g2			! Load
	add		%o0, 4, %o0			! IEU0	Group
	sllx		%g2, 32, %g2			! IEU0	Group + 2 bubbles
1:	and		%o1, 1, %o1			! IEU1
	be,pn		%icc, 1f			! CTI
	 clr		%o4				! IEU0	Group
	lduh		[%o0], %o4			! Load
	add		%o0, 2, %o0			! IEU1
	sll		%o4, 16, %o4			! IEU0	Group + 2 bubbles
1:	brz,pn		%o1, 1f				! CTI
	 clr		%o5				! IEU1
	ldub		[%o0], %o5			! Load	Group
	sll		%o5, 8, %o5			! IEU0	Group + 2 bubbles
1:	or		%g2, %o4, %o4			! IEU1
	or		%o5, %o4, %o4			! IEU0	Group
	addcc		%o4, %o2, %o2			! IEU1	Group (regdep)
	bcc,pt		%xcc, cfold			! CTI
	 sethi		%uhi(PAGE_OFFSET), %g4		! IEU0
1:	b,pt		%xcc, cfold			! CTI	Group
	 add		%o2, 1, %o2			! IEU0

csum_partial_fixit:
	bl,pn		%icc, cpte			! CTI	Group
	 and		%o1, 0xf, %o3			! IEU0
	andcc		%o0, 0x2, %g0			! IEU1
	be,pn		%icc, 1f			! CTI	Group
	 and		%o0, 0x4, %g7			! IEU0
	lduh		[%o0 + 0x00], %g2		! Load
	sub		%o1, 2, %o1			! IEU0	Group
	addcc		%o0, 2, %o0			! IEU1
	and		%o0, 0x4, %g7			! IEU0	Group
	sll		%g2, 16, %g2			! IEU0	Group (no load stall)
	addcc		%g2, %o2, %o2			! IEU1	Group (regdep)
	bcc,pt		%icc, 0f			! CTI
	 andn		%o1, 0xff, %o3			! IEU0
	srl		%o2, 16, %g2			! IEU0	Group
	b,pt		%xcc, 9f			! CTI
	 add		%g2, 1, %g2			! IEU1
0:	srl		%o2, 16, %g2			! IEU0	Group 8-(
9:	sll		%o2, 16, %o2			! IEU0	Group 8-(
	sll		%g2, 16, %g3			! IEU0	Group 8-(
	srl		%o2, 16, %o2			! IEU0	Group 8-(
	or		%g3, %o2, %o2			! IEU1
1:	brnz,pt		%g7, 2f				! CTI	Group
	 sub		%o1, 4, %o1			! IEU0
	b,pt		%xcc, csum_partial_aligned	! CTI	Group
	 add		%o1, 4, %o1			! IEU0
2:	ld		[%o0 + 0x00], %g2		! Load	Group
	add		%o0, 4, %o0			! IEU0
	andn		%o1, 0xff, %o3			! IEU1
	addcc		%g2, %o2, %o2			! IEU1	Group + 2 bubbles
	bcc,pt		%xcc, csum_partial_aligned	! CTI
	 nop						! IEU0
	b,pt		%xcc, csum_partial_aligned	! CTI	Group
	 add		%o2, 1, %o2			! IEU0

	.align		32
	.globl		csum_partial
csum_partial:						/* %o0=buf, %o1=len, %o2=sum */
	andcc		%o0, 0x7, %g0			! IEU1	Group
	srl		%o1, 0, %o1			! IEU0
	srl		%o2, 0, %o2			! IEU0	Group
	be,pt		%icc, csum_partial_aligned	! CTI
	 andn		%o1, 0xff, %o3			! IEU1
	b,pt		%xcc, csum_partial_fixit	! CTI	Group
	 cmp		%o1, 6				! IEU0
	nop
csum_partial_aligned:
	brz,pt		%o3, 3f				! CTI	Group
	 and		%o1, 0xf0, %g1			! IEU0
5:	CSUM_ECACHE_LOAD(        %o0, 0x000,      %o4, %o5, %g2, %g3, %g4, %g5, %g1, %g7)
	CSUM_ECACHE_BLOCK_LDNEXT(%o0, 0x040, %o2, %o4, %o5, %g2, %g3, %g4, %g5, %g1, %g7)
	CSUM_ECACHE_BLOCK_LDNEXT(%o0, 0x080, %o2, %o4, %o5, %g2, %g3, %g4, %g5, %g1, %g7)
	CSUM_ECACHE_BLOCK_LDNEXT(%o0, 0x0c0, %o2, %o4, %o5, %g2, %g3, %g4, %g5, %g1, %g7)
	CSUM_ECACHE_BLOCK(                   %o2, %o4, %o5, %g2, %g3, %g4, %g5, %g1, %g7)
	subcc		%o3, 256, %o3			! IEU1	Group
	bne,pt		%icc, 5b			! CTI
	 add		%o0, 256, %o0			! IEU0
	and		%o1, 0xf0, %g1			! IEU0	Group
3:	brz,pn		%g1, cpte			! CTI
	 and		%o1, 0xf, %o3			! IEU1	Group
10:	rd		%pc, %g7			! LSU	Group + 4 clocks
	sll		%g1, 1, %o4			! IEU0	Group
	sub		%g7, %o4, %g7			! IEU1
	jmp		%g7 + %lo(cpte - 10b)		! CTI	Group brk forced
	 add		%o0, %g1, %o0			! IEU0
cptbl:	CSUM_LASTCHUNK(%o0, 0xe8, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0xd8, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0xc8, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0xb8, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0xa8, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x98, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x88, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x78, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x68, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x58, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x48, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x38, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x28, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x18, %o2, %g2, %g3)
	CSUM_LASTCHUNK(%o0, 0x08, %o2, %g2, %g3)
cpte:	brnz,pn		%o3, csum_partial_end_cruft	! CTI	Group
	 sethi		%uhi(PAGE_OFFSET), %g4		! IEU0
cfold:	sllx		%o2, 32, %o0			! IEU0	Group
	addcc		%o2, %o0, %o0			! IEU1	Group (regdep)
	srlx		%o0, 32, %o0			! IEU0	Group (regdep)
	bcs,a,pn	%xcc, 1f			! CTI
	 add		%o0, 1, %o0			! IEU1	4 clocks (mispredict)
1:	retl						! CTI	Group brk forced
	 sllx		%g4, 32, %g4			! IEU0	Group

	.globl __csum_partial_copy_start, __csum_partial_copy_end
__csum_partial_copy_start:

	/* I think I have an erection...  Once _AGAIN_ the SunSoft
	 * engineers are caught asleep at the keyboard, tsk tsk...
	 */
#define CSUMCOPY_ECACHE_LOAD(src, off, t0, t1, t2, t3, t4, t5, t6, t7)			\
	ldxa		[src + off + 0x00] %asi, t0;					\
	ldxa		[src + off + 0x08] %asi, t1;					\
	ldxa		[src + off + 0x10] %asi, t2;					\
	ldxa		[src + off + 0x18] %asi, t3;					\
	ldxa		[src + off + 0x20] %asi, t4;					\
	ldxa		[src + off + 0x28] %asi, t5;					\
	ldxa		[src + off + 0x30] %asi, t6;					\
	ldxa		[src + off + 0x38] %asi, t7;					\
	nop; nop; /* DO NOT TOUCH THIS!!!!! */

#define CSUMCOPY_EC_STALIGNED_LDNXT(src, dest, off, sum, t0, t1, t2, t3, t4, t5, t6, t7)\
	stx		t0, [dest + off - 0x40];					\
	addcc		sum, t0, sum;							\
	bcc,pt		%xcc, 11f;							\
	 ldxa		[src + off + 0x00] %asi, t0;					\
	add		sum, 1, sum;							\
11:	stx		t1, [dest + off - 0x38];					\
	addcc		sum, t1, sum;							\
	bcc,pt		%xcc, 12f;							\
	 ldxa		[src + off + 0x08] %asi, t1;					\
	add		sum, 1, sum;							\
12:	stx		t2, [dest + off - 0x30];					\
	addcc		sum, t2, sum;							\
	bcc,pt		%xcc, 13f;							\
	 ldxa		[src + off + 0x10] %asi, t2;					\
	add		sum, 1, sum;							\
13:	stx		t3, [dest + off - 0x28];					\
	addcc		sum, t3, sum;							\
	bcc,pt		%xcc, 14f;							\
	 ldxa		[src + off + 0x18] %asi, t3;					\
	add		sum, 1, sum;							\
14:	stx		t4, [dest + off - 0x20];					\
	addcc		sum, t4, sum;							\
	bcc,pt		%xcc, 15f;							\
	 ldxa		[src + off + 0x20] %asi, t4;					\
	add		sum, 1, sum;							\
15:	stx		t5, [dest + off - 0x18];					\
	addcc		sum, t5, sum;							\
	bcc,pt		%xcc, 16f;							\
	 ldxa		[src + off + 0x28] %asi, t5;					\
	add		sum, 1, sum;							\
16:	stx		t6, [dest + off - 0x10];					\
	addcc		sum, t6, sum;							\
	bcc,pt		%xcc, 17f;							\
	 ldxa		[src + off + 0x30] %asi, t6;					\
	add		sum, 1, sum;							\
17:	stx		t7, [dest + off - 0x08];					\
	addcc		sum, t7, sum;							\
	bcc,pt		%xcc, 18f;							\
	 ldxa		[src + off + 0x38] %asi, t7;					\
	add		sum, 1, sum;							\
18:

#define CSUMCOPY_EC_STUNALIGN_LDNXT(src, dest, off, sum, t0, t1, t2, t3, t4, t5, t6, t7)\
	stw		t0, [dest + off - 0x3c];					\
	addcc		sum, t0, sum;							\
	srlx		t0, 32, t0;							\
	stw		t0, [dest + off - 0x40];					\
	bcc,pt		%xcc, 21f;							\
	 ldxa		[src + off + 0x00] %asi, t0;					\
	add		sum, 1, sum;							\
21:	stw		t1, [dest + off - 0x34];					\
	addcc		sum, t1, sum;							\
	srlx		t1, 32, t1;							\
	stw		t1, [dest + off - 0x38];					\
	bcc,pt		%xcc, 22f;							\
	 ldxa		[src + off + 0x08] %asi, t1;					\
	add		sum, 1, sum;							\
22:	stw		t2, [dest + off - 0x2c];					\
	addcc		sum, t2, sum;							\
	srlx		t2, 32, t2;							\
	stw		t2, [dest + off - 0x30];					\
	bcc,pt		%xcc, 23f;							\
	 ldxa		[src + off + 0x10] %asi, t2;					\
	add		sum, 1, sum;							\
23:	stw		t3, [dest + off - 0x24];					\
	addcc		sum, t3, sum;							\
	srlx		t3, 32, t3;							\
	stw		t3, [dest + off - 0x28];					\
	bcc,pt		%xcc, 24f;							\
	 ldxa		[src + off + 0x18] %asi, t3;					\
	add		sum, 1, sum;							\
24:	stw		t4, [dest + off - 0x1c];					\
	addcc		sum, t4, sum;							\
	srlx		t4, 32, t4;							\
	stw		t4, [dest + off - 0x20];					\
	bcc,pt		%xcc, 25f;							\
	 ldxa		[src + off + 0x20] %asi, t4;					\
	add		sum, 1, sum;							\
25:	stw		t5, [dest + off - 0x14];					\
	addcc		sum, t5, sum;							\
	srlx		t5, 32, t5;							\
	stw		t5, [dest + off - 0x18];					\
	bcc,pt		%xcc, 26f;							\
	 ldxa		[src + off + 0x28] %asi, t5;					\
	add		sum, 1, sum;							\
26:	stw		t6, [dest + off - 0x0c];					\
	addcc		sum, t6, sum;							\
	srlx		t6, 32, t6;							\
	stw		t6, [dest + off - 0x10];					\
	bcc,pt		%xcc, 27f;							\
	 ldxa		[src + off + 0x30] %asi, t6;					\
	add		sum, 1, sum;							\
27:	stw		t7, [dest + off - 0x04];					\
	addcc		sum, t7, sum;							\
	srlx		t7, 32, t7;							\
	stw		t7, [dest + off - 0x08];					\
	bcc,pt		%xcc, 28f;							\
	 ldxa		[src + off + 0x38] %asi, t7;					\
	add		sum, 1, sum;							\
28:

#define CSUMCOPY_EC_STALIGNED(dest, off, sum, t0, t1, t2, t3, t4, t5, t6, t7)		\
	addcc		sum, t0, sum;							\
	bcc,pt		%xcc, 31f;							\
	 stx		t0, [dest + off + 0x00];					\
	add		sum, 1, sum;							\
31:	addcc		sum, t1, sum;							\
	bcc,pt		%xcc, 32f;							\
	 stx		t1, [dest + off + 0x08];					\
	add		sum, 1, sum;							\
32:	addcc		sum, t2, sum;							\
	bcc,pt		%xcc, 33f;							\
	 stx		t2, [dest + off + 0x10];					\
	add		sum, 1, sum;							\
33:	addcc		sum, t3, sum;							\
	bcc,pt		%xcc, 34f;							\
	 stx		t3, [dest + off + 0x18];					\
	add		sum, 1, sum;							\
34:	addcc		sum, t4, sum;							\
	bcc,pt		%xcc, 35f;							\
	 stx		t4, [dest + off + 0x20];					\
	add		sum, 1, sum;							\
35:	addcc		sum, t5, sum;							\
	bcc,pt		%xcc, 36f;							\
	 stx		t5, [dest + off + 0x28];					\
	add		sum, 1, sum;							\
36:	addcc		sum, t6, sum;							\
	bcc,pt		%xcc, 37f;							\
	 stx		t6, [dest + off + 0x30];					\
	add		sum, 1, sum;							\
37:	addcc		sum, t7, sum;							\
	bcc,pt		%xcc, 38f;							\
	 stx		t7, [dest + off + 0x38];					\
	add		sum, 1, sum;							\
38:

#define CSUMCOPY_EC_STUNALIGN(dest, off, sum, t0, t1, t2, t3, t4, t5, t6, t7)		\
	stw		t0, [dest + off + 0x04];					\
	addcc		sum, t0, sum;							\
	srlx		t0, 32, t0;							\
	bcc,pt		%xcc, 41f;							\
	 stw		t0, [dest + off + 0x00];					\
	add		sum, 1, sum;							\
41:	stw		t1, [dest + off + 0x0c];					\
	addcc		sum, t1, sum;							\
	srlx		t1, 32, t1;							\
	bcc,pt		%xcc, 42f;							\
	 stw		t1, [dest + off + 0x08];					\
	add		sum, 1, sum;							\
42:	stw		t2, [dest + off + 0x14];					\
	addcc		sum, t2, sum;							\
	srlx		t2, 32, t2;							\
	bcc,pt		%xcc, 43f;							\
	 stw		t2, [dest + off + 0x10];					\
	add		sum, 1, sum;							\
43:	stw		t3, [dest + off + 0x1c];					\
	addcc		sum, t3, sum;							\
	srlx		t3, 32, t3;							\
	bcc,pt		%xcc, 44f;							\
	 stw		t3, [dest + off + 0x18];					\
	add		sum, 1, sum;							\
44:	stw		t4, [dest + off + 0x24];					\
	addcc		sum, t4, sum;							\
	srlx		t4, 32, t4;							\
	bcc,pt		%xcc, 45f;							\
	 stw		t4, [dest + off + 0x20];					\
	add		sum, 1, sum;							\
45:	stw		t5, [dest + off + 0x2c];					\
	addcc		sum, t5, sum;							\
	srlx		t5, 32, t5;							\
	bcc,pt		%xcc, 46f;							\
	 stw		t5, [dest + off + 0x28];					\
	add		sum, 1, sum;							\
46:	stw		t6, [dest + off + 0x34];					\
	addcc		sum, t6, sum;							\
	srlx		t6, 32, t6;							\
	bcc,pt		%xcc, 47f;							\
	 stw		t6, [dest + off + 0x30];					\
	add		sum, 1, sum;							\
47:	stw		t7, [dest + off + 0x3c];					\
	addcc		sum, t7, sum;							\
	srlx		t7, 32, t7;							\
	bcc,pt		%xcc, 48f;							\
	 stw		t7, [dest + off + 0x38];					\
	add		sum, 1, sum;							\
48:

#define CSUMCOPY_LASTCHUNK(src, dst, sum, off, t0, t1)					\
	ldxa		[src - off - 0x08] %asi, t0;					\
	ldxa		[src - off - 0x00] %asi, t1;					\
	nop; nop;									\
	addcc		t0, sum, sum;							\
	stw		t0, [dst - off - 0x04];						\
	srlx		t0, 32, t0;							\
	bcc,pt		%xcc, 51f;							\
	 stw		t0, [dst - off - 0x08];						\
	add		sum, 1, sum;							\
51:	addcc		t1, sum, sum;							\
	stw		t1, [dst - off + 0x04];						\
	srlx		t1, 32, t1;							\
	bcc,pt		%xcc, 52f;							\
	 stw		t1, [dst - off - 0x00];						\
	add		sum, 1, sum;							\
52:

cc_end_cruft:
	andcc		%o3, 8, %g0		! IEU1	Group
	be,pn		%icc, 1f		! CTI
	 and		%o3, 4, %g5		! IEU0
	ldxa		[%o0 + 0x00] %asi, %g2	! Load	Group
	add		%o1, 8, %o1		! IEU0
	add		%o0, 8, %o0		! IEU1
	addcc		%g2, %g7, %g7		! IEU1	Group + 2 bubbles
	stw		%g2, [%o1 - 0x04]	! Store
	srlx		%g2, 32, %g2		! IEU0
	bcc,pt		%xcc, 1f		! CTI	Group
	 stw		%g2, [%o1 - 0x08]	! Store
	add		%g7, 1, %g7		! IEU0
1:	brz,pt		%g5, 1f			! CTI	Group
	 clr		%g2			! IEU0
	lduwa		[%o0 + 0x00] %asi, %g2	! Load
	add		%o1, 4, %o1		! IEU0	Group
	add		%o0, 4, %o0		! IEU1
	stw		%g2, [%o1 - 0x04]	! Store	Group + 2 bubbles
	sllx		%g2, 32, %g2		! IEU0
1:	andcc		%o3, 2, %g0		! IEU1
	be,pn		%icc, 1f		! CTI	Group
	 clr		%o4			! IEU1
	lduha		[%o0 + 0x00] %asi, %o4	! Load
	add		%o0, 2, %o0		! IEU0	Group
	add		%o1, 2, %o1		! IEU1
	sth		%o4, [%o1 - 0x2]	! Store Group + 2 bubbles
	sll		%o4, 16, %o4		! IEU0
1:	andcc		%o3, 1, %g0		! IEU1
	be,pn		%icc, 1f		! CTI	Group
	 clr		%o5			! IEU0
	lduba		[%o0 + 0x00] %asi, %o5	! Load
	stb		%o5, [%o1 + 0x00]	! Store	Group + 2 bubbles
	sll		%o5, 8, %o5		! IEU0
1:	or		%g2, %o4, %o4		! IEU1
	or		%o5, %o4, %o4		! IEU0	Group
	addcc		%o4, %g7, %g7		! IEU1
	bcc,pt		%xcc, ccfold		! CTI
	 sethi		%uhi(PAGE_OFFSET), %g4	! IEU0	Group
	b,pt		%xcc, ccfold		! CTI
	 add		%g7, 1, %g7		! IEU1

cc_fixit:
	bl,a,pn		%icc, ccte		! CTI
	 andcc		%g1, 0xf, %o3		! IEU1	Group
	andcc		%o0, 1, %g0		! IEU1	Group
	bne,pn		%icc, ccslow		! CTI
	 andcc		%o0, 2, %g0		! IEU1	Group
	be,pn		%icc, 1f		! CTI
	 andcc		%o0, 0x4, %g0		! IEU1	Group
	lduha		[%o0 + 0x00] %asi, %g4	! Load
	sub		%g1, 2, %g1		! IEU0
	add		%o0, 2, %o0		! IEU0	Group
	add		%o1, 2, %o1		! IEU1
	sll		%g4, 16, %g3		! IEU0	Group + 1 bubble
	addcc		%g3, %g7, %g7		! IEU1
	bcc,pt		%xcc, 0f		! CTI
	 srl		%g7, 16, %g3		! IEU0	Group
	add		%g3, 1, %g3		! IEU0	4 clocks (mispredict)
0:	andcc		%o0, 0x4, %g0		! IEU1	Group
	sth		%g4, [%o1 - 0x2]	! Store
	sll		%g7, 16, %g7		! IEU0
	sll		%g3, 16, %g3		! IEU0	Group
	srl		%g7, 16, %g7		! IEU0	Group
	or		%g3, %g7, %g7		! IEU0	Group (regdep)
1:	be,pt		%icc, cc_dword_aligned	! CTI
	 andn		%g1, 0xff, %g2		! IEU1
	lduwa		[%o0 + 0x00] %asi, %g4	! Load	Group
	sub		%g1, 4, %g1		! IEU0
	add		%o0, 4, %o0		! IEU1
	add		%o1, 4, %o1		! IEU0	Group
	addcc		%g4, %g7, %g7		! IEU1	Group + 1 bubble
	stw		%g4, [%o1 - 0x4]	! Store
	bcc,pt		%xcc, cc_dword_aligned	! CTI
	 andn		%g1, 0xff, %g2		! IEU0	Group
	b,pt		%xcc, cc_dword_aligned	! CTI	4 clocks (mispredict)
	 add		%g7, 1, %g7		! IEU0

	.align		32
	.globl		__csum_partial_copy_sparc_generic, csum_partial_copy
csum_partial_copy:
__csum_partial_copy_sparc_generic:		/* %o0=src, %o1=dest, %g1=len, %g7=sum */
	xorcc		%o0, %o1, %o4		! IEU1	Group
	srl		%g7, 0, %g7		! IEU0
	andcc		%o4, 3, %g0		! IEU1	Group
	srl		%g1, 0, %g1		! IEU0
	bne,pn		%icc, ccslow		! CTI
	 andcc		%o0, 7, %g0		! IEU1	Group
	be,pt		%icc, cc_dword_aligned	! CTI
	 andn		%g1, 0xff, %g2		! IEU0
	b,pt		%xcc, cc_fixit		! CTI	Group
	 cmp		%g1, 6			! IEU1
cc_dword_aligned:
	brz,pn		%g2, 3f			! CTI	Group
	 andcc		%o1, 4, %g0		! IEU1	Group (brz uses IEU1)
	be,pn		%icc, ccdbl + 4		! CTI
5:	CSUMCOPY_ECACHE_LOAD(       %o0,    0x00,    %o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STUNALIGN_LDNXT(%o0,%o1,0x40,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STUNALIGN_LDNXT(%o0,%o1,0x80,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STUNALIGN_LDNXT(%o0,%o1,0xc0,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STUNALIGN(          %o1,0xc0,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
10:
	sub		%g1, 256, %g1		! IEU0	Group
	add		%o0, 256, %o0		! IEU1
	andncc		%g1, 0xff, %g0		! IEU1	Group
	bne,pt		%icc, 5b		! CTI
	 add		%o1, 256, %o1		! IEU0
3:	andcc		%g1, 0xf0, %o2		! IEU1	Group
ccmerge:be,pn		%icc, ccte		! CTI
	 andcc		%g1, 0xf, %o3		! IEU1	Group
	sll		%o2, 2, %o4		! IEU0
13:	rd		%pc, %o5		! LSU	Group + 4 clocks
	add		%o0, %o2, %o0		! IEU0	Group
	sub		%o5, %o4, %o5		! IEU1	Group
	jmpl		%o5 + (12f - 13b), %g0	! CTI	Group brk forced
	 add		%o1, %o2, %o1		! IEU0	Group
cctbl:	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0xe8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0xd8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0xc8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0xb8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0xa8,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x98,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x88,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x78,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x68,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x58,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x48,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x38,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x28,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x18,%g2,%g3)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x08,%g2,%g3)
12:
	andcc		%g1, 0xf, %o3		! IEU1	Group
ccte:	bne,pn		%icc, cc_end_cruft	! CTI
	 sethi		%uhi(PAGE_OFFSET), %g4	! IEU0
ccfold:	sllx		%g7, 32, %o0		! IEU0	Group
	addcc		%g7, %o0, %o0		! IEU1	Group (regdep)
	srlx		%o0, 32, %o0		! IEU0	Group (regdep)
	bcs,a,pn	%xcc, 1f		! CTI
	 add		%o0, 1, %o0		! IEU1	4 clocks (mispredict)
1:	retl					! CTI	Group brk forced
	 sllx		%g4, 32,%g4		! IEU0	Group
ccdbl:	CSUMCOPY_ECACHE_LOAD(       %o0,    0x00,    %o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STALIGNED_LDNXT(%o0,%o1,0x40,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STALIGNED_LDNXT(%o0,%o1,0x80,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STALIGNED_LDNXT(%o0,%o1,0xc0,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_EC_STALIGNED(          %o1,0xc0,%g7,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
11:
	sub		%g1, 256, %g1		! IEU0	Group
	add		%o0, 256, %o0		! IEU1
	andncc		%g1, 0xff, %g0		! IEU1	Group	
	bne,pt		%icc, ccdbl		! CTI
	 add		%o1, 256, %o1		! IEU0
	b,pt		%xcc, ccmerge		! CTI	Group
	 andcc		%g1, 0xf0, %o2		! IEU1

ccslow:	mov	0, %g5
	brlez,pn %g1, 4f
	 andcc	%o0, 1, %o5		
	be,a,pt	%icc, 1f
	 srl	%g1, 1, %o3		
	sub	%g1, 1, %g1	
	lduba [%o0] %asi, %g5
	add	%o0, 1, %o0	
	stb	%g5, [%o1]
	srl	%g1, 1, %o3
	add	%o1, 1, %o1
1:	brz,a,pn %o3, 3f
	 andcc	%g1, 1, %g0
	andcc	%o0, 2, %g0	
	be,a,pt	%icc, 1f
	 srl	%o3, 1, %o3
	lduha [%o0] %asi, %o4
	sub	%g1, 2, %g1	
	srl	%o4, 8, %g2
	sub	%o3, 1, %o3	
	stb	%g2, [%o1]
	add	%o4, %g5, %g5
	stb	%o4, [%o1 + 1]
	add	%o0, 2, %o0	
	srl	%o3, 1, %o3
	add	%o1, 2, %o1
1:	brz,a,pn %o3, 2f		
	 andcc	%g1, 2, %g0
	lda	[%o0] %asi, %o4
5:	srl	%o4, 24, %g2
	srl	%o4, 16, %g3
	stb	%g2, [%o1]
	srl	%o4, 8, %g2
	stb	%g3, [%o1 + 1]
	add	%o0, 4, %o0
	stb	%g2, [%o1 + 2]
	addcc	%o4, %g5, %g5
	stb	%o4, [%o1 + 3]
	addc	%g5, %g0, %g5	! I am now to lazy to optimize this (question is if it
	add	%o1, 4, %o1	! is worthy). Maybe some day - with the sll/srl
	subcc	%o3, 1, %o3	! tricks
	bne,a,pt %icc, 5b
	 lda [%o0] %asi, %o4
	sll	%g5, 16, %g2
	srl	%g5, 16, %g5
	srl	%g2, 16, %g2
	andcc	%g1, 2, %g0
	add	%g2, %g5, %g5 
2:	be,a,pt	%icc, 3f		
	 andcc	%g1, 1, %g0
	lduha [%o0] %asi, %o4
	andcc	%g1, 1, %g0
	srl	%o4, 8, %g2
	add	%o0, 2, %o0	
	stb	%g2, [%o1]
	add	%g5, %o4, %g5
	stb	%o4, [%o1 + 1]
	add	%o1, 2, %o1
3:	be,a,pt	%icc, 1f		
	 sll	%g5, 16, %o4
	lduba [%o0] %asi, %g2
	sll	%g2, 8, %o4	
	stb	%g2, [%o1]
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
4:	addcc	%g7, %g5, %g7
	addc	%g0, %g7, %o0
	retl	
	 srl	%o0, 0, %o0
__csum_partial_copy_end:
