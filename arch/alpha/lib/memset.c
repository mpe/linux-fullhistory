/*
 *  linux/arch/alpha/lib/memset.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * These are only slightly optimized so far..
 */

#include <linux/types.h>

void * __constant_c_memset(void * s, unsigned long c, long count)
{
	unsigned long xs = (unsigned long) s;

	/*
	 * the first and last parts could be done with just one
	 * unaligned load/store, but I don't want to think about it
	 */
	while (count > 0 && (xs & 7)) {
		*(char *) xs = c;
		count--; xs++;
	}
	while (count > 7) {
		*(unsigned long *) xs = c;
		count -=8; xs += 8;
	}
	while (count > 0) {
		*(char *) xs = c;
		count--; xs++;
	}
	return s;
}

void * __memset(void * s,char c,size_t count)
{
	char *xs = (char *) s;

	while (count--)
		*xs++ = c;

	return s;
}
