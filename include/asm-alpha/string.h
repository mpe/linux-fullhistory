#ifndef __ALPHA_STRING_H
#define __ALPHA_STRING_H

extern void * __constant_c_memset(void *, unsigned long, long);
extern void * __memset(void *, char, size_t);
extern void * __memcpy(void *, const void *, size_t);

#define __HAVE_ARCH_MEMSET
#define memset(s, c, count) \
(__builtin_constant_p(c) ? \
 __constant_c_memset((s),(0x0101010101010101UL*(unsigned char)c),(count)) : \
 __memset((s),(c),(count)))

#define __HAVE_ARCH_STRLEN

#endif
