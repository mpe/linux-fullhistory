#include <linux/linkage.h>
ENTRY(memmove)
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r14,@-r15
	sts.l	pr,@-r15
	add	#-28,r15
	mov	r15,r14
	mov.l	r4,@r14
	mov.l	r5,@(4,r14)
	mov.l	r6,@(8,r14)
	mov.l	@r14,r1
	mov.l	r1,@(12,r14)
	mov.l	@(4,r14),r1
	mov.l	r1,@(16,r14)
	mov.l	@(12,r14),r1
	mov.l	@(16,r14),r2
	sub	r2,r1
	mov.l	@(8,r14),r2
	cmp/hs	r2,r1
	bt	.L54
	bra	.L2
	nop
.L54:
	mov.l	@(8,r14),r1
	mov	#15,r2
	cmp/gt	r2,r1
	bt	.LF100
	bra	.L52
	nop
.LF100:
	mov.l	@(12,r14),r2
	neg	r2,r1
	mov	#3,r2
	and	r1,r2
	mov.l	@(8,r14),r1
	mov	r1,r9
	sub	r2,r9
	mov	r9,r2
	mov.l	r2,@(8,r14)
.L4:
	mov.l	@(12,r14),r2
	neg	r2,r1
	mov	#3,r2
	and	r1,r2
	mov.l	r2,@(20,r14)
.L7:
	mov.l	@(20,r14),r1
	cmp/pl	r1
	bt	.L9
	bra	.L6
	nop
	.align 2
