/* $Id: checksum.h,v 1.8 1997/05/29 12:45:03 jj Exp $ */
#ifndef __SPARC64_CHECKSUM_H
#define __SPARC64_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the V9.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *  Copyright(C) 1997 Jakub Jelinek
 *
 * derived from:
 *	Alpha checksum c-code
 *      ix86 inline assembly
 *      RFC1071 Computing the Internet Checksum
 */

#include <asm/uaccess.h> 

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
/* FIXME: Remove these macros ASAP */
#define csum_partial_copy(src, dst, len, sum) \
			csum_partial_copy_nocheck(src,dst,len,sum)
#define csum_partial_copy_fromuser(s, d, l, w)  \
			csum_partial_copy_from_user((char *) (s), (d), (l), (w), NULL)
			
extern __inline__ unsigned int 
csum_partial_copy_nocheck (const char *src, char *dst, int len, 
			   unsigned int sum)
{
	register unsigned long ret asm("o0") = (unsigned long)src;
	register char *d asm("o1") = dst;
	register unsigned long l asm("g1") = len;

	__asm__ __volatile__ ("
		wr	%%g0, %5, %%asi
		call __csum_partial_copy_sparc_generic
		 mov %4, %%g7
		srl	%%o0, 0, %%o0
	" : "=r" (ret) : "0" (ret), "r" (d), "r" (l), "r" (sum), "i" (ASI_P) :
	"o1", "o2", "o3", "o4", "o5", "o7", "g1", "g2", "g3", "g5", "g7");
	return (unsigned int)ret;
}

extern __inline__ unsigned int 
csum_partial_copy_from_user(const char *src, char *dst, int len, 
			    unsigned int sum, int *err)
{
	register unsigned long ret asm("o0") = (unsigned long)src;
	register char *d asm("o1") = dst;
	register unsigned long l asm("g1") = len;
	register unsigned long s asm("g7") = sum;

	__asm__ __volatile__ ("
	.section __ex_table,#alloc
	.align 8
	.xword 1f,2
	.previous
	wr	%%g0, %6, %%asi
1:
	call __csum_partial_copy_sparc_generic
	 stx %5, [%%sp + 0x7ff + 128]
	srl	%%o0, 0, %%o0
	" : "=r" (ret) : "0" (ret), "r" (d), "r" (l), "r" (s), "r" (err), "i" (ASI_S) :
	"o1", "o2", "o3", "o4", "o5", "o7", "g1", "g2", "g3", "g5", "g7");
	return (unsigned int)ret;
}

#if 0
/* Not implemented, but nobody uses it yet... */
extern __inline__ unsigned int 
csum_partial_copy_to_user(const char *src, char *dst, int len, 
			  unsigned int sum, int *err)
{
	return 0;
}
#endif
  
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
	sub		%2, 4, %%g7
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
	subcc		%%g7, 1, %%g7
	be,a,pt		%%icc, 2f
	 sll		%0, 16, %%g2
	ba,pt		%%xcc, 1b
	 lduw		[%1 + 0x10], %%g3
2:
	addcc		%0, %%g2, %%g2
	srl		%%g2, 16, %0
	addc		%0, %%g0, %0
	xnor		%%g0, %0, %0
	srl		%0, 0, %0
"	: "=r" (sum), "=&r" (iph)
	: "r" (ihl), "1" (iph)
	: "g2", "g3", "g7", "cc");
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
	: "r" (daddr), "r" ((proto<<16)+len), "0" (sum), "1" (saddr)
	: "cc");
	return (sum & 0xffff);
}

/* Fold a partial checksum without adding pseudo headers. */
extern __inline__ unsigned short csum_fold(unsigned int sum)
{
	unsigned int tmp;

	__asm__ __volatile__("
	addcc		%0, %1, %1
	srl		%1, 16, %1
	addc		%1, %%g0, %1
	xnor		%%g0, %1, %0
"	: "=&r" (sum), "=r" (tmp)
	: "0" (sum), "1" (sum<<16)
	: "cc");
	return (sum & 0xffff);
}

#define _HAVE_ARCH_IPV6_CSUM

static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int sum) 
{
	__asm__ __volatile__ ("
	addcc		%3, %4, %%g7
	addccc		%5, %%g7, %%g7
	lduw		[%2 + 0x0c], %%g2
	lduw		[%2 + 0x08], %%g3
	addccc		%%g2, %%g7, %%g7
	lduw		[%2 + 0x04], %%g2
	addccc		%%g3, %%g7, %%g7
	lduw		[%2 + 0x00], %%g3
	addccc		%%g2, %%g7, %%g7
	lduw		[%1 + 0x0c], %%g2
	addccc		%%g3, %%g7, %%g7
	lduw		[%1 + 0x08], %%g3
	addccc		%%g2, %%g7, %%g7
	lduw		[%1 + 0x04], %%g2
	addccc		%%g3, %%g7, %%g7
	lduw		[%1 + 0x00], %%g3
	addccc		%%g2, %%g7, %%g7
	addccc		%%g3, %%g7, %0
	addc		0, %0, %0
"	: "=&r" (sum)
	: "r" (saddr), "r" (daddr), "r"(htonl((__u32) (len))),
	  "r"(htonl(proto)), "r"(sum)
	: "g2", "g3", "g7", "cc");

	return csum_fold(sum);
}

/* this routine is used for miscellaneous IP-like checksums, mainly in icmp.c */
extern __inline__ unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#endif /* !(__SPARC64_CHECKSUM_H) */
