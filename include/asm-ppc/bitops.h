#ifndef _ASM_PPC_BITOPS_H_
#define _ASM_PPC_BITOPS_H_

/*
 * For the benefit of those who are trying to port Linux to another
 * architecture, here are some C-language equivalents.  You should
 * recode these in the native assembly language, if at all possible.
 * To guarantee atomicity, these routines call cli() and sti() to
 * disable interrupts while they operate.  (You have to provide inline
 * routines to cli() and sti().)
 *
 * Also note, these routines assume that you have 32 bit integers.
 * You will have to change this if you are trying to port Linux to the
 * Alpha architecture or to a Cray.  :-)
 * 
 * C language equivalents written by Theodore Ts'o, 9/26/92
 */

#include "asm/system.h"  /* For cli/sti declaration */

#define BIT(n) 1<<(n&0x1F)
typedef unsigned long BITFIELD;

extern __inline__ int set_bit(int nr, void * add)
{
       int	mask, oldbit;
  BITFIELD *addr = add;
       
	int s = _disable_interrupts();
	addr += nr >> 5;
	mask = BIT(nr);
	oldbit = (mask & *addr) != 0;
	*addr |= mask;
	_enable_interrupts(s);

	
	return oldbit;
}

extern __inline__ int change_bit(int nr, void *add)
{
  	BITFIELD *addr = add;
	int	mask, retval;
	int s = _disable_interrupts();
	addr += nr >> 5;
	mask = BIT(nr);
	retval = (mask & *addr) != 0;
	*addr ^= mask;
	_enable_interrupts(s);
	return retval;
}

extern __inline__ int clear_bit(int nr, void *add)
{
        BITFIELD *addr = add;
	int	mask, retval;
	int s = _disable_interrupts();
	addr += nr >> 5;
	mask = BIT(nr);
	retval = (mask & *addr) != 0;
	*addr &= ~mask;
	_enable_interrupts(s);
	return retval;
}

extern __inline__ int test_bit(int nr, void *add)
{
	int	mask;
	BITFIELD *addr = add;

	addr += nr >> 5;
	mask = BIT(nr);
	return ((mask & *addr) != 0);
}
#if 0
extern __inline__ int find_first_zero_bit(void *add, int len)
{
	int	mask, nr, i;
	BITFIELD *addr = add;
	nr = 0;
	while (len)
	{
		if (~*addr != 0)
		{ /* Contains at least one zero */
			for (i = 0;  i < 32;  i++, nr++)
			{
				mask = BIT(nr);
				if ((mask & *addr) == 0)
				{
					return (nr);
				}
			}
		}
		len -= 32;
		addr++;
		nr += 32;
	}
	return (0);  /* Shouldn't happen */
}

extern __inline__ int find_next_zero_bit(void *add, int len, int nr)
{
	int	mask, i;
	BITFIELD *addr = add;
	addr += nr >> 5;
	len -= nr;
	while (len)
	{
		if (*addr != 0xFFFFFFFF)
		{ /* Contains at least one zero */
			for (i = 0;  i < 32;  i++, nr++)
			{
				mask = BIT(nr);
				if ((mask & *addr) == 0)
				{
printk("Bit: %d(%d), Pat: %x\n", nr, nr&0x1F, *addr);					
					return (nr);
				}
			}
		}
		len -= 32;
		addr++;
		nr += 32;
	}
	return (0);  /* Shouldn't happen */
}
#endif
#endif /* _ASM_PPC_BITOPS_H */


