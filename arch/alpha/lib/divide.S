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
 *	__remqu: 64-bit unsigned long remainder
 *	__divqs/__remqs: signed 64-bit
 *	__divlu/__remlu: unsigned 32-bit
 *	__divls/__remls: signed 32-bit
 *
 * These are not normal C functions: instead of the normal
 * calling sequence, these expect their arguments in registers
 * $24 and $25, and return the result in $27. Register $28 may
 * be clobbered (assembly temporary), anything else must be saved. 
 *
 * In short: painful.
 *
 * This is a rather simple bit-at-a-time algorithm: it's very good
 * at dividing random 64-bit numbers, but the more usual case where
 * the divisor is small is handled better by the DEC algorithm
 * using lookup tables. This uses much less memory, though, and is
 * nicer on the cache.. Besides, I don't know the copyright status
 * of the DEC code.
 */

/*
 * My temporaries:
 *	$0 - current bit
 *	$1 - shifted divisor
 *	$2 - modulus/quotient
 *
 *	$23 - return address
 *	$24 - dividend
 *	$25 - divisor
 *
 *	$27 - quotient/modulus
 *	$28 - compare status
 */

#define halt .long 0

/*
 * Select function type and registers
 */
#define mask	$0
#define divisor	$1
#define compare $28

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
#define ufunction func(lu)
#define sfunction func(l)
#define LONGIFY(x) zapnot x,15,x
#define SLONGIFY(x) addl x,0,x
#else
#define ufunction func(qu)
#define sfunction func(q)
#define LONGIFY(x)
#define SLONGIFY(x)
#endif

.set noat
.globl	ufunction
.ent	ufunction
ufunction:
	subq	$30,32,$30
	stq	$0, 0($30)
	stq	$1, 8($30)
	stq	$2,16($30)

	bis	$25,$25,divisor
	bis	$24,$24,modulus
	bis	$31,$31,quotient
	LONGIFY(divisor)
	LONGIFY(modulus)
	beq	divisor, 9f			/* div by zero */
	bis	$31,1,mask

	/* shift divisor left */
1:	cmpult	divisor,modulus,compare
	blt	divisor, 3f
	addq	divisor,divisor,divisor
	addq	mask,mask,mask
	bne	compare,1b

	/* ok, start to go right again.. */
2:	srl	divisor,1,divisor
	beq	mask,9f
	srl	mask,1,mask
3:	cmpule	divisor,modulus,compare
	beq	compare,2b
	addq	quotient,mask,quotient
	beq	mask,9f
	subq	modulus,divisor,modulus
	br	2b

9:	ldq	$0, 0($30)
	ldq	$1, 8($30)
	ldq	$2, 16($30)
	addq	$30,32,$30
	ret	$31,($23),1
	.end	ufunction

/*
 * The "signed" version just does a halt if either of the value is
 * signed: the kernel shouldn't mess with signed divides anyway (who
 * knows what way they'll round..)
 */
.globl	sfunction
.ent	sfunction
sfunction:
	bis	$24,$25,$28
	SLONGIFY($28)
	bge	$28,ufunction
	halt
	.end	sfunction
