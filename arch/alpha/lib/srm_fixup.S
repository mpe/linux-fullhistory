/*
 *	arch/alpha/lib/srm_fixup.S
 */

.globl	srm_fixup
.ent	srm_fixup
srm_fixup:
	.frame	$30,30,$26
	subq	$30,80,$30
	stq	$26,0($30)
	stq	$8,8($30)
	stq	$9,16($30)
	stq	$10,24($30)
	stq	$11,32($30)
	stq	$12,40($30)
	stq	$13,48($30)
	stq	$14,56($30)
	stq	$15,64($30)
	stq	$29,72($30)
	.mask	0x2400FF00, -80
	.prologue 0

	ldq	$2,0xc0($17)		/* crb offset */
	addq	$2,$1,$2		/* crb */
	ldq	$27,16($2)		/* fixup procedure value */

	ldq	$2,8($27)		/* dispatch call address */
	jsr	$26,($2)		/* call it (weird VMS call seq) */

	ldq	$26,0($30)
	ldq	$8,8($30)
	ldq	$9,16($30)
	ldq	$10,24($30)
	ldq	$11,32($30)
	ldq	$12,40($30)
	ldq	$13,48($30)
	ldq	$14,56($30)
	ldq	$15,64($30)
	ldq	$29,72($30)
	addq	$30,80,$30
	ret	$31,($26),1
.end    srm_fixup
