#ifndef __ASM_ARM_STRING_H
#define __ASM_ARM_STRING_H

/*
 * inline versions, hmm...
 */

#define __HAVE_ARCH_STRRCHR
extern char * strrchr(const char * s, int c);

#define __HAVE_ARCH_STRCHR
extern char * strchr(const char * s, int c);

#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMSET

#define __HAVE_ARCH_MEMZERO
extern void memzero(void *ptr, int n);

extern void memsetl (unsigned long *, unsigned long, int n);

#endif
 
