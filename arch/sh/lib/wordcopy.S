#include <linux/linkage.h>
ENTRY(_wordcopy_fwd_aligned)
	mov.l	r14,@-r15
	add	#-20,r15
	mov	r15,r14
	mov.l	r4,@r14
	mov.l	r5,@(4,r14)
	mov.l	r6,@(8,r14)
	mov.l	@(8,r14),r2
	mov	#7,r1
	and	r2,r1
	mov	#0,r2
	mov	#7,r3
	sub	r2,r1
	cmp/hi	r3,r1
	bf	.L29
	bra	.L2
	nop
.L29:
	mova	.L22,r0
	add	r1,r1
	mov.w	@(r0,r1),r1
	add	r0,r1
	jmp  @r1
	nop
	.align 2
	.align 2
.L22:
	.word	.L15-.L22
	.word	.L18-.L22
	.word	.L3-.L22
	.word	.L5-.L22
	.word	.L7-.L22
	.word	.L9-.L22
	.word	.L11-.L22
	.word	.L13-.L22
	.align 2
.L3:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-24,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-28,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#6,r2
	mov.l	r2,@(8,r14)
	bra	.L4
	nop
	.align 2
.L5:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-20,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-24,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#5,r2
	mov.l	r2,@(8,r14)
	bra	.L6
	nop
	.align 2
.L7:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-20,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#4,r2
	mov.l	r2,@(8,r14)
	bra	.L8
	nop
	.align 2
.L9:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#3,r2
	mov.l	r2,@(8,r14)
	bra	.L10
	nop
	.align 2
.L11:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#2,r2
	mov.l	r2,@(8,r14)
	bra	.L12
	nop
	.align 2
.L13:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(8,r14)
	bra	.L14
	nop
	.align 2
.L15:
	bra	.L16
	nop
	bra	.L1
	nop
	.align 2
.L16:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@r14
	bra	.L17
	nop
	.align 2
