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

#define CSUM_BIGCHUNK(buf, offset, sum, t0, t1, t2, t3, t4, t5)	\
	ldd	[buf + offset + 0x00], t0;			\
	ldd	[buf + offset + 0x08], t2;			\
	addccc	t0, sum, sum;					\
	addccc	t1, sum, sum;					\
	ldd	[buf + offset + 0x10], t4;			\
	addccc	t2, sum, sum;					\
	addccc	t3, sum, sum;					\
	ldd	[buf + offset + 0x18], t0;			\
	addccc	t4, sum, sum;					\
	addccc	t5, sum, sum;					\
	addccc	t0, sum, sum;					\
	addccc	t1, sum, sum;

#define CSUM_LASTCHUNK(buf, offset, sum, t0, t1, t2, t3)	\
	ldd	[buf - offset - 0x08], t0;			\
	ldd	[buf - offset - 0x00], t2;			\
	addccc	t0, sum, sum;					\
	addccc	t1, sum, sum;					\
	addccc	t2, sum, sum;					\
	addccc	t3, sum, sum;

	/* Do end cruft out of band to get better cache patterns. */
csum_partial_end_cruft:
	andcc	%o1, 8, %g0			! check how much
	be,pn	%icc, 1f			! caller asks %o1 & 0x8
	 and	%o1, 4, %g5			! nope, check for word remaining
	ldd	[%o0], %g2			! load two
	addcc	%g2, %o2, %o2			! add first word to sum
	addccc	%g3, %o2, %o2			! add second word as well
	add	%o0, 8, %o0			! advance buf ptr
	addc	%g0, %o2, %o2			! add in final carry
1:	brz,pn	%g5, 1f				! nope, skip this code
	 andcc	%o1, 3, %o1			! check for trailing bytes
	ld	[%o0], %g2			! load it
	addcc	%g2, %o2, %o2			! add to sum
	add	%o0, 4, %o0			! advance buf ptr
	addc	%g0, %o2, %o2			! add in final carry
1:	brz,pn	%o1, 1f				! no trailing bytes, return
	 addcc	%o1, -1, %g0			! only one byte remains?
	bne,pn	%icc, 2f			! at least two bytes more
	 subcc	%o1, 2, %o1			! only two bytes more?
	ba,pt	%xcc, 4f			! only one byte remains
	 clr	%o4				! clear fake hword value
2:	lduh	[%o0], %o4			! get hword
	be,pn	%icc, 6f			! jmp if only hword remains
	 add	%o0, 2, %o0			! advance buf ptr either way
	sll	%o4, 16, %o4			! create upper hword
4:	ldub	[%o0], %o5			! get final byte
	sll	%o5, 8, %o5			! put into place
	or	%o5, %o4, %o4			! coalese with hword (if any)
6:	addcc	%o4, %o2, %o2			! add to sum
1:	sllx	%g4, 32, %g4			! give gfp back
	addc	%g0, %o2, %o0			! add final carry into retval
	retl					! get outta here
	 srl	%o0, 0, %o0

	/* Also do alignment out of band to get better cache patterns. */
csum_partial_fix_alignment:

	/* The common case is to get called with a nicely aligned
	 * buffer of size 0x20.  Follow the code path for that case.
	 */
	.globl	csum_partial
csum_partial:						/* %o0=buf, %o1=len, %o2=sum */
	srl	%o1, 0, %o1				! doof scheiss
	andcc	%o0, 0x7, %g0				! alignment problems?
	srl	%o2, 0, %o2
	be,pt	%icc, csum_partial_fix_aligned		! yep, handle it
	 andn	%o1, 0x7f, %o3				! num loop iterations
	cmp	%o1, 6
	bl,pn	%icc, cpte - 0x4
	 andcc	%o0, 0x2, %g0
	be,pn	%icc, 1f
	 and	%o0, 0x4, %g7
	lduh	[%o0 + 0x00], %g2
	sub	%o1, 2, %o1
	add	%o0, 2, %o0
	sll	%g2, 16, %g2
	addcc	%g2, %o2, %o2
	srl	%o2, 16, %g3
	addc	%g0, %g3, %g2
	sll	%o2, 16, %o2
	and	%o0, 0x4, %g7
	sll	%g2, 16, %g3
	srl	%o2, 16, %o2
	or	%g3, %o2, %o2
