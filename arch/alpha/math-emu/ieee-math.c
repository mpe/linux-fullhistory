/*
 * ieee-math.c - IEEE floating point emulation code
 * Copyright (C) 1989,1990,1991,1995 by
 * Digital Equipment Corporation, Maynard, Massachusetts.
 *
 * Heavily modified for Linux/Alpha.  Changes are Copyright (c) 1995
 * by David Mosberger (davidm@azstarnet.com).
 *
 * This file may be redistributed according to the terms of the
 * GNU General Public License.
 */
/*
 * The original code did not have any comments. I have created many
 * comments as I fix the bugs in the code.  My comments are based on
 * my observation and interpretation of the code.  If the original
 * author would have spend a few minutes to comment the code, we would
 * never had a problem of misinterpretation.  -HA
 *
 * This code could probably be a lot more optimized (especially the
 * division routine).  However, my foremost concern was to get the
 * IEEE behavior right.  Performance is less critical as these
 * functions are used on exceptional numbers only (well, assuming you
 * don't turn on the "trap on inexact"...).
 */
#include "ieee-math.h"

#define STICKY_S	0x20000000	/* both in longword 0 of fraction */
#define STICKY_T	1

/*
 * Careful: order matters here!
 */
enum {
	NaN, QNaN, INFTY, ZERO, DENORM, NORMAL
};

enum {
	SINGLE, DOUBLE
};

typedef unsigned long fpclass_t;

#define IEEE_TMAX	0x7fefffffffffffff
#define IEEE_SMAX	0x47efffffe0000000
#define IEEE_SNaN	0xfff00000000f0000
#define IEEE_QNaN	0xfff8000000000000
#define IEEE_PINF	0x7ff0000000000000
#define IEEE_NINF	0xfff0000000000000


/*
 * The memory format of S floating point numbers differs from the
 * register format.  In the following, the bitnumbers above the
 * diagram below give the memory format while the numbers below give
 * the register format.
 *
 *	  31 30	     23 22				0
 *	+-----------------------------------------------+
 * S	| s |	exp    |       fraction			|
 *	+-----------------------------------------------+
 *	  63 62	     52 51			      29
 *	
 * For T floating point numbers, the register and memory formats
 * match:
 *
 *	+-------------------------------------------------------------------+
 * T	| s |	     exp	|	    frac | tion			    |
 *	+-------------------------------------------------------------------+
 *	  63 62		      52 51	       32 31			   0
 */
typedef struct {
	unsigned long	f[2];	/* bit 55 in f[0] is the factor of 2^0*/
	int		s;	/* 1 bit sign (0 for +, 1 for -) */
	int		e;	/* 16 bit signed exponent */
} EXTENDED;


/*
 * Return the sign of a Q integer, S or T fp number in the register
 * format.
 */
static inline int
sign (unsigned long a)
{
	if ((long) a < 0)
		return -1;
	else
		return 1;
}


static inline long
cmp128 (const long a[2], const long b[2])
{
	if (a[1] < b[1]) return -1;
	if (a[1] > b[1]) return  1;
	return a[0] - b[0];
}


static inline void
sll128 (unsigned long a[2])
{
	a[1] = (a[1] << 1) | (a[0] >> 63);
	a[0] <<= 1;
}


static inline void
srl128 (unsigned long a[2])
{
	a[0] = (a[0] >> 1) | (a[1] << 63);
	a[1] >>= 1;
}


static inline void
add128 (const unsigned long a[2], const unsigned long b[2], unsigned long c[2])
{
	unsigned long carry = a[0] > (0xffffffffffffffff - b[0]);

	c[0] = a[0] + b[0];
	c[1] = a[1] + b[1] + carry;
}


static inline void
sub128 (const unsigned long a[2], const unsigned long b[2], unsigned long c[2])
{
	unsigned long borrow = a[0] < b[0];

	c[0] = a[0] - b[0];
	c[1] = a[1] - b[1] - borrow;
}


static inline void
mul64 (const unsigned long a, const unsigned long b, unsigned long c[2])
{
	c[0] = a * b;
	asm ("umulh %1,%2,%0" : "=r"(c[1]) : "r"(a), "r"(b));
}


