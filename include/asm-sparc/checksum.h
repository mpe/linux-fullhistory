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
 *      RFC1071 Computing the Internet Checksum
 */

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
extern unsigned int csum_partial(unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from fs:src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned int csum_partial_copy(char *src, char *dst, int len, int sum);

#define csum_partial_copy_fromuser(s, d, l, w)  \
                       csum_partial_copy((char *) (s), (d), (l), (w))

/* ihl is always 5 or greater, almost always is 5, iph is always word
 * aligned but can fail to be dword aligned very often.
 */
extern inline unsigned short ip_fast_csum(const unsigned char *iph, unsigned int ihl)
{
	unsigned long tmp1, tmp2;
	unsigned short sum;

	__asm__ __volatile__("
		ld	[%1 + 0x00], %0
		ld	[%1 + 0x04], %3
		sub	%2, 4, %2
		addcc	%3, %0, %0
		ld	[%1 + 0x08], %4
		addxcc	%4, %0, %0
		ld	[%1 + 0x0c], %3
		addxcc	%3, %0, %0
		ld	[%1 + 0x10], %4
		addx	%0, %%g0, %0
	1:
		addcc	%4, %0, %0
		add	%1, 4, %1
		addxcc	%0, %%g0, %0
		subcc	%2, 1, %2
		be,a	2f
		 sll	%0, 16, %3

		b	1b
		 ld	[%1 + 0x10], %4
	2:
		addcc	%0, %3, %3
		srl	%3, 16, %0
		addx	%0, %%g0, %0
		xnor	%%g0, %0, %0
	" : "=r" (sum), "=&r" (iph), "=&r" (ihl), "=r" (tmp1), "=r" (tmp2)
	  : "1" (iph), "2" (ihl));

	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern inline unsigned short csum_tcpudp_magic(unsigned long saddr, unsigned long daddr,
					       unsigned int len, unsigned short proto,
					       unsigned int sum)
{
	__asm__ __volatile__("
		addcc	%1, %0, %0
		addxcc	%2, %0, %0
		addxcc	%3, %0, %0
		addx	%0, %%g0, %0
		sll	%0, 16, %1
		addcc	%1, %0, %0
		srl	%0, 16, %0
		addx	%0, %%g0, %0
		xnor	%%g0, %0, %0
	" : "=r" (sum), "=r" (saddr)
	  : "r" (daddr), "r" ((proto<<16)+len), "0" (sum), "1" (saddr));

	return sum;
}

/*
 *	Fold a partial checksum without adding pseudo headers
 */
extern inline unsigned int csum_fold(unsigned int sum)
{
	unsigned int tmp;

	__asm__ __volatile__("
		addcc	%0, %1, %1
		srl	%1, 16, %1
		addx	%1, %%g0, %1
		xnor	%%g0, %1, %0
	" : "=&r" (sum), "=r" (tmp)
	  : "0" (sum), "1" (sum<<16));

	return sum;
}

#define _HAVE_ARCH_IPV6_CSUM

static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int sum) 
{
	__asm__ __volatile__ ("
		addcc	%3, %4, %%g4
		addxcc	%5, %%g4, %%g4
		ld	[%2 + 0x0c], %%g2
		ld	[%2 + 0x08], %%g3
		addxcc	%%g2, %%g4, %%g4
		ld	[%2 + 0x04], %%g2
		addxcc	%%g3, %%g4, %%g4
		ld	[%2 + 0x00], %%g3
		addxcc	%%g2, %%g4, %%g4
		ld	[%1 + 0x0c], %%g2
		addxcc	%%g3, %%g4, %%g4
		ld	[%1 + 0x08], %%g3
		addxcc	%%g2, %%g4, %%g4
		ld	[%1 + 0x04], %%g2
		addxcc	%%g3, %%g4, %%g4
		ld	[%1 + 0x00], %%g3
		addxcc	%%g2, %%g4, %%g4
		addxcc	%%g3, %%g4, %0
		addx	0, %0, %0
		"
		: "=&r" (sum)
		: "r" (saddr), "r" (daddr), 
		  "r"(htonl((__u32) (len))), "r"(htonl(proto)), "r"(sum)
		: "g2", "g3", "g4");

	return csum_fold(sum);
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#endif /* !(__SPARC_CHECKSUM_H) */