1:	brz,pn	%g7, csum_partial_fix_aligned
	 andn	%o1, 0x7f, %o3
	ld	[%o0 + 0x00], %g2
	sub	%o1, 4, %o1
	addcc	%g2, %o2, %o2
	add	%o0, 4, %o0
	andn	%o1, 0x7f, %o3
	addc	%g0, %o2, %o2
csum_partial_fix_aligned:
	brz,pt	%o3, 3f					! none to do
	 andcc	%o1, 0x70, %g1				! clears carry flag too
5:	CSUM_BIGCHUNK(%o0, 0x00, %o2, %o4, %o5, %g2, %g3, %g4, %g5)
	CSUM_BIGCHUNK(%o0, 0x20, %o2, %o4, %o5, %g2, %g3, %g4, %g5)
	CSUM_BIGCHUNK(%o0, 0x40, %o2, %o4, %o5, %g2, %g3, %g4, %g5)
	CSUM_BIGCHUNK(%o0, 0x60, %o2, %o4, %o5, %g2, %g3, %g4, %g5)
	addc	%g0, %o2, %o2				! sink in final carry
	subcc	%o3, 128, %o3				! detract from loop iters
	bne,pt	%icc, 5b				! more to do
	 add	%o0, 128, %o0				! advance buf ptr
3:	brz,pn	%g1, cpte				! nope
	 andcc	%o1, 0xf, %o3				! anything left at all?
10:	rd	%pc, %g7				! get pc
	srl	%g1, 1, %o4				! compute offset
	sub	%g7, %g1, %g7				! adjust jmp ptr
	sub	%g7, %o4, %g7				! final jmp ptr adjust
	jmp	%g7 + (11f-10b)				! enter the table
	 add	%o0, %g1, %o0				! advance buf ptr
cptbl:	CSUM_LASTCHUNK(%o0, 0x68, %o2, %g2, %g3, %g4, %g5)
	CSUM_LASTCHUNK(%o0, 0x58, %o2, %g2, %g3, %g4, %g5)
	CSUM_LASTCHUNK(%o0, 0x48, %o2, %g2, %g3, %g4, %g5)
	CSUM_LASTCHUNK(%o0, 0x38, %o2, %g2, %g3, %g4, %g5)
	CSUM_LASTCHUNK(%o0, 0x28, %o2, %g2, %g3, %g4, %g5)
	CSUM_LASTCHUNK(%o0, 0x18, %o2, %g2, %g3, %g4, %g5)
	CSUM_LASTCHUNK(%o0, 0x08, %o2, %g2, %g3, %g4, %g5)
11:	addc	%g0, %o2, %o2				! fetch final carry
	andcc	%o1, 0xf, %o3				! anything left at all?
cpte:	brnz,pn	%o3, csum_partial_end_cruft		! yep, handle it
	 sethi	%uhi(KERNBASE), %g4
	mov	%o2, %o0				! return computed csum
	retl						! get outta here
	 sllx	%g4, 32, %g4				! give gfp back

	.globl __csum_partial_copy_start, __csum_partial_copy_end
__csum_partial_copy_start:

#define EX(x,y,a,b,z)                           \
98:     x,y;                                    \
        .section .fixup,z##alloc,z##execinstr;  \
        .align  4;                              \
99:     ba,pt %xcc, 30f;                        \
         a, b, %o3;                             \
        .section __ex_table,z##alloc;           \
        .align  8;                              \
        .xword   98b, 99b;                      \
        .text;                                  \
        .align  4

#define EX2(x,y,z)                          	\
98:     x,y;                                    \
        .section __ex_table,z##alloc;           \
        .align  8;                              \
        .xword   98b, 30f;                      \
        .text;                                  \
        .align  4

#define EX3(x,y,z)                          	\
98:     x,y;                                    \
        .section __ex_table,z##alloc;           \
        .align  8;                              \
        .xword   98b, 96f;                      \
        .text;                                  \
        .align  4

#define EXT(start,end,handler,z)                \
        .section __ex_table,z##alloc;           \
        .align  8;                              \
        .xword   start, 0, end, handler;        \
        .text;                                  \
        .align  4

	/* This aligned version executes typically in 8.5 superscalar cycles, this
	 * is the best I can do.  I say 8.5 because the final add will pair with
	 * the next ldd in the main unrolled loop.  Thus the pipe is always full.
	 * If you change these macros (including order of instructions),
	 * please check the fixup code below as well.
	 */
#define CSUMCOPY_BIGCHUNK_ALIGNED(src, dst, sum, off, t0, t1, t2, t3, t4, t5, t6, t7)	\
	ldda	[src + off + 0x00] %asi, t0;						\
	ldda	[src + off + 0x08] %asi, t2;						\
	addccc	t0, sum, sum;								\
	ldda	[src + off + 0x10] %asi, t4;						\
	addccc	t1, sum, sum;								\
	ldda	[src + off + 0x18] %asi, t6;						\
	addccc	t2, sum, sum;								\
	std	t0, [dst + off + 0x00];							\
	addccc	t3, sum, sum;								\
	std	t2, [dst + off + 0x08];							\
	addccc	t4, sum, sum;								\
	std	t4, [dst + off + 0x10];							\
	addccc	t5, sum, sum;								\
	std	t6, [dst + off + 0x18];							\
	addccc	t6, sum, sum;								\
	addccc	t7, sum, sum;

	/* 12 superscalar cycles seems to be the limit for this case,
	 * because of this we thus do all the ldd's together to get
	 * Viking MXCC into streaming mode.  Ho hum...
	 */
#define CSUMCOPY_BIGCHUNK(src, dst, sum, off, t0, t1, t2, t3, t4, t5, t6, t7)	\
	ldda	[src + off + 0x00] %asi, t0;					\
	ldda	[src + off + 0x08] %asi, t2;					\
	ldda	[src + off + 0x10] %asi, t4;					\
	ldda	[src + off + 0x18] %asi, t6;					\
	st	t0, [dst + off + 0x00];						\
	addccc	t0, sum, sum;							\
	st	t1, [dst + off + 0x04];						\
	addccc	t1, sum, sum;							\
	st	t2, [dst + off + 0x08];						\
	addccc	t2, sum, sum;							\
	st	t3, [dst + off + 0x0c];						\
	addccc	t3, sum, sum;							\
	st	t4, [dst + off + 0x10];						\
	addccc	t4, sum, sum;							\
	st	t5, [dst + off + 0x14];						\
	addccc	t5, sum, sum;							\
	st	t6, [dst + off + 0x18];						\
	addccc	t6, sum, sum;							\
	st	t7, [dst + off + 0x1c];						\
	addccc	t7, sum, sum;

	/* Yuck, 6 superscalar cycles... */
#define CSUMCOPY_LASTCHUNK(src, dst, sum, off, t0, t1, t2, t3)	\
	ldda	[src - off - 0x08] %asi, t0;			\
	ldda	[src - off - 0x00] %asi, t2;			\
	addccc	t0, sum, sum;					\
	st	t0, [dst - off - 0x08];				\
	addccc	t1, sum, sum;					\
	st	t1, [dst - off - 0x04];				\
	addccc	t2, sum, sum;					\
	st	t2, [dst - off - 0x00];				\
	addccc	t3, sum, sum;					\
	st	t3, [dst - off + 0x04];

	/* Handle the end cruft code out of band for better cache patterns. */