static void
div128 (unsigned long a[2], unsigned long b[2], unsigned long c[2])
{
	unsigned long mask[2] = {1, 0};

	/*
	 * Shift b until either the sign bit is set or until it is at
	 * least as big as the dividend:
	 */
	while (cmp128(b, a) < 0 && sign(b[1]) >= 0) {
		sll128(b);
		sll128(mask);
	}
	c[0] = c[1] = 0;
	do {
		if (cmp128(a, b) >= 0) {
			sub128(a, b, a);
			add128(mask, c, c);
		}
		srl128(mask);
		srl128(b);
	} while (mask[0] || mask[1]);
}


static void
normalize (EXTENDED *a)
{
	if (!a->f[0] && !a->f[1])
		return;		/* zero fraction, unnormalizable... */
	/*
	 * In "extended" format, the "1" in "1.f" is explicit; it is
	 * in bit 55 of f[0], and the decimal point is understood to
	 * be between bit 55 and bit 54.  To normalize, shift the
	 * fraction until we have a "1" in bit 55.
	 */
	if ((a->f[0] & 0xff00000000000000) != 0 || a->f[1] != 0) {
		/*
		 * Mantissa is greater than 1.0:
		 */
		while ((a->f[0] & 0xff80000000000000) != 0x0080000000000000 ||
		       a->f[1] != 0)
		{
			unsigned long sticky;

			++a->e;
			sticky = a->f[0] & 1;
			srl128(a->f);
			a->f[0] |= sticky;
		}
		return;
	}

	if (!(a->f[0] & 0x0080000000000000)) {
		/*
		 * Mantissa is less than 1.0:
		 */
		while (!(a->f[0] & 0x0080000000000000)) {
			--a->e;
			a->f[0] <<= 1;
		}
		return;
	}
}


static inline fpclass_t
ieee_fpclass (unsigned long a)
{
	unsigned long exp, fract;

	exp   = (a >> 52) & 0x7ff;	/* 11 bits of exponent */
	fract = a & 0x000fffffffffffff;	/* 52 bits of fraction */
	if (exp == 0) {
		if (fract == 0)
			return ZERO;
		return DENORM;
	}
	if (exp == 0x7ff) {
		if (fract == 0)
			return INFTY;
		if (((fract >> 51) & 1) != 0)
			return QNaN;
		return NaN;
	}
	return NORMAL;
}


/*
 * Translate S/T fp number in register format into extended format.
 */
static fpclass_t
extend_ieee (unsigned long a, EXTENDED *b, int prec)
{
	fpclass_t result_kind;

	b->s = a >> 63;
	b->e = ((a >> 52) & 0x7ff) - 0x3ff;	/* remove bias */
	b->f[1] = 0;
	/*
	 * We shift f[1] left three bits so that the higher order bits
	 * of the fraction will reside in bits 55 through 0 of f[0].
	 */
	b->f[0] = (a & 0x000fffffffffffff) << 3;
	result_kind = ieee_fpclass(a);
	if (result_kind == NORMAL) {
		/* set implied 1. bit: */
		b->f[0] |= 1UL << 55;
	} else if (result_kind == DENORM) {
		if (prec == SINGLE)
			b->e = -126;
		else
			b->e = -1022;
	}
	return result_kind;
}


/*
 * INPUT PARAMETERS:
 *       a           a number in EXTENDED format to be converted to
 *                   s-floating format.
 *	 f	     rounding mode and exception enable bits.
 * OUTPUT PARAMETERS:
 *       b          will contain the s-floating number that "a" was
 *                  converted to (in register format).
 */
static unsigned long
make_s_ieee (long f, EXTENDED *a, unsigned long *b)
{
	unsigned long res, sticky;

	if (!a->e && !a->f[0] && !a->f[1]) {
		*b = (unsigned long) a->s << 63;	/* return +/-0 */
		return 0;
	}

	normalize(a);
	res = 0;

	if (a->e < -0x7e) {
		res = FPCR_INE;
		if (f & IEEE_TRAP_ENABLE_UNF) {
			res |= FPCR_UNF;
			a->e += 0xc0;	/* scale up result by 2^alpha */
		} else {
			/* try making denormalized number: */
			while (a->e < -0x7e) {
				++a->e;
				sticky = a->f[0] & 1;
				srl128(a->f);
				if (!a->f[0] && !a->f[0]) {
					/* underflow: replace with exact 0 */
					res |= FPCR_UNF;
					break;
				}
				a->f[0] |= sticky;
			}
			a->e = -0x3ff;
		}
	}
	if (a->e >= 0x80) {
		res = FPCR_OVF | FPCR_INE;
		if (f & IEEE_TRAP_ENABLE_OVF) {
			a->e -= 0xc0;	/* scale down result by 2^alpha */
		} else {
			/*
			 * Overflow without trap enabled, substitute
			 * result according to rounding mode:
			 */
			switch (RM(f)) {
			      case ROUND_NEAR:
				*b = IEEE_PINF;
				break;

			      case ROUND_CHOP:
				*b = IEEE_SMAX;
				break;

			      case ROUND_NINF:
				if (a->s) {
					*b = IEEE_PINF;
				} else {
					*b = IEEE_SMAX;
				}
				break;

			      case ROUND_PINF: 
				if (a->s) {
					*b = IEEE_SMAX;
				} else {
					*b = IEEE_PINF;
				}
				break;
			}
			*b |= ((unsigned long) a->s << 63);
			return res;
		}
	}

	*b = (((unsigned long) a->s << 63) |
	      (((unsigned long) a->e + 0x3ff) << 52) |
	      ((a->f[0] >> 3) & 0x000fffffe0000000));
	return res;
}


