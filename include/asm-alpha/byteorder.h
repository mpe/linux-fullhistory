#ifndef _ALPHA_BYTEORDER_H
#define _ALPHA_BYTEORDER_H

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN
#endif

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);

extern unsigned long int	__ntohl(unsigned long int);
extern unsigned short int	__ntohs(unsigned short int);

#ifdef __GNUC__

extern unsigned long int	__constant_ntohl(unsigned long int);
extern unsigned short int	__constant_ntohs(unsigned short int);

/*
 * The constant and non-constant versions here are the same.
 * Maybe I'll come up with an alpha-optimized routine for the
 * non-constant ones (the constant ones don't need it: gcc
 * will optimize it to the correct constant)
 */

extern __inline__ unsigned long int
__ntohl(unsigned long int x)
{
	unsigned long int res, t1, t2;

	__asm__(
	"# bswap input: %0 (aabbccdd)\n\t"
	"# output: %0, used %1 %2\n\t"
	"extlh	%0,5,%1		# %1 = dd000000\n\t"
	"zap	%0,0xfd,%2	# %2 = 0000cc00\n\t"
	"sll	%2,5,%2		# %2 = 00198000\n\t"
	"s8addq	%2,%1,%1	# %1 = ddcc0000\n\t"
	"zap	%0,0xfb,%2	# %2 = 00bb0000\n\t"
	"srl	%2,8,%2		# %2 = 0000bb00\n\t"
	"extbl	%0,3,%0		# %0 = 000000aa\n\t"
	"or	%1,%0,%0	# %0 = ddcc00aa\n\t"
	"or	%2,%0,%0	# %0 = ddccbbaa\n"
	: "r="(res), "r="(t1), "r="(t2)
	: "0" (x & 0xffffffffUL));
	return res;
}

#define __constant_ntohl(x) \
   ((unsigned long int)((((x) & 0x000000ffUL) << 24) | \
			(((x) & 0x0000ff00UL) <<  8) | \
			(((x) & 0x00ff0000UL) >>  8) | \
			(((x) & 0xff000000UL) >> 24)))

extern __inline__ unsigned short int
__ntohs(unsigned short int x)
{
	unsigned long int res, t1;
	
	__asm__(
	"# v0 is result; swap in-place.\n\t"
	"bis	%2,%2,%0	# v0 = aabb\n\t"
	"extwh	%0,7,%1		# t1 = bb00\n\t"
	"extbl	%0,1,%0		# v0 = 00aa\n\t"
	"bis	%0,%1,%0	# v0 = bbaa\n"
	: "r="(res), "r="(t1) : "r"(x));
	return res;
}

#define __constant_ntohs(x) \
((unsigned short int)((((unsigned short int)(x) & 0x00ff) << 8) | \
		      (((unsigned short int)(x) & 0xff00) >> 8)))

#define __htonl(x) __ntohl(x)
#define __htons(x) __ntohs(x)
#define __constant_htonl(x) __constant_ntohl(x)
#define __constant_htons(x) __constant_ntohs(x)

#ifdef  __OPTIMIZE__
#  define ntohl(x) \
(__builtin_constant_p((long)(x)) ? \
 __constant_ntohl((x)) : \
 __ntohl((x)))
#  define ntohs(x) \
(__builtin_constant_p((short)(x)) ? \
 __constant_ntohs((x)) : \
 __ntohs((x)))
#  define htonl(x) \
(__builtin_constant_p((long)(x)) ? \
 __constant_htonl((x)) : \
 __htonl((x)))
#  define htons(x) \
(__builtin_constant_p((short)(x)) ? \
 __constant_htons((x)) : \
 __htons((x)))
#endif /* __OPTIMIZE__ */

#endif /* __GNUC__ */

#endif /* _ALPHA_BYTEORDER_H */
