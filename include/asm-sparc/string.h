/* string.h: External definitions for optimized assembly string
             routines for the Linux Kernel.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

extern inline size_t strlen(const char * str)
{
  register size_t retval = 0;
  register char tmp = 0;
  register char * lstr;

  lstr = (char *) str;

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
	  "=r" (retval), "=r" (lstr), "=r" (tmp) :
	  "0" (retval), "1" (lstr), "2" (tmp));

  return retval;
}

extern __inline__ int strcmp(const char* str1, const char* str2)
{
  register unsigned int tmp1=0, tmp2=0;
  register int retval=0;

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
	  "=r" (retval), "=r" (str1), "=r" (str2), "=r" (tmp1), "=r" (tmp2) :
	  "0" (retval), "1" (str1), "2" (str2),
	  "3" (tmp1), "4" (tmp2));

  return retval;
}

extern __inline__ int strncmp(const char* str1, const char* str2, size_t strlen)
{
  register int retval=0;

  __asm__("cmp %3, 0x0\n\t"
	  "be 2f\n\t"
	  "ldub [%2], %%g3\n\t"
	  "1: ldub [%1], %%g2\n\t"
	  "sub %%g2, %%g3, %0\n\t"
	  "cmp %0, 0x0\n\t"
	  "bne 2f\n\t"
	  "add %2, 0x1, %2\n\t"
	  "cmp %%g2, 0x0\n\t"
	  "be 2f\n\t"
	  "add %1, 0x1, %1\n\t"
	  "addcc %3, -1, %3\n\t"
	  "bne,a 1b\n\t"
	  "ldub [%2], %%g3\n\t"
	  "2: " :
	  "=r" (retval), "=r" (str1), "=r" (str2), "=r" (strlen) :
	  "0" (retval), "1" (str1), "2" (str2), "3" (strlen) :
	  "%g2", "%g3");

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
	  "=r" (retval), "=r" (source), "=r" (dest), "=r" (tmp) :
	  "0" (retval), "1" (source), "2" (dest), "3" (tmp));

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
	  "=r" (retval), "=r" (dest), "=r" (source), "=r"(tmp), "=r" (cpylen) :
	  "0" (retval), "1" (dest), "2" (source), 
	  "3" (tmp), "4" (cpylen));

  return retval;
}

extern __inline__ char *strcat(char *dest, const char *src)
{
  register char *retval;
  register char temp=0;

  __asm__("or %%g0, %1, %0\n\t"
	  "1: ldub [%1], %3\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne,a 1b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "2: ldub [%2], %3\n\t"
	  "stb %3, [%1]\n\t"
	  "add %1, 0x1, %1\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne 2b\n\t"
	  "add %2, 0x1, %2\n\t" :
	  "=r" (retval), "=r" (dest), "=r" (src), "=r" (temp) :
	  "0" (retval), "1" (dest), "2" (src), "3" (temp));

  return retval;
}

extern __inline__ char *strncat(char *dest, const char *src, size_t len)
{
  register char *retval;
  register char temp=0;

  __asm__("or %%g0, %1, %0\n\t"
	  "1: ldub [%1], %3\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne,a 1b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "2: ldub [%2], %3\n\t"
	  "stb %3, [%1]\n\t"
	  "add %1, 0x1, %1\n\t"
	  "add %3, -1, %3\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne 2b\n\t"
	  "add %2, 0x1, %2\n\t" :
	  "=r" (retval), "=r" (dest), "=r" (src), "=r" (len), "=r" (temp) :
	  "0" (retval), "1" (dest), "2" (src), "3" (len), "4" (temp));

  return retval;
}

extern __inline__ char *strchr(const char *src, int c)
{
  register char temp=0;
  register char *trick=0;

  __asm__("1: ldub [%0], %2\n\t"
	  "cmp %2, %1\n\t"
	  "bne,a 1b\n\t"
	  "add %0, 0x1, %0\n\t"
	  "or %%g0, %0, %3\n\t" :
	  "=r" (src), "=r" (c), "=r" (temp), "=r" (trick), "=r" (src) :
	  "0" (src), "1" (c), "2" (temp), "3" (trick), "4" (src));

  return trick;
}

extern __inline__ char *strpbrk(const char *cs, const char *ct)
{
  register char temp1, temp2;
  register char *scratch;
  register char *trick;

  __asm__("or %%g0, %1, %4\n\t"
	  "1: ldub [%0], %2\n\t"
	  "2: ldub [%1], %3\n\t"
	  "cmp %3, %2\n\t"
	  "be 3f\n\t"
	  "nop\n\t"
	  "cmp %3, 0x0\n\t"
	  "bne 2b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "or %%g0, %4, %1\n\t"
	  "b 1b\n\t"
	  "add %0, 0x1, %0\n\t"
	  "or %%g0, %0, %5\n\t" :
	  "=r" (cs) :
	  "r" (ct), "r" (temp1), "r" (temp2), "r" (scratch), "r" (trick=0),
	  "0" (cs), "1" (ct));

  return trick;

}

      
extern __inline__ size_t strspn(const char *s, const char *accept)
{
  register char temp1, temp2;
  register char* scratch;
  register size_t trick;

  __asm__("or %%g0, %1, %4\n\t"
	  "1: ldub [%0], %2\n\t"
	  "2: ldub [%1], %3\n\t"
	  "cmp %3, 0x0\n\t"
	  "be 3f\n\t"
	  "cmp %3, %2"
	  "bne 2b\n\t"
	  "add %1, 0x1, %1\n\t"
	  "add %0, 0x1, %0\n\t"
	  "b 1b\n\t"
	  "add %5, 0x1, %5\n\t"
	  "3: or %%g0, %0, %4\n\t" :
	  "=r" (s) :
	  "r" (accept), "r" (temp1), "r" (temp2), 
	  "r" (scratch), "r" (trick=0), "0" (s));

  return trick;

}

extern __inline__ char *strtok(char *s, const char *ct)
{
  static char* old; /* frob this kludge for now */
  register char *tok;

  if (s == (char *) 0)
    {
      if (old == (char *) 0)
	{
	  return (char *) 0;
	}
      else
	s = old;
    }

  s += strspn(s, ct);
  if(*s == '\0')
    {
      old = (char *) 0;
      return (char *) 0;
    }

  tok = s;
  s = strpbrk(tok, ct);
  if (s == (char *) 0)
    old = (char *) 0;
  else
    {
      *s = '\0';
      old = s + 1;
    }
  return tok;
}
	

extern __inline__ void *memset(void *src, int c, size_t count)
{
  register void *retval;

  __asm__("or %%g0, %1, %0\n\t"
	  "1: add %1, 0x1, %1\n\t"
	  "2: add %3, -1, %3\n\t"
	  "cmp %3, -1\n\t"
	  "bne,a 1b\n\t"
	  "stb %2, [%1]\n\t" :
	  "=r" (retval), "=r" (src), "=r" (c), "=r" (count) :
	  "0" (retval), "1" (src), "2" (c), "3" (count));

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
	  "=r" (retval), "=r" (dest), "=r" (src), "=r" (count), "=r" (tmp) :
	  "0" (retval), "1" (dest), "2" (src), "3" (count), "4" (tmp));

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
	  "=r" (retval), "=r" (dest), "=r" (src), "=r" (count), "=r" (tmp) :
	  "0" (retval), "1" (dest), "2" (src), "3" (count), "4" (tmp));

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