static unsigned long
make_t_ieee (long f, EXTENDED *a, unsigned long *b)
{
	unsigned long res, sticky;

	if (!a->e && !a->f[0] && !a->f[1]) {
		*b = (unsigned long) a->s << 63;	/* return +/-0 */
		return 0;
	}

	normalize(a);
	res = 0;
	if (a->e < -0x3fe) {
		res = FPCR_INE;
		if (f & IEEE_TRAP_ENABLE_UNF) {
			res |= FPCR_UNF;
			a->e += 0x600;
		} else {
			/* try making denormalized number: */
			while (a->e < -0x3fe) {
				++a->e;
				sticky = a->f[0] & 1;
				srl128(a->f);
				if (!a->f[0] && !a->f[0]) {
					/* underflow: replace with exact 0 */
					res |= FPCR_UNF;
					break;
				}
				a->f[0] |= sticky;
			}
			a->e = -0x3ff;
		}
	}
	if (a->e >= 0x3ff) {
		res = FPCR_OVF | FPCR_INE;
		if (f & IEEE_TRAP_ENABLE_OVF) {
			a->e -= 0x600;	/* scale down result by 2^alpha */
		} else {
			/*
			 * Overflow without trap enabled, substitute
			 * result according to rounding mode:
			 */
			switch (RM(f)) {
			      case ROUND_NEAR:
				*b = IEEE_PINF;
				break;

			      case ROUND_CHOP:
				*b = IEEE_TMAX;
				break;

			      case ROUND_NINF:
				if (a->s) {
					*b = IEEE_PINF;
				} else {
					*b = IEEE_TMAX;
				}
				break;

			      case ROUND_PINF: 
				if (a->s) {
					*b = IEEE_TMAX;
				} else {
					*b = IEEE_PINF;
				}
				break;
			}
			*b |= ((unsigned long) a->s << 63);
			return res;
		}
	}
	*b = (((unsigned long) a->s << 63) |
	      (((unsigned long) a->e + 0x3ff) << 52) |
	      ((a->f[0] >> 3) & 0x000fffffffffffff));
	return res;
}


/*
 * INPUT PARAMETERS:
 *       a          EXTENDED format number to be rounded.
 *       rm	    integer with value ROUND_NEAR, ROUND_CHOP, etc.
 *                  indicates how "a" should be rounded to produce "b".
 * OUTPUT PARAMETERS:
 *       b          s-floating number produced by rounding "a".
 * RETURN VALUE:
 *       if no errors occurred, will be zero.  Else will contain flags
 *       like FPCR_INE_OP, etc.
 */
static unsigned long
round_s_ieee (int f, EXTENDED *a, unsigned long *b)
{
	unsigned long diff1, diff2, res = 0;
	EXTENDED z1, z2;

	if (!(a->f[0] & 0xffffffff)) {
		return make_s_ieee(f, a, b);	/* no rounding error */
	}

	/*
	 * z1 and z2 are the S-floating numbers with the next smaller/greater
	 * magnitude than a, respectively.
	 */
	z1.s = z2.s = a->s;
	z1.e = z2.e = a->e;
	z1.f[0] = z2.f[0] = a->f[0] & 0xffffffff00000000;
	z1.f[1] = z2.f[1] = 0;
	z2.f[0] += 0x100000000;	/* next bigger S float number */

	switch (RM(f)) {
	      case ROUND_NEAR:
		diff1 = a->f[0] - z1.f[0];
		diff2 = z2.f[0] - a->f[0];
		if (diff1 > diff2)
			res = make_s_ieee(f, &z2, b);
		else if (diff2 > diff1)
			res = make_s_ieee(f, &z1, b);
		else
			/* equal distance: round towards even */
			if (z1.f[0] & 0x100000000)
				res = make_s_ieee(f, &z2, b);
			else
				res = make_s_ieee(f, &z1, b);
		break;

	      case ROUND_CHOP:
		res = make_s_ieee(f, &z1, b);
		break;

	      case ROUND_PINF:
		if (a->s) {
			res = make_s_ieee(f, &z1, b);
		} else {
			res = make_s_ieee(f, &z2, b);
		}
		break;

	      case ROUND_NINF:
		if (a->s) {
			res = make_s_ieee(f, &z2, b);
		} else {
			res = make_s_ieee(f, &z1, b);
		}
		break;
	}
	return FPCR_INE | res;
}


