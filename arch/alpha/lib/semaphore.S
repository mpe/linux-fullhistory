/*
 *  linux/arch/alpha/lib/semaphore.S
 *
 *  Copyright (C) 1999  Richard Henderson
 */

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 */

	.set noat
	.set noreorder
	.align 4

/* __down_failed takes the semaphore in $24, clobbers $24 and $28.  */

	.globl	__down_failed
	.ent	__down_failed
__down_failed:
	ldgp	$29,0($27)
	lda	$30, -20*8($30)
	stq	$28, 0*8($30)
	stq	$0, 1*8($30)
	stq	$1, 2*8($30)
	stq	$2, 3*8($30)
	stq	$3, 4*8($30)
	stq	$4, 5*8($30)
	stq	$5, 6*8($30)
	stq	$6, 7*8($30)
	stq	$7, 8*8($30)
	stq	$16, 9*8($30)
	stq	$17, 10*8($30)
	stq	$18, 11*8($30)
	stq	$19, 12*8($30)
	stq	$20, 13*8($30)
	stq	$21, 14*8($30)
	stq	$22, 15*8($30)
	stq	$23, 16*8($30)
	stq	$25, 17*8($30)
	stq	$26, 18*8($30)
	.frame $30, 20*8, $28
	.prologue 1
	
	mov	$24, $16
	jsr	__down
	
	ldq	$28, 0*8($30)
	ldq	$0, 1*8($30)
	ldq	$1, 2*8($30)
	ldq	$2, 3*8($30)
	ldq	$3, 4*8($30)
	ldq	$4, 5*8($30)
	ldq	$5, 6*8($30)
	ldq	$6, 7*8($30)
	ldq	$7, 8*8($30)
	ldq	$16, 9*8($30)
	ldq	$17, 10*8($30)
	ldq	$18, 11*8($30)
	ldq	$19, 12*8($30)
	ldq	$20, 13*8($30)
	ldq	$21, 14*8($30)
	ldq	$22, 15*8($30)
	ldq	$23, 16*8($30)
	ldq	$25, 17*8($30)
	ldq	$26, 18*8($30)
	lda	$30, 20*8($30)
	ret	$31, ($28), 0
	.end	__down_failed

/* __down_failed_interruptible takes the semaphore in $24,
   clobbers $28, returns success in $24.  */

	.globl	__down_failed_interruptible
	.ent	__down_failed_interruptible
__down_failed_interruptible:
	ldgp	$29,0($27)
	lda	$30, -20*8($30)
	stq	$28, 0*8($30)
	stq	$0, 1*8($30)
	stq	$1, 2*8($30)
	stq	$2, 3*8($30)
	stq	$3, 4*8($30)
	stq	$4, 5*8($30)
	stq	$5, 6*8($30)
	stq	$6, 7*8($30)
	stq	$7, 8*8($30)
	stq	$16, 9*8($30)
	stq	$17, 10*8($30)
	stq	$18, 11*8($30)
	stq	$19, 12*8($30)
	stq	$20, 13*8($30)
	stq	$21, 14*8($30)
	stq	$22, 15*8($30)
	stq	$23, 16*8($30)
	stq	$25, 17*8($30)
	stq	$26, 18*8($30)
	.frame $30, 20*8, $28
	.prologue 1
	
	mov	$24, $16
	jsr	__down
	mov	$0, $24
	
	ldq	$28, 0*8($30)
	ldq	$0, 1*8($30)
	ldq	$1, 2*8($30)
	ldq	$2, 3*8($30)
	ldq	$3, 4*8($30)
	ldq	$4, 5*8($30)
	ldq	$5, 6*8($30)
	ldq	$6, 7*8($30)
	ldq	$7, 8*8($30)
	ldq	$16, 9*8($30)
	ldq	$17, 10*8($30)
	ldq	$18, 11*8($30)
	ldq	$19, 12*8($30)
	ldq	$20, 13*8($30)
	ldq	$21, 14*8($30)
	ldq	$22, 15*8($30)
	ldq	$23, 16*8($30)
	ldq	$25, 17*8($30)
	ldq	$26, 18*8($30)
	lda	$30, 20*8($30)
	ret	$31, ($28), 0
	.end	__down_failed_interruptible

/* __up_wakeup takes the semaphore in $24, clobbers $24 and $28.  */

	.globl	__up_wakeup
	.ent	__up_wakeup
__up_wakeup:
	ldgp	$29,0($27)
	lda	$30, -20*8($30)
	stq	$28, 0*8($30)
	stq	$0, 1*8($30)
	stq	$1, 2*8($30)
	stq	$2, 3*8($30)
	stq	$3, 4*8($30)
	stq	$4, 5*8($30)
	stq	$5, 6*8($30)
	stq	$6, 7*8($30)
	stq	$7, 8*8($30)
	stq	$16, 9*8($30)
	stq	$17, 10*8($30)
	stq	$18, 11*8($30)
	stq	$19, 12*8($30)
	stq	$20, 13*8($30)
	stq	$21, 14*8($30)
	stq	$22, 15*8($30)
	stq	$23, 16*8($30)
	stq	$25, 17*8($30)
	stq	$26, 18*8($30)
	.frame $30, 20*8, $28
	.prologue 1
	
	mov	$24, $16
	jsr	__up
	
	ldq	$28, 0*8($30)
	ldq	$0, 1*8($30)
	ldq	$1, 2*8($30)
	ldq	$2, 3*8($30)
	ldq	$3, 4*8($30)
	ldq	$4, 5*8($30)
	ldq	$5, 6*8($30)
	ldq	$6, 7*8($30)
	ldq	$7, 8*8($30)
	ldq	$16, 9*8($30)
	ldq	$17, 10*8($30)
	ldq	$18, 11*8($30)
	ldq	$19, 12*8($30)
	ldq	$20, 13*8($30)
	ldq	$21, 14*8($30)
	ldq	$22, 15*8($30)
	ldq	$23, 16*8($30)
	ldq	$25, 17*8($30)
	ldq	$26, 18*8($30)
	lda	$30, 20*8($30)
	ret	$31, ($28), 0
	.end	__up_wakeup
