#ifndef _LINUX_BYTEORDER_GENERIC_H
#define _LINUX_BYTEORDER_GENERIC_H

/*
 * linux/byteorder_generic.h
 * Generic Byteswap support
 *
 * Francois-Rene Rideau <rideau@ens.fr> 19970707
 *    gathered all the good ideas from all asm-foo/byteorder.h into one file,
 *    cleaned them up.
 *    I hope it is compliant with non-GCC compilers.
 *    I decided to put __BYTEORDER_HAS_U64__ in byteorder.h,
 *    because I wasn't sure it would be ok to put it in types.h
 *    Upgraded it to 2.1.43
 * Francois-Rene Rideau <rideau@ens.fr> 19971012
 *    Upgraded it to 2.1.57
 *    to please Linus T., replaced huge #ifdef's between little/big endian
 *    by nestedly #include'd files.
 *
 * TODO:
 *   = Regular kernel maintainers could also replace all these manual
 *    byteswap macros that remain, disseminated among drivers,
 *    after some grep or the sources...
 *   = Linus might want to rename all these macros and files to fit his taste,
 *    to fit his personal naming scheme.
 *   = it seems that many drivers would also appreciate
 *    nybble swapping support...
 *   = every architecture could add their byteswap macro in asm/byteorder.h
 *    see how some architectures already do (i386, alpha, ppc, etc)
 */

/*
 * This file is included by both <linux/byteorder_little_endian.h> and
 * <linux/byteorder_big_endian.h>. People porting from machines with
 * bizarre bytedisorder (like the VAX?) will have to write a different one.
 * Actually, this file mostly does byteswapping, and could be named
 * <byteswap.h> or <swab.h> rather than <linux/byteorder_generic.h>
 *
 */

/*
 * The following macros are to be defined by <asm/byteorder.h>:
 *
 * Conversion of long and short int between network and host format
 *	ntohl(__u32 x)
 *	ntohs(__u16 x)
 *	htonl(__u32 x)
 *	htons(__u16 x)
 * It seems that some programs (which? where? or perhaps a standard? POSIX?)
 * might like the above to be functions, not macros (why?).
 * if that's true, then detect them, and take measures.
 * Anyway, the measure is: define only ___ntohl as a macro instead,
 * and in a separate file, have
 * unsigned long inline ntohl(x){return ___ntohl(x);}
 *
 * The same for constant arguments
 *	__constant_ntohl(__u32 x)
 *	__constant_ntohs(__u16 x)
 *	__constant_htonl(__u32 x)
 *	__constant_htons(__u16 x)
 *
 * Conversion of XX-bit integers (16- 32- or 64-)
 * between native cpu format and little/big endian format
 * 64-bit stuff only defined for proper architectures
 *	cpu_to_[bl]eXX(__uXX x)
 *	[bl]eXX_to_cpu(__uXX x)
 *
 * The same, but takes a pointer to the value to convert
 *	cpu_to_[bl]eXXp(__uXX x)
 *	[bl]eXX_to_cpup(__uXX x)
 *
 * The same, but change in situ
 *	cpu_to_[bl]eXXs(__uXX x)
 *	[bl]eXX_to_cpus(__uXX x)
 *
 * Byteswapping, independently from cpu endianness
 *	swabXX[ps]?(foo)
 *
 *
 * See asm-foo/byteorder.h for examples of how to provide
 * architecture-optimized versions
 *
 */

#include <asm/types.h>

/*
 * Generic byte swapping routines. We fall back on
 * these if we don't have any optimized code, and
 * when we have constants that we want the compiler
 * to byte swap for us..
 */
#define ___swab16(x) \
	((__u16)( \
		(((__u16)(x) & 0x00ffU) << 8) | \
		(((__u16)(x) & 0xff00U) >> 8) ))
#define ___swab32(x) \
	((__u32)( \
		(((__u32)(x) & (__u32)0x000000ffUL) << 24) | \
		(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) | \
		(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) | \
		(((__u32)(x) & (__u32)0xff000000UL) >> 24) ))
#define ___swab64(x) \
	((__u64)( \
		(__u64)(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) | \
		(__u64)(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) | \
		(__u64)(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) | \
		(__u64)(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) | \
	        (__u64)(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) | \
		(__u64)(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) | \
		(__u64)(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) | \
		(__u64)(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56) ))

/*
 * These do constant folding - this allows the
 * compiler to do any constants at compile
 * time.  Any architecture inline asm optimizations
 * would be pessimizations.
 */
#define __swab16(x) \
	(__builtin_constant_p((__u16)(x)) ? \
	 ___swab16((x)) : __fswab16((x)))
#define __swab32(x) \
	(__builtin_constant_p((__u32)(x)) ? \
	 ___swab32((x)) : __fswab32((x)))
#define __swab64(x) \
	(__builtin_constant_p((__u64)(x)) ? \
	 ___swab64((x)) : __fswab64((x)))


/*
 * provide defaults when no architecture-specific optimization is detected
 */
#ifndef __arch__swab16
#  define __arch__swab16(x) ___swab16(x)
#endif
#ifndef __arch__swab32
#  define __arch__swab32(x) ___swab32(x)
#endif
#ifndef __arch__swab64
#  define __arch__swab64(x) ___swab64(x)
#endif

