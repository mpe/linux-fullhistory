#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

/*
 * Copyright 1994, David S. Miller (davem@caip.rutgers.edu).
 */


/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */

/* For now, the sun4c implementation will disable and enable traps
 * in order to insure atomicity. Things will have to be different
 * for sun4m (ie. SMP) no doubt.
 */

extern __inline__ unsigned int set_bit(unsigned int nr, void *addr)
{
  register unsigned long retval, tmp, mask, psr;

  __asm__ __volatile__("or %%g0, 0x1, %3\n\t"     /* produce the mask */
		       "sll %3, %4, %3\n\t"
		       "rd %%psr, %5\n\t"         /* read the psr */
		       "wr %5, 0x20, %%psr\n\t"   /* traps disabled */
		       "ld [%1], %2\n\t"          /* critical section */
		       "and %3, %2, %0\n\t"
		       "or  %3, %2, %2\n\t"
		       "st  %2, [%1]\n\t"
		       "wr %5, 0x0, %%psr\n\t" :  /* re-enable traps */
                       "=r" (retval) :
                       "r" (addr), "r" (tmp=0), "r" (mask=0),
                       "r" (nr), "r" (psr=0));

  return retval; /* confuse gcc :-) */

}

extern __inline__ unsigned int clear_bit(unsigned int nr, void *addr)
{
  register unsigned long retval, tmp, mask, psr;

  __asm__ __volatile__("or %%g0, 0x1, %3\n\t"
		       "sll %3, %4, %3\n\t"
		       "rd %%psr, %5\n\t"
		       "wr %5, 0x20, %%psr\n\t"   /* disable traps */
                       "ld [%1], %2\n\t"
		       "and %2, %3, %0\n\t"       /* get old bit */
		       "andn %2, %3, %2\n\t"      /* set new val */
		       "st  %2, [%1]\n\t"
		       "wr %5, 0x0, %%psr\n\t" :  /* enable traps */
		       "=r" (retval) :
		       "r" (addr), "r" (tmp=0), "r" (mask=0),
		       "r" (nr), "r" (psr=0));

  return retval; /* confuse gcc ;-) */

}

extern __inline__ unsigned int change_bit(unsigned int nr, void *addr)
{
  register unsigned long retval, tmp, mask, psr;

  __asm__ __volatile__("or %%g0, 0x1, %3\n\t"
		       "sll %3, %4, %3\n\t"
		       "rd %%psr, %5\n\t"
		       "wr %5, 0x20, %%psr\n\t"   /* disable traps */
                       "ld [%1], %2\n\t"
		       "and %3, %2, %0\n\t"       /* get old bit val */
		       "xor %3, %2, %2\n\t"       /* set new val */
		       "st  %2, [%1]\n\t"
		       "wr %5, 0x0, %%psr\n\t" :  /* enable traps */
		       "=r" (retval) :
		       "r" (addr), "r" (tmp=0), "r" (mask=0),
		       "r" (nr), "r" (psr=0));

  return retval; /* confuse gcc ;-) */

}

/* The following routine need not be atomic. */

extern __inline__ unsigned int test_bit(int nr, void *addr)
{
  register unsigned long retval, tmp;

  __asm__ __volatile__("ld [%1], %2\n\t"
		       "or %%g0, 0x1, %0\n\t"
		       "sll %0, %3, %0\n\t"
		       "and %0, %2, %0\n\t" :
		       "=r" (retval) :
		       "r" (addr), "r" (tmp=0),
		       "r" (nr));

  return retval; /* confuse gcc :> */

}

/* There has to be a faster way to do this, sigh... */

extern __inline__ unsigned long ffz(unsigned long word)
{
  register unsigned long cnt, tmp, tmp2;

  cnt = 0;

  __asm__("or %%g0, %3, %2\n\t"
	  "1: and %2, 0x1, %1\n\t"
	  "srl %2, 0x1, %2\n\t"
	  "cmp %1, 0\n\t"
	  "bne,a 1b\n\t"
	  "add %0, 0x1, %0\n\t" :
	  "=r" (cnt) :
	  "r" (tmp=0), "r" (tmp2=0), "r" (word));

  return cnt;
}

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern __inline__ unsigned long find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
  unsigned long *p = ((unsigned long *) addr) + (offset >> 6);
  unsigned long result = offset & ~31UL;
  unsigned long tmp;

  if (offset >= size)
    return size;
  size -= result;
  offset &= 31UL;
  if (offset) 
    {
      tmp = *(p++);
      tmp |= ~0UL >> (32-offset);
      if (size < 32)
	goto found_first;
      if (~tmp)
	goto found_middle;
      size -= 32;
      result += 32;
    }
  while (size & ~32UL) 
    {
      if (~(tmp = *(p++)))
	goto found_middle;
      result += 32;
      size -= 32;
    }
  if (!size)
    return result;
  tmp = *p;

found_first:
  tmp |= ~0UL << size;
found_middle:
  return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)


#endif /* defined(_SPARC_BITOPS_H) */

