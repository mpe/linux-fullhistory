#ifndef _M68K_STRING_H_
#define _M68K_STRING_H_

#define __HAVE_ARCH_STRCPY
extern inline char * strcpy(char * dest,const char *src)
{
  char *xdest = dest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
        "jne 1b"
	: "=a" (dest), "=a" (src)
        : "0" (dest), "1" (src) : "memory");
  return xdest;
}

#define __HAVE_ARCH_STRNCPY
extern inline char * strncpy(char *dest, const char *src, size_t n)
{
  char *xdest = dest;

  if (n == 0)
    return xdest;

  __asm__ __volatile__
       ("1:\tmoveb %1@+,%0@+\n\t"
	"jeq 2f\n\t"
        "subql #1,%2\n\t"
        "jne 1b\n\t"
        "2:"
        : "=a" (dest), "=a" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "memory");
  return xdest;
}

#define __HAVE_ARCH_STRCAT
extern inline char * strcat(char * dest, const char * src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++))
		;

	return tmp;
}

#define __HAVE_ARCH_STRNCAT
extern inline char * strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++)) {
			if (--count == 0) {
				*dest++='\0';
				break;
			}
		}
	}

	return tmp;
}

#define __HAVE_ARCH_STRCHR
extern inline char * strchr(const char * s, int c)
{
  const char ch = c;
  
  for(; *s != ch; ++s)
    if (*s == '\0')
      return( NULL );
  return( (char *) s);
}

#define __HAVE_ARCH_STRPBRK
extern inline char * strpbrk(const char * cs,const char * ct)
{
  const char *sc1,*sc2;
  
  for( sc1 = cs; *sc1 != '\0'; ++sc1)
    for( sc2 = ct; *sc2 != '\0'; ++sc2)
      if (*sc1 == *sc2)
	return((char *) sc1);
  return( NULL );
}

#define __HAVE_ARCH_STRSPN
extern inline size_t strspn(const char *s, const char *accept)
{
  const char *p;
  const char *a;
  size_t count = 0;

  for (p = s; *p != '\0'; ++p)
    {
      for (a = accept; *a != '\0'; ++a)
        if (*p == *a)
          break;
      if (*a == '\0')
        return count;
      else
        ++count;
    }

  return count;
}

#define __HAVE_ARCH_STRTOK
extern inline char * strtok(char * s,const char * ct)
{
  char *sbegin, *send;
  
  sbegin  = s ? s : ___strtok;
  if (!sbegin) {
	  return NULL;
  }
  sbegin += strspn(sbegin,ct);
  if (*sbegin == '\0') {
    ___strtok = NULL;
    return( NULL );
  }
  send = strpbrk( sbegin, ct);
  if (send && *send != '\0')
    *send++ = '\0';
  ___strtok = send;
  return (sbegin);
}

/* strstr !! */

#define __HAVE_ARCH_STRLEN
extern inline size_t strlen(const char * s)
{
  const char *sc;
  for (sc = s; *sc != '\0'; ++sc) ;
  return(sc - s);
}

/* strnlen !! */

#define __HAVE_ARCH_STRCMP
extern inline int strcmp(const char * cs,const char * ct)
{
  char __res;

  __asm__
       ("1:\tmoveb %0@+,%2\n\t" /* get *cs */
        "cmpb %1@+,%2\n\t"      /* compare a byte */
        "jne  2f\n\t"           /* not equal, break out */
        "tstb %2\n\t"           /* at end of cs? */
        "jne  1b\n\t"           /* no, keep going */
        "jra  3f\n\t"		/* strings are equal */
        "2:\tsubb %1@-,%2\n\t"  /* *cs - *ct */
        "3:"
        : "=a" (cs), "=a" (ct), "=d" (__res)
        : "0" (cs), "1" (ct));
  return __res;
}

