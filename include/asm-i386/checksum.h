#ifndef _I386_CHECKSUM_H
#define _I386_CHECKSUM_H

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
unsigned int csum_partial(unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from fs:src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

unsigned int csum_partial_copyffs( char *src, char *dst, int len, int sum);


/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl) {
	unsigned short int sum;

	__asm__("
	    movl (%%esi), %%eax
	    andl $15, %%ecx
	    subl $4, %%ecx
	    jbe 2f
	    addl 4(%%esi), %%eax
	    adcl 8(%%esi), %%eax
	    adcl 12(%%esi), %%eax
1:	    adcl 16(%%esi), %%eax
	    lea 4(%%esi), %%esi
	    decl %%ecx
	    jne	1b
	    adcl $0, %%eax
	    movl %%eax, %%ecx
	    shrl $16, %%eax
	    addw %%ecx, %%eax
	    adcl $0, %%eax
	    notl %%eax
	    andl $65535, %%eax
2:
	    "
	: "=a" (sum)
	: "S" (iph), "c"(ihl)
	: "ax", "cx", "si");
	return(sum);
}




/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum) {
    __asm__("
	addl %2, %0
	adcl %3, %0
	adcl %4, %0
	adcl $0, %0
	movl %0, %2
	shrl $16, %2
	addw %2, %0
	adcl $0, %0
	notl %0
	andl $65535, %0
	"
	: "=r" (sum)
	: "0" (daddr), "S"(saddr), "r"((ntohs(len)<<16)+proto*256), "r"(sum)
	: "si" );
	return((unsigned short)sum);
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

static inline unsigned short ip_compute_csum(unsigned char * buff, int len) {
    unsigned short int sum;

    __asm__("
	movl %%eax, %%ecx
	shrl $16, %%ecx
	addw %%cx, %%ax
	adcl $0, %%eax
	notl %%eax
	andl $65535, %%eax
	"
	: "=a"(sum)
	: "a" (csum_partial(buff, len, 0))
	: "cx");
	return(sum);
}

#endif
