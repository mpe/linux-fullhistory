/* $Id: checksum.h,v 1.3 1996/12/12 15:39:13 davem Exp $ */
#ifndef __SPARC64_CHECKSUM_H
#define __SPARC64_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the V9.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *
 * derived from:
 *	Alpha checksum c-code
 *      ix86 inline assembly
 *      RFC1071 Computing the Internet Checksum
 */

/* computes the checksum of a memory block at buff, length len,
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

/* the same as csum_partial, but copies from user space while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned int csum_partial_copy(char *src, char *dst, int len, int sum);

#define csum_partial_copy_fromuser(s, d, l, w)  \
                       csum_partial_copy((char *) (s), (d), (l), (w))

/* ihl is always 5 or greater, almost always is 5, and iph is word aligned
 * the majority of the time.
 */
extern __inline__ unsigned short ip_fast_csum(__const__ unsigned char *iph,
					      unsigned int ihl)
{
	unsigned short sum;

	/* Note: We must read %2 before we touch %0 for the first time,
	 *       because GCC can legitimately use the same register for
	 *       both operands.
	 */
	__asm__ __volatile__("
	sub		%2, 4, %%g4
	lduw		[%1 + 0x00], %0
	lduw		[%1 + 0x04], %%g2
	lduw		[%1 + 0x08], %%g3
	addcc		%%g2, %0, %0
	addccc		%%g3, %0, %0
	lduw		[%1 + 0x0c], %%g2
	lduw		[%1 + 0x10], %%g3
	addccc		%%g2, %0, %0
	addc		%0, %%g0, %0
1:
	addcc		%%g3, %0, %0
	add		%1, 4, %1
	addccc		%0, %%g0, %0
	subcc		%%g4, 1, %%g4
	be,a,pt		%%icc, 2f
	 sll		%0, 16, %%g2
	ba,pt		1b
	 lduw		[%1 + 0x10], %%g3
2:
	addcc		%0, %%g2, %%g2
	srl		%%g2, 16, %0
	addc		%0, %%g0, %0
	xnor		%%g0, %0, %0
"	: "=r" (sum), "=&r" (iph)
	: "r" (ihl), "1" (iph)
	: "g2", "g3", "g4");
	return sum;
}

/* computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern __inline__ unsigned short csum_tcpudp_magic(unsigned long saddr,
						   unsigned long daddr,
						   unsigned int len,
						   unsigned short proto,
						   unsigned int sum)
{
	__asm__ __volatile__("
	addcc		%1, %0, %0
	addccc		%2, %0, %0
	addccc		%3, %0, %0
	addc		%0, %%g0, %0
	sll		%0, 16, %1
	addcc		%1, %0, %0
	srl		%0, 16, %0
	addc		%0, %%g0, %0
	xnor		%%g0, %0, %0
"	: "=r" (sum), "=r" (saddr)
	: "r" (daddr), "r" ((proto<<16)+len), "0" (sum), "1" (saddr));
	return sum;
}

/* Fold a partial checksum without adding pseudo headers. */
extern __inline__ unsigned int csum_fold(unsigned int sum)
{
	unsigned int tmp;

	__asm__ __volatile__("
	addcc		%0, %1, %1
	srl		%1, 16, %1
	addc		%1, %%g0, %1
	xnor		%%g0, %1, %0
"	: "=&r" (sum), "=r" (tmp)
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
	addcc		%3, %4, %%g4
	addccc		%5, %%g4, %%g4
	lduw		[%2 + 0x0c], %%g2
	lduw		[%2 + 0x08], %%g3
	addccc		%%g2, %%g4, %%g4
	lduw		[%2 + 0x04], %%g2
	addccc		%%g3, %%g4, %%g4
	lduw		[%2 + 0x00], %%g3
	addccc		%%g2, %%g4, %%g4
	lduw		[%1 + 0x0c], %%g2
	addccc		%%g3, %%g4, %%g4
	lduw		[%1 + 0x08], %%g3
	addccc		%%g2, %%g4, %%g4
	lduw		[%1 + 0x04], %%g2
	addccc		%%g3, %%g4, %%g4
	lduw		[%1 + 0x00], %%g3
	addccc		%%g2, %%g4, %%g4
	addccc		%%g3, %%g4, %0
	addc		0, %0, %0
"	: "=&r" (sum)
	: "r" (saddr), "r" (daddr), "r"(htonl((__u32) (len))),
	  "r"(htonl(proto)), "r"(sum)
	: "g2", "g3", "g4");

	return csum_fold(sum);
}

/* this routine is used for miscellaneous IP-like checksums, mainly in icmp.c */
extern __inline__ unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#endif /* !(__SPARC64_CHECKSUM_H) */
