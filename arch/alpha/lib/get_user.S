/*
 * arch/alpha/lib/get_user.S
 *
 * (C) Copyright 1996 Linus Torvalds
 */

/*
 * This does simple reads from user mode, returning zero for
 * success and -EINVAL for a fault. Note that we may NOT do
 * unaligned accesses, because the unaligned fault handler
 * must not take the exception..
 *
 * NOTE! These are NOT normal function calls callable from C.
 * As we have two return values (the actual value gotten from
 * user space, and the error return value) the calling sequence
 * is different.
 *
 * Input:
 *	user address in $2
 *	exception structure address in $3
 *	return address in $28 (exceptions expect it there)
 *
 * Output:
 *	error number in $0
 *	actual result in $1
 *
 * Clobbers:
 *	$4,$5
 */
	.set noat
	.align 3
	.globl __get_user_8
	.ent __get_user_8
__get_user_8:
	ldq $4,0($3)
	lda $0,-14
	addq $4,1,$5
	stq $5,0($3)
	ldq_u $1,0($2)
	stq $4,0($3)
	bis $31,$31,$0
	extbl $1,$2,$1
	ret $31,($28),1
	.end __get_user_8

	.align 3
	.globl __get_user_16
	.ent __get_user_16
__get_user_16:
	ldq $4,0($3)
	lda $0,-14
	addq $4,1,$5
	stq $5,0($3)
	ldq_u $1,0($2)
	ldq_u $5,1($2)
	stq $4,0($3)
	extwl $1,$2,$1
	bis $31,$31,$0
	extwh $5,$2,$5
	bis $1,$5,$1
	ret $31,($28),1
	.end __get_user_16

	.align 3
	.globl __get_user_32
	.ent __get_user_32
__get_user_32:
	ldq $4,0($3)
	lda $0,-14
	addq $4,1,$5
	stq $5,0($3)
	ldq_u $1,0($2)
	ldq_u $5,3($2)
	stq $4,0($3)
	extll $1,$2,$1
	bis $31,$31,$0
	extlh $5,$2,$5
	bis $1,$5,$1
	ret $31,($28),1
	.end __get_user_32

	.align 3
	.globl __get_user_64
	.ent __get_user_64
__get_user_64:
	ldq $4,0($3)
	lda $0,-14
	addq $4,1,$5
	stq $5,0($3)
	ldq_u $1,0($2)
	ldq_u $5,7($2)
	stq $4,0($3)
	extql $1,$2,$1
	bis $31,$31,$0
	extqh $5,$2,$5
	bis $1,$5,$1
	ret $31,($28),1
	.end __get_user_64
