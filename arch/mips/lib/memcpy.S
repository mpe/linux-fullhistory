/* memcpy.S: Mips optimized memcpy based upon SparcLinux code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *
 * derived from:
 *	e-mail between David and Eddie.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5) \
	lw	t0, (offset + 0x18)(src); \
	lw	t1, (offset + 0x1c)(src); \
	sw	t0, (offset + 0x18)(dst); \
	lw	t2, (offset + 0x10)(src); \
	sw	t1, (offset + 0x1c)(dst); \
	lw	t3, (offset + 0x14)(src); \
	sw	t2, (offset + 0x10)(dst); \
	lw	t4, (offset + 0x08)(src); \
	sw	t3, (offset + 0x14)(dst); \
	lw	t5, (offset + 0x0c)(src); \
	sw	t4, (offset + 0x08)(dst); \
	lw	t0, (offset + 0x00)(src); \
	sw	t5, (offset + 0x0c)(dst); \
	lw	t1, (offset + 0x04)(src); \
	sw	t0, (offset + 0x00)(dst); \
	sw	t1, (offset + 0x04)(dst); \

	/* Alignment cases are:
	 * 1) (src&0x3)=0x0 (dst&0x3)=0x0 	can optimize
	 * 2) (src&0x3)=0x1 (dst&0x3)=0x1	can optimize
	 * 3) (src&0x3)=0x2 (dst&0x3)=0x2	can optimize
	 * 4) (src&0x3)=0x3 (dst&0x3)=0x3	can optimize
	 * 5) anything else			cannot optimize
	 */

	/* I hate MIPS register names... AIEEE, it's a SPARC! */
#define o0 a0
#define o1 a1
#define o2 a2
#define o3 a3
#define o4 t0
#define o5 t1
#define o6 sp
#define o7 ra
#define g0 zero
#define g1 t2
#define g2 t3
#define g3 t4
#define g4 t5
#define g5 t6
#define g6 t7
#define g7 t8

	.text
	.set	noreorder
	.set	noat

	.globl	bcopy
	.globl	amemmove
	.globl	memmove
	.globl	memcpy
	.align	2
bcopy:
	move	o3, o0
	move	o0, o1
	move	o1, o3

amemmove:
memmove:
memcpy:			/* o0=dst o1=src o2=len */
	xor	o4, o0, o1
	andi	o4, o4, 0x3
	move	g6, o0
	beq	o4, g0, can_align
	 sltiu	g7, o2, 0x8

	b	cannot_optimize
	 move	g1, o2

can_align:
	bne	g7, g0, cannot_optimize
	 move	g1, o2

	beq	o2, g0, out
	 andi	g7, o1, 0x1

hword_align:
	beq	g7, g0, word_align
	 andi	g7, o1, 0x2

	lbu	o4, 0x00(o1)
	subu	o2, o2, 0x1
	sb	o4, 0x00(o0)
	addu	o1, o1, 0x1
	addu	o0, o0, 0x1
	andi	g7, o1, 0x2

word_align:
	beq	g7, g0, dword_align
	 sltiu	g7, o2, 56
	
	lhu	o4, 0x00(o1)
	subu	o2, o2, 0x2
	sh	o4, 0x00(o0)
	sltiu	g7, o2, 56
	addu	o0, o0, 0x2
	addu	o1, o1, 0x2

dword_align:
	bne	g7, g0, do_end_words
	 move	g7, o2

	andi	g7, o1, 0x4
	beq	g7, zero, qword_align
	 andi	g7, o1, 0x8

	lw	o4, 0x00(o1)
	subu	o2, o2, 0x4
	sw	o4, 0x00(o0)
	addu	o1, o1, 0x4
	addu	o0, o0, 0x4
	andi	g7, o1, 0x8

qword_align:
	beq	g7, g0, oword_align
	 andi	g7, o1, 0x10

	lw	o4, 0x00(o1)
	lw	o5, 0x04(o1)
	subu	o2, o2, 0x8
	sw	o4, 0x00(o0)
	addu	o1, o1, 0x8
	sw	o5, 0x04(o0)
	andi	g7, o1, 0x10
	addu	o0, o0, 0x8

oword_align:
	beq	g7, g0, begin_movement
	 srl	g7, o2, 0x7

	lw	g2, 0x08(o1)
	lw	g3, 0x0c(o1)
	lw	o4, 0x00(o1)
	lw	o5, 0x04(o1)
	sw	g2, 0x08(o0)
	subu	o2, o2, 0x10
	sw	g3, 0x0c(o0)
	addu	o1, o1, 0x10
	sw	o4, 0x00(o0)
	srl	g7, o2, 0x7
	addu	o0, o0, 0x10
	sw	o5, -0x0c(o0)

begin_movement:
	beq	g7, g0, 0f
	 andi	g1, o2, 0x40

move_128bytes:
	MOVE_BIGCHUNK(o1, o0, 0x00, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x40, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x60, o4, o5, g2, g3, g4, g5)
	subu	g7, g7, 0x01
	addu	o1, o1, 0x80
	bne	g7, g0, move_128bytes
	 addu	o0, o0, 0x80

0:
	beq	g1, g0, 1f
	 andi	g1, o2, 0x20

move_64bytes:
	MOVE_BIGCHUNK(o1, o0, 0x00, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o4, o5, g2, g3, g4, g5)
	addu	o1, o1, 0x40
	addu	o0, o0, 0x40

1:
	beq	g1, g0, do_end_words
	 andi	g7, o2, 0x1c

move_32bytes:
	MOVE_BIGCHUNK(o1, o0, 0x00, o4, o5, g2, g3, g4, g5)
	andi	g7, o2, 0x1c
	addu	o1, o1, 0x20
	addu	o0, o0, 0x20

do_end_words:
	beq	g7, g0, maybe_end_cruft
	 srl	g7, g7, 0x2

end_words:
	lw	o4, 0x00(o1)
	subu	g7, g7, 0x1
	sw	o4, 0x00(o0)
	addu	o1, o1, 0x4
	bne	g7, g0, end_words
	 addu	o0, o0, 0x4

maybe_end_cruft:
	andi	g1, o2, 0x3

cannot_optimize:
	beq	g1, g0, out
	 move	o2, g1

end_bytes:
	lbu	o4, 0x00(o1)
	subu	o2, o2, 0x1
	sb	o4, 0x00(o0)
	addu	o1, o1, 0x1
	bne	o2, g0, end_bytes
	 addu	o0, o0, 0x1

out:
	jr	o7
	 move	v0, g6