cc_end_cruft:
	andcc	%o3, 8, %g0		! begin checks for that code
	be,pn	%icc, 1f
	 and	%o3, 4, %g5
	EX(ldda	[%o0 + 0x00] %asi, %g2, and %o3, 0xf,#)
	add	%o1, 8, %o1
	addcc	%g2, %g7, %g7
	add	%o0, 8, %o0
	addccc	%g3, %g7, %g7
	EX2(st	%g2, [%o1 - 0x08],#)
	addc	%g0, %g7, %g7
	EX2(st	%g3, [%o1 - 0x04],#)
1:	brz,pt	%g5, 1f
	 andcc	%o3, 3, %o3
	EX(lda	[%o0 + 0x00] %asi, %g2, add %o3, 4,#)
	add	%o1, 4, %o1
	addcc	%g2, %g7, %g7
	EX2(st	%g2, [%o1 - 0x04],#)
	addc	%g0, %g7, %g7
	add	%o0, 4, %o0
1:	brz,pn	%o3, 1f
	 addcc	%o3, -1, %g0
	bne,pn	%icc, 2f
	 subcc	%o3, 2, %o3
	ba,pt	%xcc, 4f
	 clr	%o4
2:	EX(lduha [%o0 + 0x00] %asi, %o4, add %o3, 2,#)
	add	%o0, 2, %o0
	EX2(sth	%o4, [%o1 + 0x00],#)
	be,pn	%icc, 6f
	 add	%o1, 2, %o1
	sll	%o4, 16, %o4
4:	EX(lduba [%o0 + 0x00] %asi, %o5, add %g0, 1,#)
	EX2(stb	%o5, [%o1 + 0x00],#)
	sll	%o5, 8, %o5
	or	%o5, %o4, %o4
6:	addcc	%o4, %g7, %g7
1:	sllx	%g4, 32, %g4
	addc	%g0, %g7, %o0
	retl
	 srl	%o0, 0, %o0

	/* Sun, you just can't beat me, you just can't.  Stop trying,
	 * give up.  I'm serious, I am going to kick the living shit
	 * out of you, game over, lights out.
	 */
	.align	8
	.globl	__csum_partial_copy_sparc_generic
__csum_partial_copy_sparc_generic:
					/* %o0=src, %o1=dest, %g1=len, %g7=sum */
	srl	%g7, 0, %g7		! you neve know...
	xor	%o0, %o1, %o4		! get changing bits
	srl	%g1, 0, %g1		! doof scheiss
	andcc	%o4, 3, %g0		! check for mismatched alignment
	bne,pn	%icc, ccslow		! better this than unaligned/fixups
	 andcc	%o0, 7, %g0		! need to align things?
	be,pt	%icc, cc_dword_aligned	! yes, we check for short lengths there
	 andn	%g1, 0x7f, %g2		! can we use unrolled loop?
	cmp	%g1, 6
	bl,a,pn	%icc, ccte
	 andcc	%g1, 0xf, %o3
	andcc	%o0, 0x1, %g0
	bne,pn	%icc, ccslow
	 andcc	%o0, 0x2, %g0
	be,pn	%icc, 1f
	 andcc	%o0, 0x4, %g0
	EX(lduha [%o0 + 0x00] %asi, %g4, add %g1, 0,#)
	sub	%g1, 2, %g1
	EX2(sth	%g4, [%o1 + 0x00],#)
	add	%o0, 2, %o0
	sll	%g4, 16, %g4
	addcc	%g4, %g7, %g7
	add	%o1, 2, %o1
	srl	%g7, 16, %g3
	addc	%g0, %g3, %g4
	sll	%g7, 16, %g7
	sll	%g4, 16, %g3
	srl	%g7, 16, %g7
	andcc	%o0, 0x4, %g0
	or	%g3, %g7, %g7
1:	be,pt	%icc, 3f
	 andn	%g1, 0x7f, %g2
	EX(lda	[%o0 + 0x00] %asi, %g4, add %g1, 0,#)
	sub	%g1, 4, %g1
	EX2(st	%g4, [%o1 + 0x00],#)
	add	%o0, 4, %o0
	addcc	%g4, %g7, %g7
	add	%o1, 4, %o1
	andn	%g1, 0x7f, %g2
	addc	%g0, %g7, %g7
cc_dword_aligned:
3:	brz,pn	%g2, 3f			! nope, less than one loop remains
	 andcc	%o1, 4, %g0		! dest aligned on 4 or 8 byte boundry?
	be,pn	%icc, ccdbl + 4		! 8 byte aligned, kick ass
5:	CSUMCOPY_BIGCHUNK(%o0,%o1,%g7,0x00,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_BIGCHUNK(%o0,%o1,%g7,0x20,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_BIGCHUNK(%o0,%o1,%g7,0x40,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_BIGCHUNK(%o0,%o1,%g7,0x60,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
10:	EXT(5b, 10b, 20f,#)		! note for exception handling
	sub	%g1, 128, %g1		! detract from length
	addc	%g0, %g7, %g7		! add in last carry bit
	andncc	%g1, 0x7f, %g0		! more to csum?
	add	%o0, 128, %o0		! advance src ptr
	bne,pt	%icc, 5b		! we did not go negative, continue looping
	 add	%o1, 128, %o1		! advance dest ptr
3:	andcc	%g1, 0x70, %o2		! can use table?
ccmerge:be,pn	%icc, ccte		! nope, go and check for end cruft
	 andcc	%g1, 0xf, %o3		! get low bits of length (clears carry btw)
	srl	%o2, 1, %o4		! begin negative offset computation
13:	rd	%pc, %o5		! set up table ptr end
	add	%o0, %o2, %o0		! advance src ptr
	sub	%o5, %o4, %o5		! continue table calculation
	sll	%o2, 1, %g2		! constant multiplies are fun...
	sub	%o5, %g2, %o5		! some more adjustments
	jmpl	%o5 + (12f-13b), %g0	! jump into it, duff style, wheee...
	 add	%o1, %o2, %o1		! advance dest ptr (carry is clear btw)
cctbl:	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x68,%g2,%g3,%g4,%g5)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x58,%g2,%g3,%g4,%g5)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x48,%g2,%g3,%g4,%g5)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x38,%g2,%g3,%g4,%g5)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x28,%g2,%g3,%g4,%g5)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x18,%g2,%g3,%g4,%g5)
	CSUMCOPY_LASTCHUNK(%o0,%o1,%g7,0x08,%g2,%g3,%g4,%g5)
12:	EXT(cctbl, 12b, 22f,#)		! note for exception table handling
	addc	%g0, %g7, %g7
	andcc	%g1, 0xf, %o3		! check for low bits set
ccte:	bne,pn	%icc, cc_end_cruft	! something left, handle it out of band
	 sethi	%uhi(KERNBASE), %g4	! restore gfp
	mov	%g7, %o0		! give em the computed checksum
	sllx	%g4, 32, %g4		! finish gfp restoration
	retl				! return
	 srl	%o0, 0, %o0
ccdbl:	CSUMCOPY_BIGCHUNK_ALIGNED(%o0,%o1,%g7,0x00,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_BIGCHUNK_ALIGNED(%o0,%o1,%g7,0x20,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_BIGCHUNK_ALIGNED(%o0,%o1,%g7,0x40,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
	CSUMCOPY_BIGCHUNK_ALIGNED(%o0,%o1,%g7,0x60,%o4,%o5,%g2,%g3,%g4,%g5,%o2,%o3)
11:	EXT(ccdbl, 11b, 21f,#)		! note for exception table handling
	sub	%g1, 128, %g1		! detract from length
	addc	%g0, %g7, %g7		! add in last carry bit
	andncc	%g1, 0x7f, %g0		! more to csum?
	add	%o0, 128, %o0		! advance src ptr
	bne,pt	%icc, ccdbl		! we did not go negative, continue looping
	 add	%o1, 128, %o1		! advance dest ptr
	ba,pt	%xcc, ccmerge		! finish it off, above
	 andcc	%g1, 0x70, %o2		! can use table? (clears carry btw)

ccslow:	mov	0, %g5
	brlez,pn %g1, 4f
	 andcc	%o0, 1, %o5		
	be,a,pt	%icc, 1f
	 srl	%g1, 1, %o3		
	sub	%g1, 1, %g1	
	EX(lduba [%o0] %asi, %g5, add %g1, 1,#)
	add	%o0, 1, %o0	
	EX2(stb	%g5, [%o1],#)
	srl	%g1, 1, %o3
	add	%o1, 1, %o1
1:	brz,a,pn %o3, 3f
	 andcc	%g1, 1, %g0
	andcc	%o0, 2, %g0	
	be,a,pt	%icc, 1f
	 srl	%o3, 1, %o3
	EX(lduha [%o0] %asi, %o4, add %g1, 0,#)
	sub	%g1, 2, %g1	
	srl	%o4, 8, %g2
	sub	%o3, 1, %o3	
	EX2(stb	%g2, [%o1],#)
	add	%o4, %g5, %g5
	EX2(stb	%o4, [%o1 + 1],#)
	add	%o0, 2, %o0	
	srl	%o3, 1, %o3
	add	%o1, 2, %o1
1:	brz,a,pn %o3, 2f		
	 andcc	%g1, 2, %g0
	EX3(lda	[%o0] %asi, %o4,#)
5:	srl	%o4, 24, %g2
	srl	%o4, 16, %g3
	EX2(stb	%g2, [%o1],#)
	srl	%o4, 8, %g2
	EX2(stb	%g3, [%o1 + 1],#)
	add	%o0, 4, %o0
	EX2(stb	%g2, [%o1 + 2],#)
	addcc	%o4, %g5, %g5
	EX2(stb	%o4, [%o1 + 3],#)
	addc	%g5, %g0, %g5	! I am now to lazy to optimize this (question is if it
	add	%o1, 4, %o1	! is worthy). Maybe some day - with the sll/srl
	subcc	%o3, 1, %o3	! tricks
	bne,a,pt %icc, 5b
	 EX3(lda [%o0] %asi, %o4,#)
	sll	%g5, 16, %g2
	srl	%g5, 16, %g5
	srl	%g2, 16, %g2
	andcc	%g1, 2, %g0
	add	%g2, %g5, %g5 
2:	be,a,pt	%icc, 3f		
	 andcc	%g1, 1, %g0
	EX(lduha [%o0] %asi, %o4, and %g1, 3,#)
	andcc	%g1, 1, %g0
	srl	%o4, 8, %g2
	add	%o0, 2, %o0	
	EX2(stb	%g2, [%o1],#)
	add	%g5, %o4, %g5
	EX2(stb	%o4, [%o1 + 1],#)
	add	%o1, 2, %o1
3:	be,a,pt	%icc, 1f		
	 sll	%g5, 16, %o4
	EX(lduba [%o0] %asi, %g2, add %g0, 1,#)
	sll	%g2, 8, %o4	
	EX2(stb	%g2, [%o1],#)
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

        .section .fixup,#alloc,#execinstr
        .align  4
/* We do these strange calculations for the csum_*_from_user case only, ie.
 * we only bother with faults on loads... */

/* o2 = ((g2%20)&3)*8
 * o3 = g1 - (g2/20)*32 - o2 */
20:
	cmp	%g2, 20
	blu,a,pn %icc, 1f
	 and	%g2, 3, %o2
	sub	%g1, 32, %g1
	ba,pt	%xcc, 20b
	 sub	%g2, 20, %g2
1:
	sll	%o2, 3, %o2
	ba,pt	%xcc, 31f
	 sub	%g1, %o2, %o3

/* o2 = (!(g2 & 15) ? 0 : (((g2 & 15) + 1) & ~1)*8)
 * o3 = g1 - (g2/16)*32 - o2 */
21:
	andcc	%g2, 15, %o3
	srl	%g2, 4, %g2
	be,a,pn	%icc, 1f
	 clr	%o2
	add	%o3, 1, %o3
	and	%o3, 14, %o3
	sll	%o3, 3, %o2
1:
	sll	%g2, 5, %g2
	sub	%g1, %g2, %o3
	ba,pt	%xcc, 31f
	 sub	%o3, %o2, %o3

/* o0 += (g2/10)*16 - 0x70
 * 01 += (g2/10)*16 - 0x70
 * o2 = (g2 % 10) ? 8 : 0
 * o3 += 0x70 - (g2/10)*16 - o2 */
22:
	cmp	%g2, 10
	blu,a,pt %xcc, 1f
	 sub	%o0, 0x70, %o0
	add	%o0, 16, %o0
	add	%o1, 16, %o1
	sub	%o3, 16, %o3
	ba,pt	%xcc, 22b
	 sub	%g2, 10, %g2
1:
	sub	%o1, 0x70, %o1
	add	%o3, 0x70, %o3
	clr	%o2
	movrnz	%g2, 8, %o2
	ba,pt	%xcc, 31f
	 sub	%o3, %o2, %o3
96:
	and	%g1, 3, %g1
	sll	%o3, 2, %o3
	add	%g1, %o3, %o3
30:
/* %o1 is dst
 * %o3 is # bytes to zero out
 * %o4 is faulting address
 * %o5 is %pc where fault occured */
	clr	%o2
31:
/* %o0 is src
 * %o1 is dst
 * %o2 is # of bytes to copy from src to dst
 * %o3 is # bytes to zero out
 * %o4 is faulting address
 * %o5 is %pc where fault occured */
	save	%sp, -136, %sp
        mov     %i5, %o0
        mov     %i7, %o1
        mov	%i4, %o2
        call    lookup_fault
	 mov	%g7, %i4
	cmp	%o0, 2
	bne,pn	%icc, 1f
	 add	%g0, -EFAULT, %i5
	brz,pn	%i2, 2f
	 mov	%i0, %o1
	mov	%i1, %o0
	call	__copy_from_user
	 mov	%i2, %o2
	brnz,a,pn %o0, 2f
	 add	%i3, %i2, %i3
	add	%i1, %i2, %i1
2:
	mov	%i1, %o0
        wr	%g0, ASI_S, %asi
        call	__bzero_noasi
	 mov	%i3, %o1
1:
	ldx	[%sp + STACK_BIAS + 264], %o2		! struct_ptr of parent
	st	%i5, [%o2]
	ret
	 restore