#define __HAVE_ARCH_STRNCMP
extern inline int strncmp(const char * cs,const char * ct,size_t count)
{
  char __res;

  if (!count)
    return 0;
  __asm__
       ("1:\tmovb %0@+,%3\n\t"          /* get *cs */
        "cmpb   %1@+,%3\n\t"            /* compare a byte */
        "jne    3f\n\t"                 /* not equal, break out */
        "tstb   %3\n\t"                 /* at end of cs? */
        "jeq    4f\n\t"                 /* yes, all done */
        "subql  #1,%2\n\t"              /* no, adjust count */
        "jne    1b\n\t"                 /* more to do, keep going */
        "2:\tmoveq #0,%3\n\t"           /* strings are equal */
        "jra    4f\n\t"
        "3:\tsubb %1@-,%3\n\t"          /* *cs - *ct */
        "4:"
        : "=a" (cs), "=a" (ct), "=d" (count), "=d" (__res)
        : "0" (cs), "1" (ct), "2" (count));
  return __res;
}

#define __HAVE_ARCH_MEMSET
extern inline void * memset(void * s,int c,size_t count)
{
  void *xs = s;
  size_t temp;

  if (!count)
    return xs;
  c &= 0xff;
  if ((long) s & 1)
    {
      char *cs = s;
      *cs++ = c;
      s = cs;
      count--;
    }
  c |= c << 8;
  if (count > 2 && (long) s & 2)
    {
      short *ss = s;
      *ss++ = c;
      s = ss;
      count -= 2;
    }
  temp = count >> 2;
  if (temp)
    {
      long *ls = s;
      c |= c << 16;
      temp--;
      do
	*ls++ = c;
      while (temp--);
      s = ls;
    }
  if (count & 2)
    {
      short *ss = s;
      *ss++ = c;
      s = ss;
    }
  if (count & 1)
    {
      char *cs = s;
      *cs = c;
    }
  return xs;
}

#define __HAVE_ARCH_MEMCPY
#define memcpy(to, from, n) \
(__builtin_constant_p(n) ? \
 __builtin_memcpy((to),(from),(n)) : \
 memcpy((to),(from),(n)))

#define __HAVE_ARCH_MEMMOVE
extern inline void * memmove(void * dest,const void * src, size_t n)
{
  void *xdest = dest;
  size_t temp;

  if (!n)
    return xdest;

  if (dest < src)
    {
      if ((long) dest & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *cdest++ = *csrc++;
	  dest = cdest;
	  src = csrc;
	  n--;
	}
      if (n > 2 && (long) dest & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *sdest++ = *ssrc++;
	  dest = sdest;
	  src = ssrc;
	  n -= 2;
	}
      temp = n >> 2;
      if (temp)
	{
	  long *ldest = dest;
	  const long *lsrc = src;
	  temp--;
	  do
	    *ldest++ = *lsrc++;
	  while (temp--);
	  dest = ldest;
	  src = lsrc;
	}
      if (n & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *sdest++ = *ssrc++;
	  dest = sdest;
	  src = ssrc;
	}
      if (n & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *cdest = *csrc;
	}
    }
  else
    {
      dest = (char *) dest + n;
      src = (const char *) src + n;
      if ((long) dest & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *--cdest = *--csrc;
	  dest = cdest;
	  src = csrc;
	  n--;
	}
      if (n > 2 && (long) dest & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *--sdest = *--ssrc;
	  dest = sdest;
	  src = ssrc;
	  n -= 2;
	}
      temp = n >> 2;
      if (temp)
	{
	  long *ldest = dest;
	  const long *lsrc = src;
	  temp--;
	  do
	    *--ldest = *--lsrc;
	  while (temp--);
	  dest = ldest;
	  src = lsrc;
	}
      if (n & 2)
	{
	  short *sdest = dest;
	  const short *ssrc = src;
	  *--sdest = *--ssrc;
	  dest = sdest;
	  src = ssrc;
	}
      if (n & 1)
	{
	  char *cdest = dest;
	  const char *csrc = src;
	  *--cdest = *--csrc;
	}
    }
  return xdest;
}

#define __HAVE_ARCH_MEMCMP
#define memcmp(cs, ct, n) \
(__builtin_constant_p(n) ? \
 __builtin_memcmp((cs),(ct),(n)) : \
 memcmp((cs),(ct),(n)))

#endif /* _M68K_STRING_H_ */
