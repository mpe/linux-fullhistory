/* string.h: External definitions for optimized assembly string
 *           routines for the Linux Kernel.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_STRING_H
#define _SPARC_STRING_H

extern __inline__ size_t strlen(const char * str)
{
  const char *sc;

  for (sc = str; *sc != '\0'; ++sc)
    /* nothing */;
  return sc - str;
}

extern __inline__ int strcmp(const char* str1, const char* str2)
{
  register signed char __res;

  while (1) {
    if ((__res = *str1 - *str2++) != 0 || !*str1++)
      break;
  }

  return __res;
}

extern __inline__ int strncmp(const char* str1, const char* str2, size_t strlen)
{
  register signed char __res = 0;

  while (strlen) {
    if ((__res = *str1 - *str2++) != 0 || !*str1++)
      break;
    strlen--;
  }

  return __res;
}


extern __inline__ char *strcpy(char* dest, const char* source)
{
  char *tmp = dest;

  while ((*dest++ = *source++) != '\0')
    /* nothing */;
  return tmp;
}

extern __inline__ char *strncpy(char *dest, const char *source, size_t cpylen)
{
  char *tmp = dest;

  while (cpylen-- && (*dest++ = *source++) != '\0')
    /* nothing */;

  return tmp;
}

extern __inline__ char *strcat(char *dest, const char *src)
{
  char *tmp = dest;

  while (*dest)
    dest++;
  while ((*dest++ = *src++) != '\0')
    ;

  return tmp;
}

extern __inline__ char *strncat(char *dest, const char *src, size_t len)
{
  char *tmp = dest;

  if (len) {
    while (*dest)
      dest++;
    while ((*dest++ = *src++)) {
      if (--len == 0)
	break;
    }
  }

  return tmp;
}

extern __inline__ char *strchr(const char *src, int c)
{
  for(; *src != c; ++src)
    if (*src == '\0')
      return NULL;
  return (char *) src;
}

extern __inline__ char *strpbrk(const char *cs, const char *ct)
{
  const char *sc1,*sc2;

  for( sc1 = cs; *sc1 != '\0'; ++sc1) {
    for( sc2 = ct; *sc2 != '\0'; ++sc2) {
      if (*sc1 == *sc2)
	return (char *) sc1;
    }
  }
  return NULL;
}

      
extern __inline__ size_t strspn(const char *s, const char *accept)
{
  const char *p;
  const char *a;
  size_t count = 0;

  for (p = s; *p != '\0'; ++p) {
    for (a = accept; *a != '\0'; ++a) {
      if (*p == *a)
	break;
    }
    if (*a == '\0')
      return count;
    ++count;
  }

  return count;
}

extern __inline__ char *strtok(char *s, const char *ct)
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
	

extern __inline__ void *memset(void *src, int c, size_t count)
{
  char *xs = (char *) src;

  while (count--)
    *xs++ = c;

  return src;
}

extern __inline__ void *memcpy(void *dest, const void *src, size_t count)
{
  char *tmp = (char *) dest, *s = (char *) src;

  while (count--)
    *tmp++ = *s++;

  return dest;
}

extern __inline__ void *memmove(void *dest, const void *src, size_t count)
{
  char *tmp, *s;

  if (dest <= src) {
    tmp = (char *) dest;
    s = (char *) src;
    while (count--)
      *tmp++ = *s++;
  }
  else {
    tmp = (char *) dest + count;
    s = (char *) src + count;
    while (count--)
      *--tmp = *--s;
  }

  return dest;
}

extern __inline__ int memcmp(const void *cs, const void *ct, size_t count)
{
  const unsigned char *su1, *su2;
  signed char res = 0;

  for( su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
    if ((res = *su1 - *su2) != 0)
      break;
  return res;
}

#endif /* !(_SPARC_STRING_H) */
