/*
 * arch/alpha/lib/memmove.S
 *
 * Barely optimized memmove routine for Alpha EV5.
 *
 * This is hand-massaged output from the original memcpy.c.  We defer to
 * memcpy whenever possible; the backwards copy loops are not unrolled.
 */
        
	.set noat
	.set noreorder
	.text

	.align 4
	.globl memmove
	.ent memmove
memmove:
	addq $16,$18,$4
	addq $17,$18,$5
	cmpule $4,$17,$1		/*  dest + n <= src  */
	cmpule $5,$16,$2		/*  dest >= src + n  */

	bis $1,$2,$1
	mov $16,$0
	xor $16,$17,$2
	bne $1,memcpy

	and $2,7,$2			/* Test for src/dest co-alignment.  */
	bne $2,$misaligned

	and $4,7,$1
	beq $1,$skip_aligned_byte_loop_head

$aligned_byte_loop_head:
	lda $4,-1($4)
	lda $5,-1($5)
	unop
	ble $18,$egress

	ldq_u $3,0($5)
	ldq_u $2,0($4)
	lda $18,-1($18)
	extbl $3,$5,$1

	insbl $1,$4,$1
	mskbl $2,$4,$2
	bis $1,$2,$1
	and $4,7,$6

	stq_u $1,0($4)
	bne $6,$aligned_byte_loop_head

$skip_aligned_byte_loop_head:
	lda $18,-8($18)
	blt $18,$skip_aligned_word_loop

$aligned_word_loop:
	ldq $1,-8($5)
	nop
	lda $5,-8($5)
	lda $18,-8($18)

	stq $1,-8($4)
	nop
	lda $4,-8($4)
	bge $18,$aligned_word_loop

$skip_aligned_word_loop:
	lda $18,8($18)
	bgt $18,$byte_loop_tail
	unop
	ret $31,($26),1

	.align 4
$misaligned:
	nop
	fnop
	unop
	beq $18,$egress

$byte_loop_tail:
	ldq_u $3,-1($5)
	ldq_u $2,-1($4)
	lda $5,-1($5)
	lda $4,-1($4)

	lda $18,-1($18)
	extbl $3,$5,$1
	insbl $1,$4,$1
	mskbl $2,$4,$2

	bis $1,$2,$1
	stq_u $1,0($4)
	nop
	bgt $18,$byte_loop_tail

$egress:
	ret $31,($26),1
	nop
	nop
	nop

	.end memmove
