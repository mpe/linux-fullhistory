/*
 * arch/alpha/lib/divide.S
 *
 * (C) 1995 Linus Torvalds
 *
 * Alpha division..
 */

/*
 * The alpha chip doesn't provide hardware division, so we have to do it
 * by hand.  The compiler expects the functions
 *
 *	__divqu: 64-bit unsigned long divide
 *	__remqu: 64-bit unsigned long reminder
 *	__divqs/__remqs: signed 64-bit
 *	__divlu/__remlu: unsigned 32-bit
 *	__divls/__remls: signed 32-bit
 *
 * These are not normal C functions: instead of the normal
 * calling sequence, these expect their arguments in registers
 * $24 and $25, and return the result in $27. Register $28 may
 * be clobbered (assembly temprary), anything else must be saved. 
 *
 * In short: painful. I'm not going to try to be clever about it:
 * let somebody else optimize it if they will.
 */

/*
 * My temporaries:
 *	$0 - current bit
 *	$1 - shifted divisor
 *	$2 - rest
 *	$3 - compare status
 *
 *	$23 - return address
 *	$24 - dividend
 *	$25 - divisor
 */

/*
 * Select function type and registers
 */
#ifdef DIV
#define func(x) __div##x
#define modulus $2
#define quotient $27
#else
#define func(x) __rem##x
#define modulus $27
#define quotient $2
#endif

/*
 * For 32-bit operations, we need to extend to 64-bit
 */
#ifdef INTSIZE
#define function func(lu)
#define LONGIFY(x) zapnot x,15,x
#else
#define function func(qu)
#define LONGIFY(x)
#endif

.globl	function
.ent	function
function:
	lda	$30,-32($30)
	stq	$0, 0($30)
	stq	$1, 8($30)
	stq	$2,16($30)
	stq	$3,24($30)
	bis	$25,$25,$1
	bis	$24,$24,modulus
	bis	$31,$31,quotient
	LONGIFY($1)
	LONGIFY(modulus)
	beq	$1, 9f			/* div by zero */
	bis	$31,1,$0
	blt	$1, 2f			/* high bit set */
1:	cmpult	modulus,$1,$3
	bne	$3,3f
	addq	$1,$1,$1
	addq	$0,$0,$0
	bge	$1,1b
2:	cmpult	modulus,$1,$3
	bne	$3,3f
	subq	modulus,$1,modulus
	addq	$0,quotient,quotient
3:	srl	$0,1,$0
	srl	$1,1,$1
	bne	$0,2b
9:	ldq	$0, 0($30)
	ldq	$1, 8($30)
	ldq	$2, 16($30)
	ldq	$3, 32($30)
	lda	$30,32($30)
	ret	$31,($23),1
	.end	function
