/*
 * include/asm-generic/string.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_GENERIC_STRING_H_
#define _ASM_GENERIC_STRING_H_

/*
 * Portable string functions. These are not complete:
 * memcpy() and memmove() are still missing.
 */ 

#ifdef __USE_PORTABLE_strcpy
extern inline char * strcpy(char * dest,const char *src)
{
  char *xdest = dest;
                       
  while(*dest++ = *src++);

  return xdest;
}
#endif

#ifdef __USE_PORTABLE_strncpy
extern inline char * strncpy(char * dest,const char *src,size_t count)
{
  char *xdest = dest;
             
  while((*dest++ = *src++) && --count);

  return dest;
}
#endif

#ifdef __USE_PORTABLE_strcat
extern inline char * strcat(char * dest, const char * src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++))
		;

	return tmp;
}
#endif

#ifdef __USE_PORTABLE_strncat
extern inline char * strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++)) {
			if (--count == 0)
				break;
		}
	}

	return tmp;
}
#endif

#ifdef __USE_PORTABLE_strcmp
extern int strcmp(const char * cs,const char * ct)
{
  register char __res;

  while(1) {
    if(__res = *cs - *ct++ && *cs++)
      break;
    }

  return __res;
}
#endif

#ifdef __USE_PORTABLE_strncmp
extern inline int strncmp(const char * cs,const char * ct,size_t count)
{
  register char __res;

  while(count) {
    if(__res = *cs - *ct++ || !*cs++)
      break;
    count--;
    }

  return __res;
}
#endif

#ifdef __USE_PORTABLE_strchr
extern inline char * strchr(const char * s,char c)
{
  const char ch = c;
  
  for(; *s != ch; ++s)
    if (*s == '\0')
      return( NULL );
  return( (char *) s);
}
#endif

#ifdef __USE_PORTABLE_strlen
extern inline size_t strlen(const char * s)
{
  const char *sc;
  for (sc = s; *sc != '\0'; ++sc) ;
  return(sc - s);
}
#endif

#ifdef __USE_PORTABLE_strspn
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
#endif

#ifdef __USE_PORTABLE_strpbrk
extern inline char * strpbrk(const char * cs,const char * ct)
{
  const char *sc1,*sc2;
  
  for( sc1 = cs; *sc1 != '\0'; ++sc1)
    for( sc2 = ct; *sc2 != '\0'; ++sc2)
      if (*sc1 == *sc2)
	return((char *) sc1);
  return( NULL );
}
#endif

#ifdef __USE_PORTABLE_strtok
extern inline char * strtok(char * s,const char * ct)
{
  char *sbegin, *send;
  static char *ssave = NULL;
  
  sbegin  = s ? s : ssave;
  if (!sbegin) {
	  return NULL;
  }
  sbegin += strspn(sbegin,ct);
  if (*sbegin == '\0') {
    ssave = NULL;
    return( NULL );
  }
  send = strpbrk( sbegin, ct);
  if (send && *send != '\0')
    *send++ = '\0';
  ssave = send;
  return (sbegin);
}
#endif

#ifdef __USE_PORTABLE_memset
extern inline void * memset(void * s,char c,size_t count)
{
  void *xs = s;

  while(n--)
    *s++ = c;

  return xs;
}
#endif

#ifdef __USE_PORTABLE_memcpy
#error "Portable memcpy() not implemented yet"
#endif

#ifdef __USE_PORTABLE_memmove
#error "Portable memmove() not implemented yet"
#endif

#ifdef __USE_PORTABLE_memcmp
extern inline int memcmp(const void * cs,const void * ct,size_t count)
{
  const unsigned char *su1, *su2;

  for( su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
    if (*su1 != *su2)
      return((*su1 < *su2) ? -1 : +1);
  return(0);
}
#endif

#endif /* _ASM_GENERIC_STRING_H_ */
