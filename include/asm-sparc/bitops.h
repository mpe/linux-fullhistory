#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

#include <linux/kernel.h>
#include <asm/system.h>

/*
 * Copyright 1995, David S. Miller (davem@caip.rutgers.edu).
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

/* These routines now do things in little endian byte order. */

/* Our unsigned long accesses on the Sparc look like this:
 * Big Endian:
 *    byte 0    byte 1      byte 2    byte 3
 *  0000 0000  0000 0000  0000 0000  0000 0000
 *  31     24  23     16  15      8  7       0
 *
 * We want to set the bits in a little-endian fashion:
 * Little Endian:
 *    byte 3    byte 2      byte 1    byte 0
 *  0000 0000  0000 0000  0000 0000  0000 0000
 *  31     24  23     16  15      8  7       0
 */

/* #define __LITTLE_ENDIAN_BITOPS */

extern __inline__ unsigned int set_bit(unsigned int nr, void *vaddr)
{


#ifdef __LITTLE_ENDIAN_BITOPS


        int retval;
        unsigned char *addr = (unsigned char *)vaddr;
	unsigned char mask;
#ifndef TEST_BITOPS
        unsigned long flags;
#endif

        addr += nr >> 3;
        mask = 1 << (nr & 0x7);

#ifndef TEST_BITOPS
	save_flags(flags);
	cli();
#endif

        retval = (mask & *addr) != 0;
        *addr |= mask;

#ifndef TEST_BITOPS
	restore_flags(flags);
#endif

        return retval;

#else  /* BIG ENDIAN BITOPS */


	int retval;
	unsigned long *addr = vaddr;
	unsigned long mask;
#ifndef TEST_BITOPS
	unsigned long flags;
#endif

	addr += nr>>5;
	mask = 1 << (nr&31);

#ifndef TEST_BITOPS
	save_flags(flags);
	cli();
#endif

	retval = (mask & *addr) != 0;
	*addr |= mask;

#ifndef TEST_BITOPS
	restore_flags(flags);
#endif

	return retval;


#endif
}

extern __inline__ unsigned int clear_bit(unsigned int nr, void *vaddr)
{
#ifdef __LITTLE_ENDIAN_BITOPS


        int retval;
        unsigned char *addr = (unsigned char *)vaddr;
	unsigned char mask;
#ifndef TEST_BITOPS
        unsigned long flags;
#endif

        addr += nr >> 3;
        mask = 1 << (nr & 7);

#ifndef TEST_BITOPS
	save_flags(flags);
	cli();
#endif

        retval = (mask & *addr) != 0;
        *addr &= ~mask;

#ifndef TEST_BITOPS
	restore_flags(flags);
#endif

        return retval;


#else   /* BIG ENDIAN BITOPS */


	int retval;
	unsigned long *addr = vaddr;
	unsigned long mask;
#ifndef TEST_BITOPS
	unsigned long flags;
#endif

	addr += nr>>5;
	mask = 1 << (nr&31);

#ifndef TEST_BITOPS
	save_flags(flags);
	cli();
#endif

	retval = (mask & *addr) != 0;
	*addr &= ~mask;

#ifndef TEST_BITOPS
	restore_flags(flags);
#endif

	return retval;


#endif
}

extern __inline__ unsigned int change_bit(unsigned int nr, void *vaddr)
{
#ifdef __LITTLE_ENDIAN_BITOPS


        int retval;
        unsigned char *addr = (unsigned char *)vaddr;
	unsigned char mask;
#ifndef TEST_BITOPS
        unsigned long flags;
#endif

        addr += nr >> 3;
        mask = 1 << (nr & 7);

#ifndef TEST_BITOPS
	save_flags(flags);
	cli();
#endif

        retval = (mask & *addr) != 0;
        *addr ^= mask;

#ifndef TEST_BITOPS
	restore_flags(flags);
#endif

        return retval;


#else   /* BIG ENDIAN BITOPS */


	int retval;
	unsigned long *addr = vaddr;
	unsigned long mask;
#ifndef TEST_BITOPS
	unsigned long flags;
#endif

	addr += nr>>5;
	mask = 1 << (nr&31);

#ifndef TEST_BITOPS
	save_flags(flags);
	cli();
#endif

	retval = (mask & *addr) != 0;
	*addr ^= mask;

#ifndef TEST_BITOPS
	restore_flags(flags);
#endif

	return retval;


#endif
}

/* The following routine need not be atomic. */

extern __inline__ unsigned int test_bit(int nr, void *vaddr)
{
#ifdef __LITTLE_ENDIAN_BITOPS

        unsigned char mask;
        unsigned char *addr = (unsigned char *)vaddr;

        addr += nr >> 3;
        mask = 1 << (nr & 7);
        return ((mask & *addr) != 0);

#else   /* BIG ENDIAN BITOPS */

	unsigned long mask;
	unsigned long *addr = vaddr;

	addr += (nr>>5);
	mask = 1 << (nr&31);
	return ((mask & *addr) != 0);

#endif
}

/* There has to be a faster way to do this, sigh... */

extern __inline__ unsigned long ffz(unsigned long word)
{
  register unsigned long cnt;

  cnt = 0;

#ifdef __LITTLE_ENDIAN_BITOPS

  for(int byte_bit = 24; byte_bit >=0; byte_bit -= 8)
	  for(int bit = 0; bit<8; bit++)
		  if((word>>(byte_bit+bit))&1)
			  cnt++;
		  else
			  return cnt;

#else /* BIT ENDIAN BITOPS */
  while(cnt<32) {
	  if(!((word>>cnt)&1))
		  return cnt;
	  else
		  cnt++;
  }
  return cnt;
#endif

}

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern __inline__ unsigned long
find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
#ifdef __LITTLE_ENDIAN_BITOPS

	/* FOO, needs to be written */

#else   /* BIG ENDIAN BITOPS */
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
#endif
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)


#endif /* defined(_SPARC_BITOPS_H) */

