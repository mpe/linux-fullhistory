/*
 * arch/alpha/lib/strncpy_from_user.S
 * Contributed by Richard Henderson (rth@tamu.edu)
 *
 * Just like strncpy except in the return value:
 *
 * -EFAULT       if an exception occurs before the terminator is copied.
 * N             if the buffer filled.
 * 
 * Otherwise the length of the string is returned.
 *
 * Additionally, the fourth argument should be `&current->tss.ex'.
 */

#include <asm/errno.h>

	.set noat
	.set noreorder

	.text

	.align 3
	.globl __strncpy_from_user
	.ent __strncpy_from_user

__strncpy_from_user:
	.frame $30, 0, $26
	.prologue 0

	ldq	$20, 0($19)
	beq	$18, 9f
	br	$28, 1f		# set up exception return address

	lda	$0, -EFAULT
	ret

1:	addq	$20, 1, $21
	mov	$16, $0		# save the string start
	stq	$21, 0($19)	# increment exception count
	bsr	$23, __stxncpy	# do the work of the copy

	zapnot	$1, $22, $5	# was last byte written null?
	stq	$20, 0($19)	# decrement exception count
	cmovne	$5, 1, $5

	and	$22, 0xf0, $4	# binary search for the address of the
	and	$22, 0xcc, $3	# last byte written
	and	$22, 0xaa, $2
	bic	$16, 7, $1
	cmovne	$4, 4, $4
	cmovne	$3, 2, $3
	cmovne	$2, 1, $2
	addq	$1, $4, $1
	addq	$2, $3, $2
	addq	$1, $2, $1
	addq	$1, $5, $1	# add one if we filled the buffer

	subq	$1, $0, $0	# find string length
	ret

9:	clr	$0
	ret

	.end __strncpy_from_user