#ifndef __arch__swab16p
#  define __arch__swab16p(x) __swab16(*(x))
#endif
#ifndef __arch__swab32p
#  define __arch__swab32p(x) __swab32(*(x))
#endif
#ifndef __arch__swab64p
#  define __arch__swab64p(x) __swab64(*(x))
#endif

#ifndef __arch__swab16s
#  define __arch__swab16s(x) *(x) = __swab16p((x))
#endif
#ifndef __arch__swab32s
#  define __arch__swab32s(x) *(x) = __swab32p((x))
#endif
#ifndef __arch__swab64s
#  define __arch__swab64s(x) *(x) = __swab64p((x))
#endif


extern __inline__ __const__ __u16 __fswab16(__u16 x)
{
	return __arch__swab16(x);
}
extern __inline__ __u16 __swab16p(__u16 *x)
{
	return __arch__swab16p(x);
}
extern __inline__ void __swab16s(__u16 *addr)
{
	__arch__swab16s(addr);
}

extern __inline__ __const__ __u32 __fswab32(__u32 x)
{
	return __arch__swab32(x);
}
extern __inline__ __u32 __swab32p(__u32 *x)
{
	return __arch__swab32p(x);
}
extern __inline__ void __swab32s(__u32 *addr)
{
	__arch__swab32s(addr);
}

#ifdef __BYTEORDER_HAS_U64__
extern __inline__ __const__ __u64 __fswab64(__u64 x)
{
#  ifdef __SWAB_64_THRU_32__
	__u32 h = x >> 32;
        __u32 l = x & ((1ULL<<32)-1);
        return (((__u64)__swab32(l)) << 32) | ((__u64)(__swab32(h)));
#  else
	return __arch__swab64(x);
#  endif
}
extern __inline__ __u64 __swab64p(__u64 *x)
{
	return __arch__swab64p(x);
}
extern __inline__ void __swab64s(__u64 *addr)
{
	__arch__swab64s(addr);
}
#endif /* __BYTEORDER_HAS_U64__ */

#if defined(__KERNEL__) || defined(__REQUIRE_CPU_TO_XX)
#define swab16 __swab16
#define swab32 __swab32
#define swab64 __swab64
#define swab16p __swab16p
#define swab32p __swab32p
#define swab64p __swab64p
#define swab16s __swab16s
#define swab32s __swab32s
#define swab64s __swab64s
#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
#define cpu_to_be64 __cpu_to_be64
#define be64_to_cpu __be64_to_cpu
#define cpu_to_be32 __cpu_to_be32
#define be32_to_cpu __be32_to_cpu
#define cpu_to_be16 __cpu_to_be16
#define be16_to_cpu __be16_to_cpu
#define cpu_to_le64p __cpu_to_le64p
#define le64_to_cpup __le64_to_cpup
#define cpu_to_le32p __cpu_to_le32p
#define le32_to_cpup __le32_to_cpup
#define cpu_to_le16p __cpu_to_le16p
#define le16_to_cpup __le16_to_cpup
#define cpu_to_be64p __cpu_to_be64p
#define be64_to_cpup __be64_to_cpup
#define cpu_to_be32p __cpu_to_be32p
#define be32_to_cpup __be32_to_cpup
#define cpu_to_be16p __cpu_to_be16p
#define be16_to_cpup __be16_to_cpup
#define cpu_to_le64s __cpu_to_le64s
#define le64_to_cpus __le64_to_cpus
#define cpu_to_le32s __cpu_to_le32s
#define le32_to_cpus __le32_to_cpus
#define cpu_to_le16s __cpu_to_le16s
#define le16_to_cpus __le16_to_cpus
#define cpu_to_be64s __cpu_to_be64s
#define be64_to_cpus __be64_to_cpus
#define cpu_to_be32s __cpu_to_be32s
#define be32_to_cpus __be32_to_cpus
#define cpu_to_be16s __cpu_to_be16s
#define be16_to_cpus __be16_to_cpus
#endif

/*
 * Handle ntohl and suches. These have various compatibility
 * issues - like we want to give the prototype even though we
 * also have a macro for them in case some strange program
 * wants to take the address of the thing or something..
 *
 * Note that these traditionally return a "long", even though
 * long is often 64-bit these days.. Thus the casts.
 *
 * They have to be macros in order to do the constant folding
 * correctly - if the argument passed into a inline function
 * it is no longer constant according to gcc..
 */

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

/*
 * Do the prototypes. Somebody might want to take the
 * address or some such sick thing..
 */
extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);

#define ___htonl(x) __cpu_to_be32(x)
#define ___htons(x) __cpu_to_be16(x)
#define ___ntohl(x) __be32_to_cpu(x)
#define ___ntohs(x) __be16_to_cpu(x)

#define htonl(x) ((unsigned long)___htonl(x))
#define htons(x) ___htons(x)
#define ntohl(x) ((unsigned long)___ntohl(x))
#define ntohs(x) ___ntohs(x)

#endif /* _LINUX_BYTEORDER_H */
