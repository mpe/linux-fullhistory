/*
 * Functions depending of the byteorder.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 *
 * $Id: byteorder.h,v 1.5 1997/06/25 19:10:18 ralf Exp $
 */
#ifndef __ASM_MIPS_BYTEORDER_H
#define __ASM_MIPS_BYTEORDER_H

#define __swap32(x) \
	((unsigned long int)((((unsigned long int)(x) & 0x000000ffU) << 24) | \
			     (((unsigned long int)(x) & 0x0000ff00U) <<  8) | \
			     (((unsigned long int)(x) & 0x00ff0000U) >>  8) | \
			     (((unsigned long int)(x) & 0xff000000U) >> 24)))
#define __swap16(x) \
	((unsigned short int)((((unsigned short int)(x) & 0x00ff) << 8) | \
			      (((unsigned short int)(x) & 0xff00) >> 8)))

#if defined (__MIPSEB__)

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN
#endif

#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif

#define __constant_ntohl(x) (x)
#define __constant_ntohs(x) (x)
#define __constant_htonl(x) (x)
#define __constant_htons(x) (x)

#ifdef __KERNEL__

/*
 * In-kernel byte order macros to handle stuff like
 * byte-order-dependent filesystems etc.
 */
#define cpu_to_le32(x) __swap32((x))
#define le32_to_cpu(x) __swap32((x))
#define cpu_to_le16(x) __swap16((x))
#define le16_to_cpu(x) __swap16((x))

#define cpu_to_be32(x) (x)
#define be32_to_cpu(x) (x)
#define cpu_to_be16(x) (x)
#define be16_to_cpu(x) (x)

#endif /* __KERNEL__ */

#elif defined (__MIPSEL__)

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN
#endif

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

#define __constant_ntohl(x) __swap32(x)
#define __constant_ntohs(x) __swap16(x)
#define __constant_htonl(x) __swap32(x)
#define __constant_htons(x) __swap16(x)

#ifdef __KERNEL__

/*
 * In-kernel byte order macros to handle stuff like
 * byte-order-dependent filesystems etc.
 */
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#define cpu_to_le16(x) (x)
#define le16_to_cpu(x) (x)

#define cpu_to_be32(x) __swap32((x))
#define be32_to_cpu(x) __swap32((x))
#define cpu_to_be16(x) __swap16((x))
#define be16_to_cpu(x) __swap16((x))

#endif /* __KERNEL__ */

#else
#error "MIPS but neither __MIPSEL__ nor __MIPSEB__?"
#endif

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

#define le16_to_cpup(x) cpu_to_le16p(x)
#define le32_to_cpup(x) cpu_to_le32p(x)
#define be16_to_cpup(x) cpu_to_be16p(x)
#define be32_to_cpup(x) cpu_to_be32p(x)


/* The same, but do the conversion in situ, ie. put the value back to addr. */
extern __inline__ void cpu_to_le16s(__u16 *addr)
{
	*addr = cpu_to_le16(*addr);
}

extern __inline__ void cpu_to_le32s(__u32 *addr)
{
	*addr = cpu_to_le32(*addr);
}

extern __inline__ void cpu_to_be16s(__u16 *addr)
{
	*addr = cpu_to_be16(*addr);
}

extern __inline__ void cpu_to_be32s(__u32 *addr)
{
	*addr = cpu_to_be32(*addr);
}

#define le16_to_cpus(x) cpu_to_le16s(x)
#define le32_to_cpus(x) cpu_to_le32s(x)
#define be16_to_cpus(x) cpu_to_be16s(x)
#define be32_to_cpus(x) cpu_to_be32s(x)

#ifdef __KERNEL__
extern unsigned long int ntohl(unsigned long int __x);
extern unsigned short int ntohs(unsigned short int __x);
extern unsigned short int htons(unsigned short int __x);
extern unsigned long int htonl(unsigned long int __x);


extern __inline__ unsigned long int ntohl(unsigned long int __x)
{
	return __constant_ntohl(__x);
}

extern __inline__ unsigned short int ntohs(unsigned short int __x)
{
	return __constant_ntohs(__x);
}

extern __inline__ unsigned long int htonl(unsigned long int __x)
{
	return __constant_htonl(__x);
}

extern __inline__ unsigned short int htons(unsigned short int __x)
{
	return __constant_htons(__x);
}
#endif /* __KERNEL__ */

#endif /* __ASM_MIPS_BYTEORDER_H */
