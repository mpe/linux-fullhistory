#ifndef _ALPHA_BYTEORDER_H
#define _ALPHA_BYTEORDER_H

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

extern unsigned int		ntohl(unsigned int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned int		htonl(unsigned int);
extern unsigned short int	htons(unsigned short int);

extern unsigned int		__ntohl(unsigned int);
extern unsigned short int	__ntohs(unsigned short int);

#ifdef __GNUC__

extern unsigned int		__constant_ntohl(unsigned int);
extern unsigned short int	__constant_ntohs(unsigned short int);

extern __inline__ unsigned int
__ntohl(unsigned int x)
{
	unsigned int t1, t2, t3;

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

#define __constant_ntohl(x) \
   ((unsigned int)((((x) & 0x000000ff) << 24) | \
		   (((x) & 0x0000ff00) <<  8) | \
		   (((x) & 0x00ff0000) >>  8) | \
		   (((x) & 0xff000000) >> 24)))

extern __inline__ unsigned short int
__ntohs(unsigned short int x)
{
	unsigned short int t1, t2;
	
	__asm__(
	"insbl	%2,1,%1		# %1 = bb00\n\t"
	"extbl	%2,1,%0		# %0 = 00aa"
	: "=r"(t1), "=&r"(t2) : "r"(x));

	return t1 | t2;
}

#define __constant_ntohs(x) \
((unsigned short int)((((x) & 0x00ff) << 8) | \
		      (((x) & 0xff00) >> 8)))

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

#ifdef __KERNEL__

/*
 * In-kernel byte order macros to handle stuff like
 * byte-order-dependent filesystems etc.
 */
#define cpu_to_le32(x) (x)
#define cpu_to_le16(x) (x)

#define cpu_to_be32(x) htonl((x))
#define cpu_to_be16(x) htons((x))

/* The same, but returns converted value from the location pointer by addr. */
extern __inline__ __u16 cpu_to_le16p(__u16 *addr)
{
	return cpu_to_le16(*addr);
}

extern __inline__ __u32 cpu_to_le32p(__u32 *addr)
{
	return cpu_to_le32(*addr);
}

extern __inline__ __u16 cpu_to_be16p(__u16 *addr)
{
	return cpu_to_be16(*addr);
}

extern __inline__ __u32 cpu_to_be32p(__u32 *addr)
{
	return cpu_to_be32(*addr);
}

/* The same, but do the conversion in situ, ie. put the value back to addr. */
#define cpu_to_le16s(x) do { } while (0)
#define cpu_to_le32s(x) do { } while (0)

extern __inline__ void cpu_to_be16s(__u16 *addr)
{
	*addr = cpu_to_be16(*addr);
}

extern __inline__ void cpu_to_be32s(__u32 *addr)
{
	*addr = cpu_to_be32(*addr);
}

/* Convert from specified byte order, to CPU byte order. */
#define le16_to_cpu(x)  cpu_to_le16(x)
#define le32_to_cpu(x)  cpu_to_le32(x)
#define be16_to_cpu(x)  cpu_to_be16(x)
#define be32_to_cpu(x)  cpu_to_be32(x)

#define le16_to_cpup(x) cpu_to_le16p(x)
#define le32_to_cpup(x) cpu_to_le32p(x)
#define be16_to_cpup(x) cpu_to_be16p(x)
#define be32_to_cpup(x) cpu_to_be32p(x)

#define le16_to_cpus(x) cpu_to_le16s(x)
#define le32_to_cpus(x) cpu_to_le32s(x)
#define be16_to_cpus(x) cpu_to_be16s(x)
#define be32_to_cpus(x) cpu_to_be32s(x)

#endif /* __KERNEL__ */

#endif /* _ALPHA_BYTEORDER_H */
