/* string.h: External definitions for optimized assembly string
             routines for the Linux Kernel.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

extern __inline__ size_t strlen(const char* str)
{
  register int retval, tmp;

  __asm__("ldub [%1], %2\n\t"
	  "or %%g0, %%g0, %0\n\t"
	  "orcc %2, %%g0, %%g0\n\t"
	  "be 2f\n\t"
	  "add %1, 0x1, %1\n\t"
	  "1: ldub [%1], %2\n\t"
	  "add %0, 0x1, %0\n\t"
	  "orcc %2, %%g0, %%g0\n\t"
	  "bne 1b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "2:" :
	  "=r" (retval) :
	  "r" (str), "r" (tmp=0));

  return retval;
}

extern __inline__ int strcmp(const char* str1, const char* str2)
{
  register unsigned int tmp1, tmp2;
  register int retval;

  __asm__("ldub [%1], %3\n\t"
	  "ldub [%2], %4\n\t"
	  "1: add %2, 0x1, %2\n\t"
	  "cmp %3, %4\n\t"
	  "bne,a 2f\n\t"
	  "sub %2, 0x1, %2\n\t"
	  "ldub [%1], %3\n\t"
	  "add %1, 0x1, %1\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne,a 1b\n\t"
	  "ldub [%2], %4\n\t"
	  "b 3f\n\t"
	  "or %%g0, %%g0, %0\n\t"
	  "2: ldub [%1], %3\n\t"
	  "ldub [%2], %4\n\t"
          "sub %3, %4, %0\n\t"
	  "3: \n\t" :
	  "=r" (retval) :
	  "r" (str1), "r" (str2),
	  "r" (tmp1=0), "r" (tmp2=0));

  return retval;
}

extern __inline__ int strncmp(const char* str1, const char* str2, size_t strlen)
{
  register char tmp1, tmp2;
  register int retval;

  __asm__("cmp %3, 0x0\n\t"
	  "be 2f\n\t"
	  "ldub [%2], %5\n\t"
	  "1: ldub [%1], %4\n\t"
	  "sub %4, %5, %0\n\t"
	  "cmp %0, 0x0\n\t"
	  "bne 2f\n\t"
	  "add %2, 0x1, %2\n\t"
	  "cmp %4, 0x0\n\t"
	  "be 2f\n\t"
	  "add %1, 0x1, %1\n\t"
	  "addcc %3, -1, %3\n\t"
	  "bne,a 1b\n\t"
	  "ldub [%2], %5\n\t"
	  "2: " :
	  "=r" (retval) :
	  "r" (str1), "r" (str2),
	  "r" (strlen), "r" (tmp1=0),
	  "r" (tmp2=0));

  return retval;
}


extern __inline__ char *strcpy(char* dest, const char* source)
{
  register char tmp;
  register char *retval;

  __asm__("or %%g0, %2, %0\n\t"
	  "ldub [%1], %3\n\t"
	  "1: stb %3, [%2]\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne,a 1b\n\t"
	  "ldub [%1], %3\n\t" :
	  "=r" (retval) :
	  "r" (source), "r" (dest), "r" (tmp));

  return retval;
}

extern __inline__ char *strncpy(char *dest, const char *source, size_t cpylen)
{
  register char tmp;
  register char *retval;

  __asm__("or %%g0, %1, %0\n\t"
	  "1: cmp %4, 0x0\n\t"
	  "be 2f\n\t"
	  "ldub [%1], %3\n\t"
	  "add %1, 0x1, %1\n\t"
	  "stb %3, [%2]\n\t"
	  "sub %4, 0x1, %4\n\t"
	  "ba 1\n\t"
	  "add %2, 0x1, %2\n\t" :
	  "=r" (retval) :
	  "r" (dest), "r" (source), 
	  "r" (tmp), "r" (cpylen));

  return retval;
}

/* extern __inline__ char *strcat(char *dest, char *src); */

/* extern __inline__ char *strncat(char *dest, char *src, int len); */

/* extern __inline__ char *strchr(char *src, char c); */

/* extern __inline__ char *strpbrk(char *cs, char *ct); */

/* extern __inline__ char *strtok(char *s, char *ct); */

/* extern __inline__ int strspn(char *s, char *accept); */

extern __inline__ void *memset(void *src, char c, size_t count)
{
  register void *retval;

  __asm__("or %%g0, %1, %0\n\t"
	  "1: add %1, 0x1, %1\n\t"
	  "2: add %3, -1, %3\n\t"
	  "cmp %3, -1\n\t"
	  "bne,a 1b\n\t"
	  "stb %2, [%1]\n\t" :
	  "=r" (retval) :
	  "r" (src), "r" (c), "r" (count));

  return retval;
}

extern __inline__ void *memcpy(void *dest, const void *src, size_t count)
{
  register void *retval;
  register char tmp;

  __asm__("or %%g0, %1, %0\n\t"
	  "add %3, -1, %3\n\t"
	  "cmp %3, -1\n\t"
	  "be 2f\n\t"
	  "1: ldub [%2], %4\n\t"
	  "add %2, 0x1, %2\n\t"
	  "add %3, -1, %3\n\t"
	  "cmp %3, -1\n\t"
	  "stb %4, [%1]\n\t"
	  "bne 1b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "2: " :
	  "=r" (retval) :
	  "r" (dest), "r" (src), "r" (count), "r" (tmp));

  return retval;
}

extern __inline__ void *memmove(void *dest, const void *src, size_t count)
{
  register void *retval;
  register char tmp;

  __asm__("or %%g0, %1, %1\n\t"
	  "add %3, -1, %3\n\t"
	  "cmp %3, -1\n\t"
	  "be 2f\n\t"
	  "1: ldub [%2], %4\n\t"
	  "add %2, 0x1, %2\n\t"
	  "add %3, -1, %3\n\t"
	  "cmp %3, -1\n\t"
	  "stb %4, [%1]\n\t"
	  "bne 1b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "2: " :
	  "=r" (retval) :
	  "r" (dest), "r" (src), "r" (count), "r" (tmp));

  return retval;
}

extern __inline__ int memcmp(const void *cs, const void *ct, size_t count)
{
  register int retval;
  register unsigned long tmp1, tmp2;

  __asm__("or %%g0, %1, %0\n\t"
	  "cmp %3, 0x0\n\t"
	  "ble,a 3f\n\t"
	  "or %%g0, %%g0, %0\n\t"
	  "1: ldub [%1], %4\n\t"
	  "ldub [%2], %5\n\t"
	  "cmp %4, %5\n\t"
	  "be,a 2f\n\t"
	  "add %1, 0x1, %1\n\t"
	  "bgeu 3f\n\t"
	  "or %%g0, 0x1, %0\n\t"
	  "b 3f\n\t"
	  "or %%g0, -1, %0\n\t"
	  "2: add %3, -1, %3\n\t"
	  "cmp %3, 0x0\n\t"
	  "bg 1b\n\t"
	  "add %2, 0x1, %2\n\t"
	  "or %%g0, %%g0, %0\n\t"
	  "3: " :
	  "=r" (retval) :
	  "r" (cs), "r" (ct), "r" (count), "r" (tmp1=0), "r" (tmp2=0));

  return retval;
}