.L18:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#4,r2
	mov.l	r2,@(4,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(8,r14)
	bra	.L19
	nop
	bra	.L20
	nop
	.align 2
.L19:
	bra	.L21
	nop
	.align 2
.L23:
.L2:
	nop
.L24:
.L21:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L17:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#4,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
.L14:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#8,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L12:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#12,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
.L10:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#16,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#16,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L8:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#20,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#20,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
.L6:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#24,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#24,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L4:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#28,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#28,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#32,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#32,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@(8,r14)
.L26:
	mov.l	@(8,r14),r1
	tst	r1,r1
	bf	.L27
	bra	.L25
	nop
	.align 2
.L27:
	bra	.L21
	nop
	.align 2
.L25:
	nop
.L20:
	mov.l	@r14,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L1:
	add	#20,r14
	mov	r14,r15
	mov.l	@r15+,r14
	rts	
	nop
.Lfe1:
	.size	 __wordcopy_fwd_aligned,.Lfe1-__wordcopy_fwd_aligned
	.global	___lshrsi3
	.global	___ashlsi3
	.align 2
	.global	__wordcopy_fwd_dest_aligned
	.type	 __wordcopy_fwd_dest_aligned,@function
__wordcopy_fwd_dest_aligned:
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r14,@-r15
	sts.l	pr,@-r15
	add	#-40,r15
	mov	r15,r14
	mov.l	r4,@r14
	mov.l	r5,@(4,r14)
	mov.l	r6,@(8,r14)
	mov.l	@(4,r14),r1
	mov	#3,r2
	and	r1,r2
	mov	r2,r1
	mov	r1,r2
	shll2	r2
	add	r2,r2
	mov.l	r2,@(28,r14)
	mov.l	@(28,r14),r2
	neg	r2,r1
	add	#32,r1
	mov.l	r1,@(32,r14)
	mov.l	@(4,r14),r1
	mov	#-4,r2
	and	r2,r1
	mov.l	r1,@(4,r14)
	mov.l	@(8,r14),r2
	mov	#3,r1
	and	r2,r1
	mov	#0,r2
	mov	#3,r3
	sub	r2,r1
	cmp/hi	r3,r1
	bf	.L53
	bra	.L31
	nop
.L53:
	mova	.L43,r0
	add	r1,r1
	mov.w	@(r0,r1),r1
	add	r0,r1
	jmp  @r1
	nop
	.align 2
	.align 2
.L43:
	.word	.L36-.L43
	.word	.L39-.L43
	.word	.L32-.L43
	.word	.L34-.L43
	.align 2
.L32:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#2,r2
	mov.l	r2,@(8,r14)
	bra	.L33
	nop
	.align 2
.L34:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(8,r14)
	bra	.L35
	nop
	.align 2
.L36:
	bra	.L37
	nop
	bra	.L30
	nop
	.align 2
.L37:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(24,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#4,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@r14
	bra	.L38
	nop
	.align 2
.L39:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(24,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#8,r2
	mov.l	r2,@(4,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(8,r14)
	bra	.L40
	nop
	bra	.L41
	nop
	.align 2
.L40:
	bra	.L42
	nop
	.align 2
.L44:
.L31:
	nop
.L45:
.L42:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r8
	mov.l	.L49,r1
	mov.l	@(20,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L50,r1
	mov.l	@(24,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L38:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r1
	mov	r1,r8
	add	#4,r8
	mov.l	.L49,r1
	mov.l	@(24,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L50,r1
	mov.l	@(12,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L35:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(20,r14)
	mov.l	@r14,r1
	mov	r1,r8
	add	#8,r8
	mov.l	.L49,r1
	mov.l	@(12,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L50,r1
	mov.l	@(16,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L33:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(24,r14)
	mov.l	@r14,r1
	mov	r1,r8
	add	#12,r8
	mov.l	.L49,r1
	mov.l	@(16,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L50,r1
	mov.l	@(20,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#16,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#16,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@(8,r14)
.L47:
	mov.l	@(8,r14),r1
	tst	r1,r1
	bf	.L48
	bra	.L46
	nop
	.align 2
.L48:
	bra	.L42
	nop
	.align 2
.L46:
	nop
.L41:
	mov.l	@r14,r8
	mov.l	.L49,r1
	mov.l	@(20,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L50,r1
	mov.l	@(24,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L30:
	add	#40,r14
	mov	r14,r15
	lds.l	@r15+,pr
	mov.l	@r15+,r14
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	rts	
	nop
.L51:
	.align 2
.L49:
	.long	___lshrsi3
.L50:
	.long	___ashlsi3
.Lfe2:
	.size	 __wordcopy_fwd_dest_aligned,.Lfe2-__wordcopy_fwd_dest_aligned
	.align 2
	.global	__wordcopy_bwd_aligned
	.type	 __wordcopy_bwd_aligned,@function
__wordcopy_bwd_aligned:
	mov.l	r14,@-r15
	add	#-20,r15
	mov	r15,r14
	mov.l	r4,@r14
	mov.l	r5,@(4,r14)
	mov.l	r6,@(8,r14)
	mov.l	@(8,r14),r2
	mov	#7,r1
	and	r2,r1
	mov	#0,r2
	mov	#7,r3
	sub	r2,r1
	cmp/hi	r3,r1
	bf	.L82
	bra	.L55
	nop
.L82:
	mova	.L75,r0
	add	r1,r1
	mov.w	@(r0,r1),r1
	add	r0,r1
	jmp  @r1
	nop
	.align 2
	.align 2
.L75:
	.word	.L68-.L75
	.word	.L71-.L75
	.word	.L56-.L75
	.word	.L58-.L75
	.word	.L60-.L75
	.word	.L62-.L75
	.word	.L64-.L75
	.word	.L66-.L75
	.align 2
.L56:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#6,r2
	mov.l	r2,@(8,r14)
	bra	.L57
	nop
	.align 2
.L58:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#5,r2
	mov.l	r2,@(8,r14)
	bra	.L59
	nop
	.align 2
.L60:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#4,r2
	mov.l	r2,@(8,r14)
	bra	.L61
	nop
	.align 2
.L62:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-20,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#16,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#3,r2
	mov.l	r2,@(8,r14)
	bra	.L63
	nop
	.align 2
.L64:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-24,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-20,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#20,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#2,r2
	mov.l	r2,@(8,r14)
	bra	.L65
	nop
	.align 2
.L66:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-28,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-24,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#24,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(8,r14)
	bra	.L67
	nop
	.align 2
.L68:
	bra	.L69
	nop
	bra	.L54
	nop
	.align 2
.L69:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-32,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-28,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#28,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	bra	.L70
	nop
	.align 2
.L71:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-36,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-32,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#32,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(8,r14)
	bra	.L72
	nop
	bra	.L73
	nop
	.align 2
.L72:
	bra	.L74
	nop
	.align 2
.L76:
.L55:
	nop
.L77:
.L74:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#28,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#28,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L70:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#24,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#24,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
.L67:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#20,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#20,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L65:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#16,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#16,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
.L63:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#12,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L61:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#8,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
.L59:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r2
	mov	r2,r1
	add	#4,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L57:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r1
	mov.l	@(12,r14),r2
	mov.l	r2,@r1
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-32,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-32,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@(8,r14)
.L79:
	mov.l	@(8,r14),r1
	tst	r1,r1
	bf	.L80
	bra	.L78
	nop
	.align 2
.L80:
	bra	.L74
	nop
	.align 2
.L78:
	nop
.L73:
	mov.l	@r14,r2
	mov	r2,r1
	add	#28,r1
	mov.l	@(16,r14),r2
	mov.l	r2,@r1
.L54:
	add	#20,r14
	mov	r14,r15
	mov.l	@r15+,r14
	rts	
	nop
.Lfe3:
	.size	 __wordcopy_bwd_aligned,.Lfe3-__wordcopy_bwd_aligned
	.align 2
	.global	__wordcopy_bwd_dest_aligned
	.type	 __wordcopy_bwd_dest_aligned,@function
__wordcopy_bwd_dest_aligned:
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r14,@-r15
	sts.l	pr,@-r15
	add	#-40,r15
	mov	r15,r14
	mov.l	r4,@r14
	mov.l	r5,@(4,r14)
	mov.l	r6,@(8,r14)
	mov.l	@(4,r14),r1
	mov	#3,r2
	and	r1,r2
	mov	r2,r1
	mov	r1,r2
	shll2	r2
	add	r2,r2
	mov.l	r2,@(28,r14)
	mov.l	@(28,r14),r2
	neg	r2,r1
	add	#32,r1
	mov.l	r1,@(32,r14)
	mov.l	@(4,r14),r1
	mov	#-4,r2
	and	r2,r1
	mov.l	r1,@(4,r14)
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#4,r2
	mov.l	r2,@(4,r14)
	mov.l	@(8,r14),r2
	mov	#3,r1
	and	r2,r1
	mov	#0,r2
	mov	#3,r3
	sub	r2,r1
	cmp/hi	r3,r1
	bf	.L106
	bra	.L84
	nop
.L106:
	mova	.L96,r0
	add	r1,r1
	mov.w	@(r0,r1),r1
	add	r0,r1
	jmp  @r1
	nop
	.align 2
	.align 2
.L96:
	.word	.L89-.L96
	.word	.L92-.L96
	.word	.L85-.L96
	.word	.L87-.L96
	.align 2
.L85:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#2,r2
	mov.l	r2,@(8,r14)
	bra	.L86
	nop
	.align 2
.L87:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-8,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(24,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(8,r14)
	bra	.L88
	nop
	.align 2
.L89:
	bra	.L90
	nop
	bra	.L83
	nop
	.align 2
.L90:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-20,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-12,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#16,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(24,r14)
	bra	.L91
	nop
	.align 2
.L92:
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-24,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@r14
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#20,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#16,r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(8,r14)
	bra	.L93
	nop
	bra	.L94
	nop
	.align 2
.L93:
	bra	.L95
	nop
	.align 2
.L97:
.L84:
	nop
.L98:
.L95:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#12,r1
	mov.l	@r1,r2
	mov.l	r2,@(24,r14)
	mov.l	@r14,r1
	mov	r1,r8
	add	#12,r8
	mov.l	.L102,r1
	mov.l	@(12,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L103,r1
	mov.l	@(16,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L91:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#8,r1
	mov.l	@r1,r2
	mov.l	r2,@(20,r14)
	mov.l	@r14,r1
	mov	r1,r8
	add	#8,r8
	mov.l	.L102,r1
	mov.l	@(24,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L103,r1
	mov.l	@(12,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L88:
	mov.l	@(4,r14),r2
	mov	r2,r1
	add	#4,r1
	mov.l	@r1,r2
	mov.l	r2,@(16,r14)
	mov.l	@r14,r1
	mov	r1,r8
	add	#4,r8
	mov.l	.L102,r1
	mov.l	@(20,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L103,r1
	mov.l	@(24,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L86:
	mov.l	@(4,r14),r1
	mov.l	@r1,r2
	mov.l	r2,@(12,r14)
	mov.l	@r14,r8
	mov.l	.L102,r1
	mov.l	@(16,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L103,r1
	mov.l	@(20,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
	mov.l	@(4,r14),r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@(4,r14)
	mov.l	@r14,r1
	mov	r1,r2
	add	#-16,r2
	mov.l	r2,@r14
	mov.l	@(8,r14),r1
	mov	r1,r2
	add	#-4,r2
	mov.l	r2,@(8,r14)
.L100:
	mov.l	@(8,r14),r1
	tst	r1,r1
	bf	.L101
	bra	.L99
	nop
	.align 2
.L101:
	bra	.L95
	nop
	.align 2
.L99:
	nop
.L94:
	mov.l	@r14,r1
	mov	r1,r8
	add	#12,r8
	mov.l	.L102,r1
	mov.l	@(12,r14),r4
	mov.l	@(28,r14),r5
	jsr	@r1
	nop
	mov	r0,r9
	mov.l	.L103,r1
	mov.l	@(16,r14),r4
	mov.l	@(32,r14),r5
	jsr	@r1
	nop
	mov.l	r0,@(36,r14)
	mov.l	@(36,r14),r1
	or	r9,r1
	mov.l	r1,@r8
.L83:
	add	#40,r14
	mov	r14,r15
	lds.l	@r15+,pr
	mov.l	@r15+,r14
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	rts	
	nop
.L104:
	.align 2
.L102:
	.long	___lshrsi3
.L103:
	.long	___ashlsi3
.Lfe4:
