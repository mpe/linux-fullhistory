#ifndef _M68K_STRING_H_
#define _M68K_STRING_H_

#define __USE_PORTABLE_STRINGS_H_

extern inline char * strcpy(char * dest,const char *src)
{
  char *xdest = dest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
        "bne 1b"
	: "=a" (dest), "=a" (src)
        : "0" (dest), "1" (src) : "memory");
  return xdest;
}

extern inline char * strncpy(char *dest, const char *src, size_t n)
{
  char *xdest = dest;

  if (n == 0)
    return xdest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
	"beq 2f\n\t"
        "subql #1,%2\n\t"
        "bne 1b\n\t"
        "2:"
        : "=a" (dest), "=a" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "memory");
  return xdest;
}

#define __USE_PORTABLE_strcat

#define __USE_PORTABLE_strncat

extern inline int strcmp(const char * cs,const char * ct)
{
  char __res;

  __asm__
       ("1:\tmoveb %0@+,%2\n\t" /* get *cs */
        "cmpb %1@+,%2\n\t"      /* compare a byte */
        "bne  2f\n\t"           /* not equal, break out */
        "tstb %2\n\t"           /* at end of cs? */
        "bne  1b\n\t"           /* no, keep going */
        "bra  3f\n\t"		/* strings are equal */
        "2:\tsubb %1@-,%2\n\t"  /* *cs - *ct */
        "3:"
        : "=a" (cs), "=a" (ct), "=d" (__res)
        : "0" (cs), "1" (ct));
  return __res;
}

extern inline int strncmp(const char * cs,const char * ct,size_t count)
{
  char __res;

  if (!count)
    return 0;
  __asm__
       ("1:\tmovb %0@+,%3\n\t"          /* get *cs */
        "cmpb   %1@+,%3\n\t"            /* compare a byte */
        "bne    3f\n\t"                 /* not equal, break out */
        "tstb   %3\n\t"                 /* at end of cs? */
        "beq    4f\n\t"                 /* yes, all done */
        "subql  #1,%2\n\t"              /* no, adjust count */
        "bne    1b\n\t"                 /* more to do, keep going */
        "2:\tmoveq #0,%3\n\t"           /* strings are equal */
        "bra    4f\n\t"
        "3:\tsubb %1@-,%3\n\t"          /* *cs - *ct */
        "4:"
        : "=a" (cs), "=a" (ct), "=d" (count), "=d" (__res)
        : "0" (cs), "1" (ct), "2" (count));
  return __res;
}

#define __USE_PORTABLE_strchr

#define __USE_PORTABLE_strlen

#define __USE_PORTABLE_strspn

#define __USE_PORTABLE_strpbrk

#define __USE_PORTABLE_strtok

extern inline void * memset(void * s,char c,size_t count)
{
  void *xs = s;

  if (!count)
    return xs;
  __asm__ __volatile__
       ("1:\tmoveb %3,%0@+\n\t"
	"subql #1,%1\n\t"
        "bne 1b"
	: "=a" (s), "=d" (count)
        : "0" (s), "d" (c), "1" (count)
	: "memory");
  return xs;
}

extern inline void * memcpy(void * to, const void * from, size_t n)
{
  void *xto = to;

  if (!n)
    return xto;
  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
	"subql #1,%2\n\t"
        "bne 1b"
        : "=a" (to), "=a" (from), "=d" (n)
        : "0" (to), "1" (from), "2" (n)
        : "memory" );
  return xto;
}

extern inline void * memmove(void * dest,const void * src, size_t n)
{
  void *xdest = dest;

  if (!n)
    return xdest;

  if (dest < src)
    __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
	"subql #1,%2\n\t"
        "bne 1b"
        : "=a" (dest), "=a" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "memory" );
  else
    __asm__ __volatile__
       ("1:\tmoveb %1@-,%0@-\n\t"
	"subql #1,%2\n\t"
        "bne 1b"
        : "=a" (dest), "=a" (src), "=d" (n)
        : "0" (dest+n), "1" (src+n), "2" (n)
        : "memory" );
  return xdest;
}

#define __USE_PORTABLE_memcmp

#define __USE_PORTABLE_memscan

#endif /* _M68K_STRING_H_ */
