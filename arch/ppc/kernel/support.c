/*
 * Miscellaneous support routines
 */

#include <asm/bitops.h>

/*extern __inline__*/ int find_first_zero_bit(void *add, int len)
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

/*extern __inline__*/ int find_next_zero_bit(void *add, int last_bit, int nr)
{
	int	mask, i;
	BITFIELD *addr = add;
#if 0	
printk("Find next (%x, %x)", addr, nr);
#endif
	addr += nr >> 5;
#if 0	
printk(" - Pat: %x(%08X)\n", addr, *addr);
#endif
	if ((nr & 0x1F) != 0)
	{ 
		if (*addr != 0xFFFFFFFF)
		{ /* At least one more bit available in this longword */
			for (i = (nr&0x1F);  i < 32;  i++, nr++)
			{
				mask = BIT(nr);
				if ((mask & *addr) == 0)
				{
#if 0					
printk("(1)Bit: %x(%d), Pat: %x(%08x)\n", nr, nr&0x1F, addr, *addr);
#endif
					return (nr);
				}
			}
		}
		addr++;
		nr = (nr + 0x1F) & ~0x1F;
	}
	while (nr < last_bit)
	{
		if (*addr != 0xFFFFFFFF)
		{ /* Contains at least one zero */
			for (i = 0;  i < 32;  i++, nr++)
			{
				mask = BIT(nr);
				if ((mask & *addr) == 0)
				{
#if 0					
printk("(2)Bit: %x(%d), Pat: %x(%08x)\n", nr, nr&0x1F, addr, *addr);
#endif
					return (nr);
				}
			}
		}
		addr++;
		nr += 32;
	}
	return (nr);  /* Shouldn't happen */
}


