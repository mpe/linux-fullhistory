#ifndef _LINUX_BYTEORDER_VAX_ENDIAN_H
#define _LINUX_BYTEORDER_VAX_ENDIAN_H

/*
 * Could have been named NUXI-endian
 * This file isn't operational.
 * It's the beginning of what vaxlinux implementers will have to do.
 * I just hope we won't have to write standardized cpu_to_ve32() and suches!
 * little endian is 1234; bigendian is 4321; vaxendian is 3412
 * But what does a __u64 look like: is it 34127856 or 78563412 ???
 * I don't dare imagine!
 */

#ifndef __VAX_ENDIAN
#define __VAX_ENDIAN 3412
#endif
#ifndef __VAX_ENDIAN_BITFIELD
#define __VAX_ENDIAN_BITFIELD
#endif

#define ___swahw32(x) \
	((__u32)( \
		(((__u32)(x) & (__u32)0x0000ffffUL) << 16) | \
		(((__u32)(x) & (__u32)0xffff0000UL) >> 16) ))
#define ___swahb32(x) \
	((__u32)( \
		(((__u32)(x) & (__u32)0x00ff00ffUL) << 16) | \
		(((__u32)(x) & (__u32)0xff00ff00UL) >> 16) ))


#ifndef __arch__swahw32
#  define __arch__swahw32(x) ___swahw32(x)
#endif
#ifndef __arch__swahb32
#  define __arch__swahb32(x) ___swahb32(x)
#endif
#ifndef __arch__swahw32p
#  define __arch__swahw32p(x) __swahw32(*(x))
#endif
#ifndef __arch__swahb32p
#  define __arch__swahb32p(x) __swahb32(*(x))
#endif
#ifndef __arch__swahw32s
#  define __arch__swahw32s(x) *(x) = __swahw32p((x))
#endif
#ifndef __arch__swahb32s
#  define __arch__swahb32s(x) *(x) = __swahb32p((x))
#endif


#define __constant_htonl(x) ___swahb32((x))
#define __constant_ntohl(x) ___swahb32((x))
#define __constant_htons(x) ___swab16((x))
#define __constant_ntohs(x) ___swab16((x))
#define __cpu_to_le64(x) I DON'T KNOW
#define __le64_to_cpu(x) I DON'T KNOW
#define __cpu_to_le32(x) ___swahw32((x))
#define __le32_to_cpu(x) ___swahw32((x))
#define __cpu_to_le16(x) ((__u16)(x)
#define __le16_to_cpu(x) ((__u16)(x)
#define __cpu_to_be64(x) I DON'T KNOW
#define __be64_to_cpu(x) I DON'T KNOW
#define __cpu_to_be32(x) __swahb32((x))
#define __be32_to_cpu(x) __swahb32((x))
#define __cpu_to_be16(x) __swab16((x))
#define __be16_to_cpu(x) __swab16((x))
#define __cpu_to_le64p(x) I DON'T KNOW
#define __le64_to_cpup(x) I DON'T KNOW
#define __cpu_to_le32p(x) ___swahw32p((x))
#define __le32_to_cpup(x) ___swahw32p((x))
#define __cpu_to_le16p(x) (*(__u16*)(x))
#define __le16_to_cpup(x) (*(__u16*)(x))
#define __cpu_to_be64p(x) I DON'T KNOW
#define __be64_to_cpup(x) I DON'T KNOW
#define __cpu_to_be32p(x) __swahb32p((x))
#define __be32_to_cpup(x) __swahb32p((x))
#define __cpu_to_be16p(x) __swab16p((x))
#define __be16_to_cpup(x) __swab16p((x))
#define __cpu_to_le64s(x) I DON'T KNOW
#define __le64_to_cpus(x) I DON'T KNOW
#define __cpu_to_le32s(x) ___swahw32s((x))
#define __le32_to_cpus(x) ___swahw32s((x))
#define __cpu_to_le16s(x) do {} while (0)
#define __le16_to_cpus(x) do {} while (0)
#define __cpu_to_be64s(x) I DON'T KNOW
#define __be64_to_cpus(x) I DON'T KNOW
#define __cpu_to_be32s(x) __swahb32s((x))
#define __be32_to_cpus(x) __swahb32s((x))
#define __cpu_to_be16s(x) __swab16s((x))
#define __be16_to_cpus(x) __swab16s((x))

#include <linux/byteorder_generic.h>

#endif /* _LINUX_BYTEORDER_VAX_ENDIAN_H */