static unsigned long
round_t_ieee (int f, EXTENDED *a, unsigned long *b)
{
	unsigned long diff1, diff2, res;
	EXTENDED z1, z2;

	if (!(a->f[0] & 0x7)) {
		/* no rounding error */
		return make_t_ieee(f, a, b);
	}

	z1.s = z2.s = a->s;
	z1.e = z2.e = a->e;
	z1.f[0] = z2.f[0] = a->f[0] & ~0x7;
	z1.f[1] = z2.f[1] = 0;
	z2.f[0] += (1 << 3);

	res = 0;
	switch (RM(f)) {
	      case ROUND_NEAR:
		diff1 = a->f[0] - z1.f[0];
		diff2 = z2.f[0] - a->f[0];
		if (diff1 > diff2)
			res = make_t_ieee(f, &z2, b);
		else if (diff2 > diff1)
			res = make_t_ieee(f, &z1, b);
		else
			/* equal distance: round towards even */
			if (z1.f[0] & (1 << 3))
				res = make_t_ieee(f, &z2, b);
			else
				res = make_t_ieee(f, &z1, b);
		break;

	      case ROUND_CHOP:
		res = make_t_ieee(f, &z1, b);
		break;

	      case ROUND_PINF:
		if (a->s) {
			res = make_t_ieee(f, &z1, b);
		} else {
			res = make_t_ieee(f, &z2, b);
		}
		break;

	      case ROUND_NINF:
		if (a->s) {
			res = make_t_ieee(f, &z2, b);
		} else {
			res = make_t_ieee(f, &z1, b);
		}
		break;
	}
	return FPCR_INE | res;
}


static fpclass_t
add_kernel_ieee (EXTENDED *op_a, EXTENDED *op_b, EXTENDED *op_c)
{
	unsigned long mask, fa, fb, fc;
	int diff;

	diff = op_a->e - op_b->e;
	fa = op_a->f[0];
	fb = op_b->f[0];
	if (diff < 0) {
		diff = -diff;
		op_c->e = op_b->e;
		mask = (1UL << diff) - 1;
		fa >>= diff;
		if (op_a->f[0] & mask) {
			fa |= 1;		/* set sticky bit */
		}
	} else {
		op_c->e = op_a->e;
		mask = (1UL << diff) - 1;
		fb >>= diff;
		if (op_b->f[0] & mask) {
			fb |= 1;		/* set sticky bit */
		}
	}
	if (op_a->s)
		fa = -fa;
	if (op_b->s)
		fb = -fb;
	fc = fa + fb;
	op_c->f[1] = 0;
	op_c->s = fc >> 63;
	if (op_c->s) {
		fc = -fc;
	}
	op_c->f[0] = fc;
	normalize(op_c);
	return 0;
}


/*
 * converts s-floating "a" to t-floating "b".
 *
 * INPUT PARAMETERS:
 *       a           a s-floating number to be converted
 *       f           the rounding mode (ROUND_NEAR, etc. )
 * OUTPUT PARAMETERS:
 *       b           the t-floating number that "a" is converted to.
 * RETURN VALUE:
 *       error flags - i.e., zero if no errors occurred,
 *       FPCR_INV if invalid operation occurred, etc.
 */
unsigned long
ieee_CVTST (int f, unsigned long a, unsigned long *b)
{
	EXTENDED temp;
	fpclass_t a_type;

	a_type = extend_ieee(a, &temp, SINGLE);
	if (a_type >= NaN && a_type <= INFTY) {
		*b = a;
		if (a_type == NaN) {
			*b |= (1UL << 51);	/* turn SNaN into QNaN */
			return FPCR_INV;
		}
		return 0;
	}
	return round_t_ieee(f, &temp, b);
}


