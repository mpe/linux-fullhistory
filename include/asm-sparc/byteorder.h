/* $Id: byteorder.h,v 1.14 1997/05/28 11:35:38 jj Exp $ */
#ifndef _SPARC_BYTEORDER_H
#define _SPARC_BYTEORDER_H

#define ntohl(x) ((unsigned long int)(x))
#define ntohs(x) ((unsigned short int)(x))
#define htonl(x) ((unsigned long int)(x))
#define htons(x) ((unsigned short int)(x))

/* Some programs depend upon these being around. */
#define __constant_ntohl(x) ((unsigned long int)(x))
#define __constant_ntohs(x) ((unsigned short int)(x))
#define __constant_htonl(x) ((unsigned long int)(x))
#define __constant_htons(x) ((unsigned short int)(x))

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#ifdef __KERNEL__

/* Convert from CPU byte order, to specified byte order. */
extern __inline__ __u16 cpu_to_le16(__u16 value)
{
	return (value >> 8) | (value << 8);
}

extern __inline__ __u32 cpu_to_le32(__u32 value)
{
	return((value>>24) | ((value>>8)&0xff00) |
	       ((value<<8)&0xff0000) | (value<<24));
}
#define cpu_to_be16(x)  (x)
#define cpu_to_be32(x)  (x)

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
extern __inline__ void cpu_to_le16s(__u16 *addr)
{
	*addr = cpu_to_le16(*addr);
}

extern __inline__ void cpu_to_le32s(__u32 *addr)
{
	*addr = cpu_to_le32(*addr);
}

#define cpu_to_be16s(x) do { } while (0)
#define cpu_to_be32s(x) do { } while (0)

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

#endif /* !(_SPARC_BYTEORDER_H) */
