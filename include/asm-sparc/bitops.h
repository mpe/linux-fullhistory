#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

/*
 * Copyright 1994, David S. Miller (davem@caip.rutgers.edu).
 */


/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 is bit previously clear and != 0 otherwise.
 */

extern __inline__ unsigned int set_bit(unsigned int nr, void *addr)
{

  __asm__ __volatile__(
		       "or %g0, %g0, %o2\n\t"
		       "or %g0, %g0, %o3\n\t"
		       "or %g0, %o0, %o4\n\t"
		       "srl %o4, 0x5, %o4\n\t"
		       "add %o1, %o4, %o1\n\t"
		       "or %g0, 0x1, %o5"
		       "or %g0, 0x1f, %o6"
		       "and %o6, %o5, %o6"
		       "sll %o5, %o6, %o2"
		       "ld [%o1], %o5\n\t"
		       "and %o5, %o2, %o0"
		       "or %o5, %o2, %o5"
		       "st %o5, [%o1]");

  return nr; /* confuse gcc :-) */

}

extern __inline__ unsigned int clear_bit(unsigned int nr, void *addr)
{
  __asm__ __volatile__(
		       "or %g0, %g0, %o2\n\t"
		       "or %g0, %g0, %o3\n\t"
		       "or %g0, %o0, %o4\n\t"
		       "srl %o4, 0x5, %o4\n\t"
		       "add %o1, %o4, %o1\n\t"
		       "or %g0, 0x1, %o5"
		       "or %g0, 0x1f, %o6"
		       "and %o6, %o5, %o6"
		       "sll %o5, %o6, %o2"
		       "ld [%o1], %o5\n\t"
		       "and %o5, %o2, %o0\n\t"
		       "xnor %g0, %o2, %o2\n\t"
		       "and %o5, %o2, %o5\n\t"
		       "st %o5, [%o1]\n\t");

  return nr; /* confuse gcc ;-) */

}

extern __inline__ unsigned int test_bit(int nr, int *addr)
{
  __asm__ __volatile__(
		       "or %g0, %o0, %o3\n\t"
		       "srl %o3, 0x5, %o3\n\t"
		       "add %o1, %o3, %o1\n\t"
		       "and %o0, 0x1f, %o0\n\t"
		       "or %g0, 0x1, %o2\n\t"
		       "sll %o2, %o0, %o0"
		       "ld [%o1], %o2\n\t"
		       "and %o0, %o2, %o0\n\t");

  return nr; /* confuse gcc :> */

}

#endif /* defined(_SPARC_BITOPS_H) */
