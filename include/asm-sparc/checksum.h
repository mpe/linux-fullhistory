/* $Id: checksum.h,v 1.10 1995/11/25 02:31:23 davem Exp $ */
#ifndef __SPARC_CHECKSUM_H
#define __SPARC_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the Sparc.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 */


/* 32 bits version of the checksum routines written for the Alpha by Linus */
extern inline unsigned short from32to16(unsigned long x)
{
	/* add up 16-bit and 17-bit words for 17+c bits */
	x = (x & 0xffff) + (x >> 16);
	/* add up 16-bit and 2-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

extern inline unsigned long
do_csum(unsigned char * buff, int len)
{
	int odd, count;
	unsigned long result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (odd) {
		result = *buff << 8;
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
				len -= 4;
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
		result += (*buff) << 8;
	result = from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}

extern inline unsigned short ip_fast_csum(unsigned char * iph, unsigned int ihl)
{
	return ~do_csum(iph,ihl*4);
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
	unsigned long result = do_csum(buff, len);

	/* add in old sum, and carry.. */
	result += sum;
	/* 32+c bits -> 32 bits */
	result = (result & 0xffff) + (result >> 16);
	return result;
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

static inline unsigned short csum_fold(unsigned int sum)
{
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
					   unsigned long daddr,
					   unsigned short len,
					   unsigned short proto,
					   unsigned int sum)
{
	return ~from32to16 (((saddr >> 16) + (saddr & 0xffff) + (daddr >> 16)
			     + (daddr & 0xffff) + (sum >> 16) +
			     (sum & 0xffff) + proto + len));
}

#endif /* !(__SPARC_CHECKSUM_H) */
