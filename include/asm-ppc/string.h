#ifndef _PPC_STRING_H_
#define _PPC_STRING_H_



/*
 * keep things happy, the compile became unhappy since memset is
 * in include/linux/string.h and lib/string.c with different prototypes
 *                          -- Cort
 */
#if 1
#define  __HAVE_ARCH_MEMSET
extern __inline__ void * memset(void * s,int c,__kernel_size_t count)
{
	char *xs = (char *) s;

	while (count--)
		*xs++ = c;

	return s;
}
#endif
#define bzero(addr,size) memset((addr),(int)(0),(size))


#endif