.L9:
	mov	r14,r2
	mov	r14,r1
	add	#24,r1
	mov.l	@(16,r14),r2
	mov.b	@r2,r3
	mov.b	r3,@r1
	mov.l	@(16,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(20,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(12,r14),r1
	mov	r14,r2
	mov	r14,r3
	add	#24,r3
	mov.b	@r3,r2
	mov.b	r2,@r1
	mov.l	@(12,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(12,r14)
	bra	.L7
	nop
	.align 2
.L8:
.L6:
	bra	.L5
	nop
	.align 2
.L10:
	bra	.L4
	nop
	.align 2
.L5:
	nop
.L11:
	mov.l	@(16,r14),r1
	mov	#3,r2
	and	r1,r2
	tst	r2,r2
	bf	.L14
	mov	r15,r2
	mov.l	@(12,r14),r1
	mov.l	@(16,r14),r2
	mov.l	@(8,r14),r7
	mov	r7,r3
	shlr2	r3
	mov	r1,r4
	mov	r2,r5
	mov	r3,r6
	mov.l	.L46,r8
	jsr	@r8
	nop
	bra	.L15
	nop
	.align 2
.L14:
	mov	r15,r2
	mov.l	@(12,r14),r1
	mov.l	@(16,r14),r2
	mov.l	@(8,r14),r7
	mov	r7,r3
	shlr2	r3
	mov	r1,r4
	mov	r2,r5
	mov	r3,r6
	mov.l	.L47,r8
	jsr	@r8
	nop
.L15:
	mov.l	@(8,r14),r1
	mov	#-4,r2
	and	r2,r1
	mov.l	@(16,r14),r2
	add	r2,r1
	mov.l	r1,@(16,r14)
	mov.l	@(8,r14),r1
	mov	#-4,r2
	and	r2,r1
	mov.l	@(12,r14),r2
	add	r2,r1
	mov.l	r1,@(12,r14)
	mov.l	@(8,r14),r1
	mov	#3,r2
	and	r1,r2
	mov.l	r2,@(8,r14)
.L13:
.L52:
	bra	.L3
	nop
	.align 2
.L16:
	bra	.L11
	nop
	.align 2
.L12:
.L3:
	nop
.L17:
	mov.l	@(8,r14),r1
	mov.l	r1,@(20,r14)
.L20:
	mov.l	@(20,r14),r1
	cmp/pl	r1
	bt	.L22
	bra	.L19
	nop
	.align 2
.L22:
	mov	r14,r2
	mov	r14,r1
	add	#24,r1
	mov.l	@(16,r14),r2
	mov.b	@r2,r3
	mov.b	r3,@r1
	mov.l	@(16,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(16,r14)
	mov.l	@(20,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(12,r14),r1
	mov	r14,r2
	mov	r14,r3
	add	#24,r3
	mov.b	@r3,r2
	mov.b	r2,@r1
	mov.l	@(12,r14),r1
	mov	r1,r2
	add	#1,r2
	mov.l	r2,@(12,r14)
	bra	.L20
	nop
	.align 2
.L21:
.L19:
	bra	.L18
	nop
	.align 2
.L23:
	bra	.L17
	nop
	.align 2
.L18:
	bra	.L24
	nop
	.align 2
.L2:
	mov.l	@(16,r14),r1
	mov.l	@(8,r14),r2
	add	r2,r1
	mov.l	r1,@(16,r14)
	mov.l	@(12,r14),r1
	mov.l	@(8,r14),r2
	add	r2,r1
	mov.l	r1,@(12,r14)
	mov.l	@(8,r14),r1
	mov	#15,r2
	cmp/gt	r2,r1
	bt	.LF101
	bra	.L53
	nop
.LF101:
	mov.l	@(12,r14),r1
	mov	#3,r2
	and	r1,r2
	mov.l	@(8,r14),r1
	mov	r1,r9
	sub	r2,r9
	mov	r9,r2
	mov.l	r2,@(8,r14)
.L26:
	mov.l	@(12,r14),r1
	mov	#3,r2
	and	r1,r2
	mov.l	r2,@(20,r14)
.L29:
	mov.l	@(20,r14),r1
	cmp/pl	r1
	bt	.L31
	bra	.L28
	nop
	.align 2
.L31:
	mov.l	@(16,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(16,r14)
	mov	r14,r2
	mov	r14,r1
	add	#24,r1
	mov.l	@(16,r14),r2
	mov.b	@r2,r3
	mov.b	r3,@r1
	mov.l	@(12,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(20,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(12,r14),r1
	mov	r14,r2
	mov	r14,r3
	add	#24,r3
	mov.b	@r3,r2
	mov.b	r2,@r1
	bra	.L29
	nop
	.align 2
.L30:
.L28:
	bra	.L27
	nop
	.align 2
.L32:
	bra	.L26
	nop
	.align 2
.L27:
	nop
.L33:
	mov.l	@(16,r14),r1
	mov	#3,r2
	and	r1,r2
	tst	r2,r2
	bf	.L36
	mov	r15,r2
	mov.l	@(12,r14),r1
	mov.l	@(16,r14),r2
	mov.l	@(8,r14),r7
	mov	r7,r3
	shlr2	r3
	mov	r1,r4
	mov	r2,r5
	mov	r3,r6
	mov.l	.L48,r8
	jsr	@r8
	nop
	bra	.L37
	nop
	.align 2
.L36:
	mov	r15,r2
	mov.l	@(12,r14),r1
	mov.l	@(16,r14),r2
	mov.l	@(8,r14),r7
	mov	r7,r3
	shlr2	r3
	mov	r1,r4
	mov	r2,r5
	mov	r3,r6
	mov.l	.L49,r8
	jsr	@r8
	nop
.L37:
	mov.l	@(8,r14),r1
	mov	#-4,r2
	and	r2,r1
	mov.l	@(16,r14),r2
	mov	r2,r9
	sub	r1,r9
	mov	r9,r1
	mov.l	r1,@(16,r14)
	mov.l	@(8,r14),r1
	mov	#-4,r2
	and	r2,r1
	mov.l	@(12,r14),r2
	mov	r2,r9
	sub	r1,r9
	mov	r9,r1
	mov.l	r1,@(12,r14)
	mov.l	@(8,r14),r1
	mov	#3,r2
	and	r1,r2
	mov.l	r2,@(8,r14)
.L35:
.L53:
	bra	.L25
	nop
	.align 2
.L38:
	bra	.L33
	nop
	.align 2
.L34:
.L25:
	nop
.L39:
	mov.l	@(8,r14),r1
	mov.l	r1,@(20,r14)
.L42:
	mov.l	@(20,r14),r1
	cmp/pl	r1
	bt	.L44
	bra	.L41
	nop
	.align 2
.L44:
	mov.l	@(16,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(16,r14)
	mov	r14,r2
	mov	r14,r1
	add	#24,r1
	mov.l	@(16,r14),r2
	mov.b	@r2,r3
	mov.b	r3,@r1
	mov.l	@(12,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(12,r14)
	mov.l	@(20,r14),r1
	mov	r1,r2
	add	#-1,r2
	mov.l	r2,@(20,r14)
	mov.l	@(12,r14),r1
	mov	r14,r2
	mov	r14,r3
	add	#24,r3
	mov.b	@r3,r2
	mov.b	r2,@r1
	bra	.L42
	nop
	.align 2
.L43:
.L41:
	bra	.L24
	nop
	.align 2
.L45:
	bra	.L39
	nop
	.align 2
.L40:
.L24:
	mov.l	@r14,r1
	mov	r1,r0
	bra	.L1
	nop
	.align 2
.L1:
	add	#28,r14
	mov	r14,r15
	lds.l	@r15+,pr
	mov.l	@r15+,r14
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	rts	
	 nop
.L50:
	.align 2
.L46:
	.long	__wordcopy_fwd_aligned
.L47:
	.long	__wordcopy_fwd_dest_aligned
.L48:
	.long	__wordcopy_bwd_aligned
.L49:
	.long	__wordcopy_bwd_dest_aligned
.Lfe1:
