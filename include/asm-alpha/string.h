#ifndef __ALPHA_STRING_H
#define __ALPHA_STRING_H

/* This doesn't actually work that well for unaligned stuff ;-p */
extern inline void * memcpy(void * to, const void * from, size_t n)
{
	const unsigned long * f = from;
	unsigned long * t = to;
	int size = n;

	for (;;) {
		size -= 8;
		if (size < 0)
			return to;
		*(t++) = *(f++);
	}
}

#endif
