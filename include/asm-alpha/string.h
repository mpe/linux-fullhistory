#ifndef __ALPHA_STRING_H__
#define __ALPHA_STRING_H__

#ifdef __KERNEL__

/*
 * GCC of any recent vintage doesn't do stupid things with bcopy.  Of
 * EGCS-devel vintage, it knows all about expanding memcpy inline.
 * For things other than EGCS-devel but still recent, GCC will expand
 * __builtin_memcpy as a simple call to memcpy.
 *
 * Similarly for a memset with data = 0.
 */

#define __HAVE_ARCH_MEMCPY
/* For backward compatibility with modules.  Unused otherwise.  */
extern void * __memcpy(void *, const void *, size_t);

#if __GNUC__ > 2 || __GNUC_MINOR__ >= 8
#define memcpy __builtin_memcpy
#endif

#define __HAVE_ARCH_MEMSET
extern void * __constant_c_memset(void *, unsigned long, long);
extern void * __memset(void *, char, size_t);

#if __GNUC__ > 2 || __GNUC_MINOR__ >= 8
#define memset(s, c, n)							    \
(__builtin_constant_p(c)						    \
 ? (__builtin_constant_p(n) && (c) == 0					    \
    ? __builtin_memset((s),0,(n)) 					    \
    : __constant_c_memset((s),0x0101010101010101UL*(unsigned char)(c),(n))) \
 : __memset((s),(c),(n)))
#else
#define memset(s, c, n)							\
(__builtin_constant_p(c)						\
 ? __constant_c_memset((s),0x0101010101010101UL*(unsigned char)(c),(n)) \
 : __memset((s),(c),(n)))
#endif

#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRNCPY
#define __HAVE_ARCH_STRCAT
#define __HAVE_ARCH_STRNCAT
#define __HAVE_ARCH_STRCHR
#define __HAVE_ARCH_STRRCHR
#define __HAVE_ARCH_STRLEN

#endif /* __KERNEL__ */

#endif /* __ALPHA_STRING_H__ */
