/*
 *  linux/arch/alpha/lib/memcpy.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This is reasonably optimized for the quad-word-aligned case, which
 * happens with page/buffer copies. It's horribly bad for the unaligned
 * case: it could be made much better, but that would require lots of
 * assembly (unaligned 8-byte load + shift + aligned 4-byte store, for
 * example).
 */

#include <linux/types.h>

static inline void __memcpy_b(unsigned long d, unsigned long s, long n)
{
	while (--n >= 0)
		*(char *) (d++) = *(char *) (s++);
}

static inline void __memcpy_q(unsigned long d, unsigned long s, long n)
{
	/* this first part could be done in one go with ldq_u*2/mask/stq_u */
	while (d & 7) {
		if (--n < 0)
			return;
		*(char *) d = *(char *) s;
		d++;
		s++;
	}
	while ((n -= 8) >= 0) {
		*(unsigned long *) d = *(unsigned long *) s;
		d += 8;
		s += 8;
	}
	/* as could this.. */
	__memcpy_b(d,s,n+8);
}	

static inline void __memcpy_l(unsigned long d, unsigned long s, long n)
{
	while (d & 3) {
		if (--n < 0)
			return;
		*(char *) d = *(char *) s;
		d++;
		s++;
	}
	while ((n -= 4) >= 0) {
		*(unsigned int *) d = *(unsigned int *) s;
		d += 4;
		s += 4;
	}
	__memcpy_b(d,s,n+4);
}	

void * __memcpy(void * dest, const void *src, size_t n)
{
	unsigned long differ;
	differ = ((unsigned long) dest ^ (unsigned long) src) & 7;

	if (!differ) {
		__memcpy_q((unsigned long) dest, (unsigned long) src, n);
		return dest;
	}
	if (differ == 4) {
		__memcpy_l((unsigned long) dest, (unsigned long) src, n);
		return dest;
	}
	__memcpy_b((unsigned long) dest, (unsigned long) src, n);
	return dest;
}
