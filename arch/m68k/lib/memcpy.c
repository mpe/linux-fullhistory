#include <linux/types.h>

void * memcpy(void * to, const void * from, size_t n)
{
  void *xto = to;
  size_t temp;

  if (!n)
    return xto;
  if ((long) to & 1)
    {
      char *cto = to;
      const char *cfrom = from;
      *cto++ = *cfrom++;
      to = cto;
      from = cfrom;
      n--;
    }
  if (n > 2 && (long) to & 2)
    {
      short *sto = to;
      const short *sfrom = from;
      *sto++ = *sfrom++;
      to = sto;
      from = sfrom;
      n -= 2;
    }
  temp = n >> 2;
  if (temp)
    {
      long *lto = to;
      const long *lfrom = from;
      temp--;
      do
	*lto++ = *lfrom++;
      while (temp--);
      to = lto;
      from = lfrom;
    }
  if (n & 2)
    {
      short *sto = to;
      const short *sfrom = from;
      *sto++ = *sfrom++;
      to = sto;
      from = sfrom;
    }
  if (n & 1)
    {
      char *cto = to;
      const char *cfrom = from;
      *cto = *cfrom;
    }
  return xto;
}
