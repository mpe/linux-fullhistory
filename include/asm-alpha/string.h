#ifndef __ALPHA_STRING_H__
#define __ALPHA_STRING_H__

#ifdef __KERNEL__

extern void * __constant_c_memset(void *, unsigned long, long);
extern void * __memset(void *, char, size_t);

/*
 * Ugh. Gcc uses "bcopy()" internally for structure assignments.
 */
#define __HAVE_ARCH_BCOPY

/*
 * Define "memcpy()" to something else, otherwise gcc will
 * corrupt that too into a "bcopy".  Also, some day we might
 * want to do a separate inlined constant-size memcpy (for 8
 * and 16 byte user<->kernel structure copying).
 */
#define __HAVE_ARCH_MEMCPY
extern void * __memcpy(void *, const void *, size_t);
#define memcpy __memcpy

#define __HAVE_ARCH_MEMSET
#define memset(s, c, count) \
(__builtin_constant_p(c) ? \
 __constant_c_memset((s),(0x0101010101010101UL*(unsigned char)c),(count)) : \
 __memset((s),(c),(count)))

#define __HAVE_ARCH_STRLEN

#endif /* __KERNEL__ */

#endif /* __ALPHA_STRING_H__ */