/*
 * converts t-floating "a" to s-floating "b".
 *
 * INPUT PARAMETERS:
 *       a           a t-floating number to be converted
 *       f           the rounding mode (ROUND_NEAR, etc. )
 * OUTPUT PARAMETERS:
 *       b           the s-floating number that "a" is converted to.
 * RETURN VALUE:
 *       error flags - i.e., zero if no errors occurred,
 *       FPCR_INV if invalid operation occurred, etc.
 */
unsigned long
ieee_CVTTS (int f, unsigned long a, unsigned long *b)
{
	EXTENDED temp;
	fpclass_t a_type;

	a_type = extend_ieee(a, &temp, DOUBLE);
	if (a_type >= NaN && a_type <= INFTY) {
		*b = a;
		if (a_type == NaN) {
			*b |= (1UL << 51);	/* turn SNaN into QNaN */
			return FPCR_INV;
		}
		return 0;
	}
	return round_s_ieee(f, &temp, b);
}


/*
 * converts q-format (64-bit integer) "a" to s-floating "b".
 *
 * INPUT PARAMETERS:
 *       a           an 64-bit integer to be converted.
 *       f           the rounding mode (ROUND_NEAR, etc. )
 * OUTPUT PARAMETERS:
 *       b           the s-floating number "a" is converted to.
 * RETURN VALUE:
 *       error flags - i.e., zero if no errors occurred,
 *       FPCR_INV if invalid operation occurred, etc.
 */
unsigned long
ieee_CVTQS (int f, unsigned long a, unsigned long *b)
{
	EXTENDED op_b;

	op_b.s    = 0;
	op_b.f[0] = a;
	op_b.f[1] = 0;
	if (sign(a) < 0) {
		op_b.s = 1;
		op_b.f[0] = -a;
	}
	op_b.e = 55;
	normalize(&op_b);
	return round_s_ieee(f, &op_b, b);
}


/*
 * converts 64-bit integer "a" to t-floating "b".
 *
 * INPUT PARAMETERS:
 *       a           a 64-bit integer to be converted.
 *       f           the rounding mode (ROUND_NEAR, etc.)
 * OUTPUT PARAMETERS:
 *       b           the t-floating number "a" is converted to.
 * RETURN VALUE:
 *       error flags - i.e., zero if no errors occurred,
 *       FPCR_INV if invalid operation occurred, etc.
 */
unsigned long
ieee_CVTQT (int f, unsigned long a, unsigned long *b)
{
	EXTENDED op_b;

	op_b.s    = 0;
	op_b.f[0] = a;
	op_b.f[1] = 0;
	if (sign(a) < 0) {
		op_b.s = 1;
		op_b.f[0] = -a;
	}
	op_b.e = 55;
	normalize(&op_b);
	return round_t_ieee(f, &op_b, b);
}


/*
 * converts t-floating "a" to 64-bit integer (q-format) "b".
 *
 * INPUT PARAMETERS:
 *       a           a t-floating number to be converted.
 *       f           the rounding mode (ROUND_NEAR, etc. )
 * OUTPUT PARAMETERS:
 *       b           the 64-bit integer "a" is converted to.
 * RETURN VALUE:
 *       error flags - i.e., zero if no errors occurred,
 *       FPCR_INV if invalid operation occurred, etc.
 */
unsigned long
ieee_CVTTQ (int f, unsigned long a, unsigned long *pb)
{
	unsigned int midway;
	unsigned long ov, uv, res, b;
	fpclass_t a_type;
	EXTENDED temp;

	a_type = extend_ieee(a, &temp, DOUBLE);

	b = 0x7fffffffffffffff;
	res = FPCR_INV;
	if (a_type == NaN || a_type == INFTY)
		goto out;

	res = 0;
	if (a_type == QNaN)
		goto out;

	if (temp.e > 0) {
		ov = 0;
		while (temp.e > 0) {
			--temp.e;
			ov |= temp.f[1] >> 63;
			sll128(temp.f);
		}
		if (ov || (temp.f[1] & 0xffc0000000000000))
			res |= FPCR_IOV | FPCR_INE;
	}
	else if (temp.e < 0) {
		while (temp.e < 0) {
			++temp.e;
			uv = temp.f[0] & 1;		/* save sticky bit */
			srl128(temp.f);
			temp.f[0] |= uv;
		}
	}
	b = (temp.f[1] << 9) | (temp.f[0] >> 55);

	/*
	 * Notice: the fraction is only 52 bits long.  Thus, rounding
	 * cannot possibly result in an integer overflow.
	 */
	switch (RM(f)) {
	      case ROUND_NEAR:
		if (temp.f[0] & 0x0040000000000000) {
			midway = (temp.f[0] & 0x003fffffffffffff) == 0;
			if ((midway && (temp.f[0] & 0x0080000000000000)) ||
			    !midway)
				++b;
		}
		break;

	      case ROUND_PINF:
		b += ((temp.f[0] & 0x007fffffffffffff) != 0 && !temp.s);
		break;

	      case ROUND_NINF:
		b += ((temp.f[0] & 0x007fffffffffffff) != 0 && temp.s);
		break;

	      case ROUND_CHOP:
		/* no action needed */
		break;
	}
	if ((temp.f[0] & 0x007fffffffffffff) != 0)
		res |= FPCR_INE;

	if (temp.s) {
		b = -b;
	}

out:
	*pb = b;
	return res;
}


