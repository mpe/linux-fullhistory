/*
 * include/asm-mips/checksum.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#ifndef __ASM_MIPS_CHECKSUM_H
#define __ASM_MIPS_CHECKSUM_H

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
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum);

/*
 * the same as csum_partial, but copies from user space (but on the alpha
 * we have just one address space, so this is identical to the above)
 */
#define csum_partial_copy_fromuser csum_partial_copy
  
/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl)
{
	unsigned short int sum;
	unsigned long	dummy1, dummy2;

	/*
	 * This is optimized for 32-bit MIPS processors.
	 * I tried it in plain C but the generated code looks to bad to
	 * use with old first generation MIPS CPUs.
	 * Using 64-bit code could even further improve these routines.
	 */
	__asm__("
	.set	noreorder
	.set	noat
	lw	%0,(%3)
	subu	%1,4
	blez	%1,2f
	sll	%1,%4,2			# delay slot
	lw	%2,4(%3)
	addu	%1,%3			# delay slot
	addu	%0,%2
	sltu	$1,%0,%2
	lw	%2,8(%3)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2
	lw	%2,12(%3)
	addu	%0,$1
	addu	%0,%2
	sltu	$1,%0,%2
	addu	%0,$1
1:	lw	%2,16(%3)
	addu	%1,4
	addu	%0,%2
	sltu	$1,%0,%2
	bne	%1,%3,1b
	addu	%0,$1			# delay slot
	srl	$1,%0,16
	addu	%0,$1
	sltu	$1,%0,$1
	addu	%0,$1
	nor	%0,$0,%0
	andi	%0,0xffff
2:	.set	at
	.set	reorder"
	: "=r" (sum), "=r" (dummy1), "=r" (dummy2)
	: "r" (iph), "r"(ihl)
	: "$1");

	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
    __asm__("
	.set	noat
	addu	%0,%2
	sltu	$1,%0,%2
	addu	%0,$1
	addu	%0,%3
	sltu	$1,%0,%3
	addu	%0,$1
	addu	%0,%4
	sltu	$1,%0,%4
	addu	%0,$1
	srl	$1,%0,16
	addu	%0,$1
	sltu	$1,%0,$1
	addu	%0,$1
	nor	%0,$0,%0
	andi	%0,0xffff
	.set	at"
	: "=r" (sum)
	: "0" (daddr), "r"(saddr), "r"((ntohs(len)<<16)+proto*256), "r"(sum)
	: "$1");

	return (unsigned short)sum;
}

/*
 *	Fold a partial checksum without adding pseudo headers
 */
static inline unsigned short int csum_fold(unsigned int sum)
{
    __asm__("
	.set	noat
	srl	$1,%0,16
	addu	%0,$1
	sltu	$1,%0,$1
	nor	%0,$0,%0
	andi	%0,0xffff
	.set	at"
	: "=r"(sum)
	: "0" (sum)
	: "$1");

 	return sum;
}
 
/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline unsigned short ip_compute_csum(unsigned char * buff, int len) {
    unsigned short int sum;

    __asm__("
	.set	noat
	srl	$1,%0,16
	addu	%0,$1
	sltu	$1,%0,$1
	nor	%0,$0,%0
	andi	%0,0xffff
	.set	at"
	: "=r"(sum)
	: "r" (csum_partial(buff, len, 0))
	: "$1");

	return sum;
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int sum) 
{
	unsigned long scratch;

        __asm__("
		.set	noreorder
		.set	noat
		addu	%0,%5		# proto (long in network byte order)
		sltu	$1,%0,%5
		addu	%0,$1

		addu	%0,%6		# csum
		sltu	$1,%0,%6
		lw	%1,0(%2)	# four words source address
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,4(%2)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,8(%2)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,12(%2)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,0(%3)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,4(%3)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,8(%3)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1

		lw	%1,12(%3)
		addu	%0,$1
		addu	%0,%1
		sltu	$1,%0,$1
		.set	noat
		.set	noreorder
                "
                : "=r" (sum),
		  "=r" (scratch)
                : "r" (saddr),
		  "r" (daddr),
                  "0" (htonl((__u32) (len))),
		  "r" (htonl(proto)),
		  "r"(sum)
		: "$1");

	return csum_fold(sum);
}

#endif /* __ASM_MIPS_CHECKSUM_H */
