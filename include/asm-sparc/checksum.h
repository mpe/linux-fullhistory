/* $Id: checksum.h,v 1.13 1996/04/18 03:30:19 davem Exp $ */
#ifndef __SPARC_CHECKSUM_H
#define __SPARC_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the Sparc.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
 *
 * derived from:
 *	Alpha checksum c-code
 *      ix86 inline assembly
 */

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

extern inline unsigned short csum_tcpudp_magic(unsigned long saddr,
					       unsigned long daddr,
					       unsigned short len,
					       unsigned short proto,
					       unsigned int sum)
{
	__asm__ __volatile__("
		addcc	%0, %1, %0
		addxcc	%0, %4, %0
		addxcc	%0, %5, %0
		addx	%0, %%g0, %0

		! We need the carry from the addition of 16-bit
		! significant addition, so we zap out the low bits
		! in one half, zap out the high bits in another,
		! shift them both up to the top 16-bits of a word
		! and do the carry producing addition, finally
		! shift the result back down to the low 16-bits.

		! Actually, we can further optimize away two shifts
		! because we know the low bits of the original
		! value will be added to zero-only bits so cannot
		! affect the addition result nor the final carry
		! bit.

		sll	%0, 16, %1
		addcc	%0, %1, %0		! add and set carry, neat eh?
		srl	%0, 16, %0		! shift back down the result
		addx	%0, %%g0, %0		! get remaining carry bit
		xnor	%%g0, %0, %0		! negate, sparc is cool
		"
		: "=&r" (sum), "=&r" (saddr)
		: "0" (daddr), "1" (saddr), "r" (len+proto), "r" (sum));
		return ((unsigned short) sum); 
}

extern inline unsigned short from32to16(unsigned long x)
{
	__asm__ __volatile__("
		addcc	%0, %1, %0
		srl	%0, 16, %0
		addx	%%g0, %0, %0
		"
		: "=r" (x)
		: "r" (x << 16), "0" (x));
	return x;
}