unsigned long
ieee_CMPTEQ (unsigned long a, unsigned long b, unsigned long *c)
{
	EXTENDED op_a, op_b;
	fpclass_t a_type, b_type;

	*c = 0;
	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if (a_type == NaN || b_type == NaN)
		return FPCR_INV;
	if (a_type == QNaN || b_type == QNaN)
		return 0;

	if ((op_a.e == op_b.e && op_a.s == op_b.s &&
	     op_a.f[0] == op_b.f[0] && op_a.f[1] == op_b.f[1]) ||
	    (a_type == ZERO && b_type == ZERO))
		*c = 0x4000000000000000;
	return 0;
}


unsigned long
ieee_CMPTLT (unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b;

	*c = 0;
	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if (a_type == NaN || b_type == NaN)
		return FPCR_INV;
	if (a_type == QNaN || b_type == QNaN)
		return 0;

	if ((op_a.s == 1 && op_b.s == 0 &&
	     (a_type != ZERO || b_type != ZERO)) ||
	    (op_a.s == 1 && op_b.s == 1 &&
	     (op_a.e > op_b.e || (op_a.e == op_b.e &&
				  cmp128(op_a.f, op_b.f) > 0))) ||
	    (op_a.s == 0 && op_b.s == 0 &&
	     (op_a.e < op_b.e || (op_a.e == op_b.e &&
				  cmp128(op_a.f,op_b.f) < 0))))
		*c = 0x4000000000000000;
	return 0;
}


unsigned long
ieee_CMPTLE (unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b;

	*c = 0;
	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if (a_type == NaN || b_type == NaN)
		return FPCR_INV;
	if (a_type == QNaN || b_type == QNaN)
		return 0;

	if ((a_type == ZERO && b_type == ZERO) ||
	    (op_a.s == 1 && op_b.s == 0) ||
	    (op_a.s == 1 && op_b.s == 1 &&
	     (op_a.e > op_b.e || (op_a.e == op_b.e &&
				  cmp128(op_a.f,op_b.f) >= 0))) ||
	    (op_a.s == 0 && op_b.s == 0 &&
	     (op_a.e < op_b.e || (op_a.e == op_b.e &&
				  cmp128(op_a.f,op_b.f) <= 0))))
		*c = 0x4000000000000000;
	return 0;
}


unsigned long
ieee_CMPTUN (unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b;

	*c = 0x4000000000000000;
	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if (a_type == NaN || b_type == NaN)
		return FPCR_INV;
	if (a_type == QNaN || b_type == QNaN)
		return 0;
	*c = 0;
	return 0;
}


/*
 * Add a + b = c, where a, b, and c are ieee s-floating numbers.  "f"
 * contains the rounding mode etc.
 */
unsigned long
ieee_ADDS (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	a_type = extend_ieee(a, &op_a, SINGLE);
	b_type = extend_ieee(b, &op_b, SINGLE);
	if ((a_type >= NaN && a_type <= INFTY) ||
	    (b_type >= NaN && b_type <= INFTY))
	{
		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		if (a_type == INFTY && b_type == INFTY && sign(a) != sign(b)) {
			*c = IEEE_QNaN;
			return FPCR_INV;
		}
		if (a_type == INFTY)
			*c = a;
		else
			*c = b;
		return 0;
	}

	add_kernel_ieee(&op_a, &op_b, &op_c);
	/* special case for -0 + -0 ==> -0 */
	if (a_type == ZERO && b_type == ZERO)
		op_c.s = op_a.s && op_b.s;
	return round_s_ieee(f, &op_c, c);
}


