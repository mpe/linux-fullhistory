#ifndef __ALPHA_STRING_H
#define __ALPHA_STRING_H

extern void * __constant_c_memset(void *, unsigned long, long);
extern void * __memset(void *, char, size_t);

#define memset(s, c, count) \
(__builtin_constant_p(c) ? \
 __constant_c_memset((s),(0x01010101UL*(unsigned char)c),(count)) : \
 __memset((s),(c),(count)))

#endif
