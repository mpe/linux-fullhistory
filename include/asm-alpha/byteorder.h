#ifndef _ALPHA_BYTEORDER_H
#define _ALPHA_BYTEORDER_H

#include <asm/types.h>

/* EGCS 1.1 can, without scheduling, do just as good as we do here
   with the standard macros.  And since it can schedule, it does even
   better in the end.  */

#if defined(__GNUC__) && __GNUC_MINOR__ < 91

static __inline__ __const__ __u32 ___arch__swab32(__u32 x)
{
	__u64 t1, t2, t3;

	/* Break the final or's out of the block so that gcc can
	   schedule them at will.  Further, use add not or so that
	   we elide the sign extend gcc will put in because the
	   return type is not a long.  */

	__asm__(
	"insbl	%3,3,%1		# %1 = dd000000\n\t"
	"zapnot	%3,2,%2		# %2 = 0000cc00\n\t"
	"sll	%2,8,%2		# %2 = 00cc0000\n\t"
	"or	%2,%1,%1	# %1 = ddcc0000\n\t"
	"zapnot	%3,4,%2		# %2 = 00bb0000\n\t"
	"extbl	%3,3,%0		# %0 = 000000aa\n\t"
	"srl	%2,8,%2		# %2 = 0000bb00"
	: "=r"(t3), "=&r"(t1), "=&r"(t2)
	: "r"(x));

	return t3 + t2 + t1;
}

static __inline__ __const__ __u16 ___arch__swab16(__u16 x)
{
	__u64 t1, t2;

	__asm__(
	"insbl	%2,1,%1		# %1 = bb00\n\t"
	"extbl	%2,1,%0		# %0 = 00aa"
	: "=r"(t1), "=&r"(t2) : "r"(x));

	return t1 | t2;
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#endif /* __GNUC__ */

#define __BYTEORDER_HAS_U64__

#include <linux/byteorder/little_endian.h>

#endif /* _ALPHA_BYTEORDER_H */
