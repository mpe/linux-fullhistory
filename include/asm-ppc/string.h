#ifndef _PPC_STRING_H_
#define _PPC_STRING_H_

#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRNCPY
#define __HAVE_ARCH_STRLEN
#define __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRCAT
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_BCOPY
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_MEMCHR
/*#define bzero(addr,size) memset((addr),(int)(0),(size))*/
extern inline void * memchr(const void * cs,int c,size_t count)
{
	unsigned long i = 0;
	while ( count != i )
	{
		if ( (char)c == *(char *)(cs + i) )
			return (void *)(cs + i);
		i--;
	}
	return NULL;
}
#endif
