#ifndef _ASM_PPC_BITOPS_H_
#define _ASM_PPC_BITOPS_H_

#include <asm/system.h>
#include <asm/byteorder.h>

#define BIT(n) 1<<(n&0x1F)
typedef unsigned long BITFIELD;


/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. powerpc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */
extern __inline__ int set_bit(int nr, void * add)
{
  BITFIELD *addr = add;
	long	mask,oldbit;       
#ifdef __KERNEL__
	int s = _disable_interrupts();
#endif
	addr += nr >> 5;
	mask = BIT(nr);
	oldbit = (mask & *addr) != 0;
	*addr |= mask;
#ifdef __KERNEL__	
	_enable_interrupts(s);
#endif
	return oldbit;
}

extern __inline__ int change_bit(int nr, void *add)
{
  	BITFIELD *addr = add;
	int	mask, retval;
#ifdef __KERNEL__
	int s = _disable_interrupts();
#endif
	addr += nr >> 5;
	mask = BIT(nr);
	retval = (mask & *addr) != 0;
	*addr ^= mask;
#ifdef __KERNEL__
	_enable_interrupts(s);
#endif
	return retval;
}

extern __inline__ int clear_bit(int nr, void *add)
{
        BITFIELD *addr = add;
	int	mask, retval;
#ifdef __KERNEL__
	int s = _disable_interrupts();
#endif
	addr += nr >> 5;
	mask = BIT(nr);
	retval = (mask & *addr) != 0;
	*addr &= ~mask;
#ifdef __KERNEL__	
	_enable_interrupts(s);
#endif
	return retval;
}

#define _EXT2_HAVE_ASM_BITOPS_
#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)


extern __inline__ int ext2_set_bit(int nr, void * addr)
{
#ifdef __KERNEL__
  int s = _disable_interrupts();
#endif
  int			mask;
  unsigned char	*ADDR = (unsigned char *) addr;
  int oldbit;
  
  ADDR += nr >> 3;
  mask = 1 << (nr & 0x07);
  oldbit = (*ADDR & mask) ? 1 : 0;
  *ADDR |= mask;
#ifdef __KERNEL__	
  _enable_interrupts(s);
#endif
  return oldbit;
}

extern __inline__ int ext2_clear_bit(int nr, void * addr)
{
#ifdef __KERNEL__
  int s = _disable_interrupts();
#endif
  int		mask;
  unsigned char	*ADDR = (unsigned char *) addr;
  int oldbit;
  
  ADDR += nr >> 3;
  mask = 1 << (nr & 0x07);
  oldbit = (*ADDR & mask) ? 1 : 0;
  *ADDR = *ADDR & ~mask;
#ifdef __KERNEL__	
  _enable_interrupts(s);
#endif
  return oldbit;
}


/* The following routine need not be atomic. */
extern __inline__ unsigned long test_bit(int nr, void *addr)
{
	return 1UL & (((__const__ unsigned int *) addr)[nr >> 5] >> (nr & 31));
}

extern __inline__ int ext2_test_bit(int nr, __const__ void * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

extern __inline__ unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		tmp = *(p++);
		tmp |= le32_to_cpu(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	return result + ffz(le32_to_cpu(tmp) | (~0UL << size));
found_middle:
	return result + ffz(le32_to_cpu(tmp));
}

#endif /* _ASM_PPC_BITOPS_H */