/*
 * Add a + b = c, where a, b, and c are ieee t-floating numbers.  "f"
 * contains the rounding mode etc.
 */
unsigned long
ieee_ADDT (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if ((a_type >= NaN && a_type <= INFTY) ||
	    (b_type >= NaN && b_type <= INFTY))
	{
		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		if (a_type == INFTY && b_type == INFTY && sign(a) != sign(b)) {
			*c = IEEE_QNaN;
			return FPCR_INV;
		}
		if (a_type == INFTY)
			*c = a;
		else
			*c = b;
		return 0;
	}
	add_kernel_ieee(&op_a, &op_b, &op_c);
	/* special case for -0 + -0 ==> -0 */
	if (a_type == ZERO && b_type == ZERO)
		op_c.s = op_a.s && op_b.s;

	return round_t_ieee(f, &op_c, c);
}


/*
 * Subtract a - b = c, where a, b, and c are ieee s-floating numbers.
 * "f" contains the rounding mode etc.
 */
unsigned long
ieee_SUBS (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	a_type = extend_ieee(a, &op_a, SINGLE);
	b_type = extend_ieee(b, &op_b, SINGLE);
	if ((a_type >= NaN && a_type <= INFTY) ||
	    (b_type >= NaN && b_type <= INFTY))
	{
		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		if (a_type == INFTY && b_type == INFTY && sign(a) == sign(b)) {
			*c = IEEE_QNaN;
			return FPCR_INV;
		}
		if (a_type == INFTY)
			*c = a;
		else
			*c = b ^ (1UL << 63);
		return 0;
	}
	op_b.s = !op_b.s;
	add_kernel_ieee(&op_a, &op_b, &op_c);
	/* special case for -0 - +0 ==> -0 */
	if (a_type == ZERO && b_type == ZERO)
		op_c.s = op_a.s && op_b.s;

	return round_s_ieee(f, &op_c, c);
}


/*
 * Subtract a - b = c, where a, b, and c are ieee t-floating numbers.
 * "f" contains the rounding mode etc.
 */
unsigned long
ieee_SUBT (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if ((a_type >= NaN && a_type <= INFTY) ||
	    (b_type >= NaN && b_type <= INFTY))
	{
		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		if (a_type == INFTY && b_type == INFTY && sign(a) == sign(b)) {
			*c = IEEE_QNaN;
			return FPCR_INV;
		}
		if (a_type == INFTY)
			*c = a;
		else
			*c = b ^ (1UL << 63);
		return 0;
	}
	op_b.s = !op_b.s;
	add_kernel_ieee(&op_a, &op_b, &op_c);
	/* special case for -0 - +0 ==> -0 */
	if (a_type == ZERO && b_type == ZERO)
		op_c.s = op_a.s && op_b.s;

	return round_t_ieee(f, &op_c, c);
}


/*
 * Multiply a x b = c, where a, b, and c are ieee s-floating numbers.
 * "f" contains the rounding mode.
 */
unsigned long
ieee_MULS (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	a_type = extend_ieee(a, &op_a, SINGLE);
	b_type = extend_ieee(b, &op_b, SINGLE);
	if ((a_type >= NaN && a_type <= INFTY) ||
	    (b_type >= NaN && b_type <= INFTY))
	{
		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		if ((a_type == INFTY && b_type == ZERO) ||
		    (b_type == INFTY && a_type == ZERO))
		{
			*c = IEEE_QNaN;		/* return canonical QNaN */
			return FPCR_INV;
		}
		if (a_type == INFTY)
			*c = a ^ ((b >> 63) << 63);
		else if (b_type == INFTY)
			*c = b ^ ((a >> 63) << 63);
		else
			/* either of a and b are +/-0 */
			*c = ((unsigned long) op_a.s ^ op_b.s) << 63;
		return 0;
	}
	op_c.s = op_a.s ^ op_b.s;
	op_c.e = op_a.e + op_b.e - 55;
	mul64(op_a.f[0], op_b.f[0], op_c.f);

	return round_s_ieee(f, &op_c, c);
}


/*
 * Multiply a x b = c, where a, b, and c are ieee t-floating numbers.
 * "f" contains the rounding mode.
 */
