/*
 * arch/alpha/lib/put_user.S
 *
 * (C) Copyright 1996 Linus Torvalds
 */

/*
 * This does simple writes to user mode, returning zero for
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
 *	value to be written in $6
 *	user address in $7
 *	exception pointer in $8
 *	return address in $28 (exceptions expect it there)
 * Output:
 *	return value in $0
 * Clobbers:
 *	$1,$2,$3,$4,$5,$6
 */
	.set noat
	.align 3
	.globl __put_user_8
	.ent __put_user_8
__put_user_8:
	ldq $2,0($8)
	lda $0,-14
	addq $2,1,$1
	stq $1,0($8)
	ldq_u $1,0($7)
	insbl $6,$7,$6
	mskbl $1,$7,$1
	bis $6,$1,$6
	stq_u $6,0($7)
	stq $2,0($8)
	bis $31,$31,$0
	ret $31,($28),1
	.end __put_user_8

	.align 3
	.globl __put_user_16
	.ent __put_user_16
__put_user_16:
	ldq $2,0($8)
	lda $0,-14
	addq $2,1,$1
	stq $1,0($8)
	ldq_u $4,1($7)
	ldq_u $5,0($7)
	inswh $6,$7,$1
	inswl $6,$7,$3
	mskwh $4,$7,$4
	mskwl $5,$7,$5
	bis $4,$1,$4
	bis $5,$3,$5
	stq_u $4,1($7)
	stq_u $5,0($7)
	stq $2,0($8)
	bis $31,$31,$0
	ret $31,($28),1
	.end __put_user_16

	.align 3
	.globl __put_user_32
	.ent __put_user_32
__put_user_32:
	ldq $5,0($8)
	lda $0,-14
	and $7,3,$2
	addq $5,1,$1
	stq $1,0($8)
	bne $2,__una32
	stl $6,0($7)
	stq $5,0($8)
	bis $31,$31,$0
	ret $31,($28),1
	.align 4
__una32:
	ldq_u $3,3($7)
	ldq_u $4,0($7)
	insll $6,$7,$2
	inslh $6,$7,$1
	msklh $3,$7,$3
	mskll $4,$7,$4
	bis $3,$1,$3
	bis $4,$2,$4
	stq_u $3,3($7)
	stq_u $4,0($7)
	stq $5,0($8)
	bis $31,$31,$0
	ret $31,($28),1
	.end __put_user_32

	.align 3
	.globl __put_user_64
	.ent __put_user_64
__put_user_64:
	ldq $5,0($8)
	lda $0,-14
	and $7,7,$2
	addq $5,1,$1
	stq $1,0($8)
	bne $2,__una64
	stq $6,0($7)
	stq $5,0($8)
	bis $31,$31,$0
	ret $31,($28),1
	.align 4
__una64:
	ldq_u $4,0($7)
	ldq_u $3,8($7)
	insql $6,$7,$2
	insqh $6,$7,$1
	mskql $4,$7,$4
	mskqh $3,$7,$3
	bis $4,$2,$4
	bis $3,$1,$3
	stq_u $4,0($7)
	stq_u $3,8($7)
	stq $5,0($8)
	bis $31,$31,$0
	ret $31,($28),1
	.end __put_user_64
