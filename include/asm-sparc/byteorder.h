#ifndef _SPARC_BYTEORDER_H
#define _SPARC_BYTEORDER_H

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

#define BIG_ENDIAN
#define BIG_ENDIAN_BITFIELD

extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);

extern unsigned long int	__ntohl(unsigned long int);
extern unsigned short int	__ntohs(unsigned short int);
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
	return (((x & 0x000000ffU) << 24) |
		((x & 0x0000ff00U) <<  8) |
		((x & 0x00ff0000U) >>  8) |
		((x & 0xff000000U) >> 24));
}

extern __inline__ unsigned long int
__constant_ntohl(unsigned long int x)
{
	return (((x & 0x000000ffU) << 24) |
		((x & 0x0000ff00U) <<  8) |
		((x & 0x00ff0000U) >>  8) |
		((x & 0xff000000U) >> 24));
}

extern __inline__ unsigned short int
__ntohs(unsigned short int x)
{
	return (((x & 0x00ff) << 8) |
		((x & 0xff00) >> 8));
}

extern __inline__ unsigned short int
__constant_ntohs(unsigned short int x)
{
	return (((x & 0x00ff) << 8) |
		((x & 0xff00) >> 8));
}

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
#endif

#endif /* !(_SPARC_BYTEORDER_H) */