unsigned long
ieee_MULT (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	*c = IEEE_QNaN;
	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if ((a_type >= NaN && a_type <= ZERO) ||
	    (b_type >= NaN && b_type <= ZERO))
	{
		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		if ((a_type == INFTY && b_type == ZERO) ||
		    (b_type == INFTY && a_type == ZERO))
		{
			*c = IEEE_QNaN;		/* return canonical QNaN */
			return FPCR_INV;
		}
		if (a_type == INFTY)
			*c = a ^ ((b >> 63) << 63);
		else if (b_type == INFTY)
			*c = b ^ ((a >> 63) << 63);
		else
			/* either of a and b are +/-0 */
			*c = ((unsigned long) op_a.s ^ op_b.s) << 63;
		return 0;
	}
	op_c.s = op_a.s ^ op_b.s;
	op_c.e = op_a.e + op_b.e - 55;
	mul64(op_a.f[0], op_b.f[0], op_c.f);

	return round_t_ieee(f, &op_c, c);
}


/*
 * Divide a / b = c, where a, b, and c are ieee s-floating numbers.
 * "f" contains the rounding mode etc.
 */
unsigned long
ieee_DIVS (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	a_type = extend_ieee(a, &op_a, SINGLE);
	b_type = extend_ieee(b, &op_b, SINGLE);
	if ((a_type >= NaN && a_type <= ZERO) ||
	    (b_type >= NaN && b_type <= ZERO))
	{
		unsigned long res;

		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		res = 0;
		*c = IEEE_PINF;
		if (a_type == INFTY) {
			if (b_type == INFTY) {
				*c = IEEE_QNaN;
				return FPCR_INV;
			}
		} else if (b_type == ZERO) {
			if (a_type == ZERO) {
				*c = IEEE_QNaN;
				return FPCR_INV;
			}
			res = FPCR_DZE;
		} else
			/* a_type == ZERO || b_type == INFTY */
			*c = 0;
		*c |= (unsigned long) (op_a.s ^ op_b.s) << 63;
		return res;
	}
	op_c.s = op_a.s ^ op_b.s;
	op_c.e = op_a.e - op_b.e;

	op_a.f[1] = op_a.f[0];
	op_a.f[0] = 0;
	div128(op_a.f, op_b.f, op_c.f);
	if (a_type != ZERO)
		/* force a sticky bit because DIVs never hit exact .5: */
		op_c.f[0] |= STICKY_S;
	normalize(&op_c);
	op_c.e -= 9;		/* remove excess exp from original shift */
	return round_s_ieee(f, &op_c, c);
}


/*
 * Divide a/b = c, where a, b, and c are ieee t-floating numbers.  "f"
 * contains the rounding mode etc.
 */
unsigned long
ieee_DIVT (int f, unsigned long a, unsigned long b, unsigned long *c)
{
	fpclass_t a_type, b_type;
	EXTENDED op_a, op_b, op_c;

	*c = IEEE_QNaN;
	a_type = extend_ieee(a, &op_a, DOUBLE);
	b_type = extend_ieee(b, &op_b, DOUBLE);
	if ((a_type >= NaN && a_type <= ZERO) ||
	    (b_type >= NaN && b_type <= ZERO))
	{
		unsigned long res;

		/* propagate NaNs according to arch. ref. handbook: */
		if (b_type == QNaN)
			*c = b;
		else if (b_type == NaN)
			*c = b | (1UL << 51);
		else if (a_type == QNaN)
			*c = a;
		else if (a_type == NaN)
			*c = a | (1UL << 51);

		if (a_type == NaN || b_type == NaN)
			return FPCR_INV;
		if (a_type == QNaN || b_type == QNaN)
			return 0;

		res = 0;
		*c = IEEE_PINF;
		if (a_type == INFTY) {
			if (b_type == INFTY) {
				*c = IEEE_QNaN;
				return FPCR_INV;
			}
		} else if (b_type == ZERO) {
			if (a_type == ZERO) {
				*c = IEEE_QNaN;
				return FPCR_INV;
			}
			res = FPCR_DZE;
		} else
			/* a_type == ZERO || b_type == INFTY */
			*c = 0;
		*c |= (unsigned long) (op_a.s ^ op_b.s) << 63;
		return res;
	}
	op_c.s = op_a.s ^ op_b.s;
	op_c.e = op_a.e - op_b.e;

	op_a.f[1] = op_a.f[0];
	op_a.f[0] = 0;
	div128(op_a.f, op_b.f, op_c.f);
	if (a_type != ZERO)
		/* force a sticky bit because DIVs never hit exact .5 */
		op_c.f[0] |= STICKY_T;
	normalize(&op_c);
	op_c.e -= 9;		/* remove excess exp from original shift */
	return round_t_ieee(f, &op_c, c);
}
