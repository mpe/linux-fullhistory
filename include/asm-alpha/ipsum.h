#ifndef __ASM_IPSUM_H
#define __ASM_IPSUM_H

/*
 *	This routine computes a UDP checksum. 
 */
extern inline unsigned short udp_check(struct udphdr *uh, int len, u32 saddr, u32 daddr)
{
	/* uhh.. eventually */
	return 0;
}

/*
 *	This routine computes a TCP checksum. 
 */
extern inline unsigned short tcp_check(struct tcphdr *th, int len, u32 saddr, u32 daddr)
{     
	/* uhh.. eventually */
	return 0;
}


/*
 * This routine does all the checksum computations that don't
 * require anything special (like copying or special headers).
 */

extern inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	/* uhh.. eventually */
	return 0;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers, which
 *	always checksum on 4 octet boundaries.
 */

static inline unsigned short ip_fast_csum(unsigned char * buff, int wlen)
{
	/* uhh.. eventually */
	return 0;
}

#endif