extern inline unsigned long do_csum(unsigned char * buff, int len)
{
	int odd, count;
	unsigned long result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (odd) {
		result = *buff;
		len--;
		buff++;
	}
	count = len >> 1;		/* nr of 16-bit words.. */
	if (count) {
		if (2 & (unsigned long) buff) {
			result += *(unsigned short *) buff;
			count--;
			len -= 2;
			buff += 2;
		}
		count >>= 1;		/* nr of 32-bit words.. */
		if (count) {
		        unsigned long carry = 0;
			do {
				unsigned long w = *(unsigned long *) buff;
				count--;
				buff += 4;
				result += carry;
				result += w;
				carry = (w > result);
			} while (count);
			result += carry;
			result = (result & 0xffff) + (result >> 16);
		}
		if (len & 2) {
			result += *(unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
		result += (*buff << 8);
	result = from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}

/* ihl is always 5 or greater, almost always is 5, iph is always word
 * aligned but can fail to be dword aligned very often.
 */
extern inline unsigned short ip_fast_csum(const unsigned char *iph, unsigned int ihl)
{
	unsigned int sum;

	__asm__ __volatile__("
		ld	[%1], %0
		sub	%2, 4, %2
		ld	[%1 + 0x4], %%g1
		ld	[%1 + 0x8], %%g2
		addcc	%%g1, %0, %0
		addxcc	%%g2, %0, %0
		ld	[%1 + 0xc], %%g1
		ld	[%1 + 0x10], %%g2
		addxcc	%%g1, %0, %0
		addxcc	%0, %%g0, %0
1:
		addcc	%%g2, %0, %0
		add	%1, 0x4, %1
		addxcc	%0, %%g0, %0
		subcc	%2, 0x1, %2
		bne,a	1b
		 ld	[%1 + 0x10], %%g2

		sll	%0, 16, %2
		addcc	%0, %2, %2
		srl	%2, 16, %0
		addx	%0, %%g0, %2
		xnor	%%g0, %2, %0
2:
		"
		: "=&r" (sum), "=&r" (iph), "=&r" (ihl)
		: "1" (iph), "2" (ihl)
		: "g1", "g2");
	return sum;
}

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
extern inline unsigned int csum_partial(unsigned char * buff, int len, unsigned int sum)
{
	__asm__ __volatile__("
		mov	0, %%g5			! g5 = result
		cmp	%1, 0
		bgu,a	1f
		 andcc	%0, 1, %%g7		! g7 = odd

		b,a	9f

1:
		be,a	1f
		 srl	%1, 1, %%g6		! g6 = count = (len >> 1)

		sub	%1, 1, %1	! if(odd) { result = *buff;
		ldub	[%0], %%g5	!           len--;
		add	%0, 1, %0	!           buff++ }

		srl	%1, 1, %%g6
1:
		cmp	%%g6, 0		! if (count) {
		be,a	8f
		 andcc	%1, 1, %%g0

		andcc	%0, 2, %%g0	! if (2 & buff) {
		be,a	1f
		 srl	%%g6, 1, %%g6

		sub	%1, 2, %1	!	result += *(unsigned short *) buff;
		lduh	[%0], %%g1	!	count--; 
		sub	%%g6, 1, %%g6	!	len -= 2;
		add	%%g1, %%g5, %%g5!	buff += 2; 
		add	%0, 2, %0	! }

		srl	%%g6, 1, %%g6
1:
		cmp	%%g6, 0		! if (count) {
		be,a	2f
		 andcc	%1, 2, %%g0

		ld	[%0], %%g1		! csum aligned 32bit words
1:
		add	%0, 4, %0
		addcc	%%g1, %%g5, %%g5
		addx	%%g5, %%g0, %%g5
		subcc	%%g6, 1, %%g6
		bne,a	1b
		 ld	[%0], %%g1

		sethi	%%hi(0xffff), %%g3
		srl	%%g5, 16, %%g2
		or	%%g3, %%lo(0xffff), %%g3
		and	%%g5, %%g3, %%g5
		add	%%g2, %%g5, %%g5! }

		andcc	%1, 2, %%g0
2:
		be,a	8f		! if (len & 2) {
		 andcc	%1, 1, %%g0

		lduh	[%0], %%g1	!	result += *(unsigned short *) buff; 
		add	%%g5, %%g1, %%g5!	buff += 2; 
		add	%0, 2, %0	! }


		andcc	%1, 1, %%g0
8:
		be,a	1f		! if (len & 1) {
		 sll	%%g5, 16, %%g1

		ldub	[%0], %%g1
		sll	%%g1, 8, %%g1	!	result += (*buff << 8); 
		add	%%g5, %%g1, %%g5! }

		sll	%%g5, 16, %%g1
1:
		addcc	%%g1, %%g5, %%g5! result = from32to16(result);
		srl	%%g5, 16, %%g1
		addx	%%g0, %%g1, %%g5

		orcc	%%g7, %%g0, %%g0! if(odd) {
		be	9f
		 srl	%%g5, 8, %%g1

		and	%%g5, 0xff, %%g2!	result = ((result >> 8) & 0xff) |
		and	%%g1, 0xff, %%g1!		((result & 0xff) << 8);
		sll	%%g2, 8, %%g2
		or	%%g2, %%g1, %%g5! }
9:
		addcc	%2, %%g5, %2	! add result and sum with carry
		addx	%%g0, %2, %2
	" :
        "=&r" (buff), "=&r" (len), "=&r" (sum) :
        "0" (buff), "1" (len), "2" (sum) :
	"g1", "g2", "g3", "g5", "g6", "g7"); 

	return sum;
}

/*
 * the same as csum_partial, but copies from fs:src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern inline unsigned int csum_partial_copy(char *src, char *dst, int len, int sum)
{
	/*
	 * The whole idea is to do the copy and the checksum at
	 * the same time, but we do it the easy way now.
	 *
	 * At least csum on the source, not destination, for cache
	 * reasons..
	 */
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);
	return sum;
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return ~from32to16(do_csum(buff,len));
}

#define csum_partial_copy_fromuser(s, d, l, w)  \
                       csum_partial_copy((char *) (s), (d), (l), (w))

/*
 *	Fold a partial checksum without adding pseudo headers
 */
extern inline unsigned int csum_fold(unsigned int sum)
{
	__asm__ __volatile__("
		addcc	%0, %1, %0
		srl	%0, 16, %0
		addx	%%g0, %0, %0
		xnor	%%g0, %0, %0
		"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum)); 
	return sum;
}

#endif /* !(__SPARC_CHECKSUM_H) */
