#ifndef _M68K_CHECKSUM_H
#define _M68K_CHECKSUM_H

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
 * the same as csum_partial_copy, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum);


/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

unsigned int csum_partial_copy_fromuser(const char *src, char *dst, int len, int sum);


/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 */
static inline unsigned short
ip_fast_csum(unsigned char *iph, unsigned int ihl)
{
	unsigned int sum = 0;

	__asm__ ("subqw #1,%2\n"
		 "1:\t"
		 "movel %1@+,%/d0\n\t"
		 "addxl %/d0,%0\n\t"
		 "dbra  %2,1b\n\t"
		 "movel %0,%/d0\n\t"
		 "swap  %/d0\n\t"
		 "addxw %/d0,%0\n\t"
		 "clrw  %/d0\n\t"
		 "addxw %/d0,%0\n\t"
		 : "=d" (sum), "=a" (iph), "=d" (ihl)
		 : "0" (sum), "1" (iph), "2" (ihl)
		 : "d0");
	return ~sum;
}



/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline unsigned short int
csum_tcpudp_magic(unsigned long saddr, unsigned long daddr, unsigned short len,
		  unsigned short proto, unsigned int sum)
{
	__asm__ ("addl  %1,%0\n\t"
		 "addxl %4,%0\n\t"
		 "addxl %5,%0\n\t"
		 "movl  %0,%1\n\t"
		 "swap  %1\n\t"
		 "addxw %1,%0\n\t"
		 "clrw  %1\n\t"
		 "addxw %1,%0\n\t"
		 : "=&d" (sum), "=&d" (saddr)
		 : "0" (daddr), "1" (saddr), "d" (len + proto),
		   "d"(sum));
	return ~sum;
}

/*
 *	Fold a partial checksum without adding pseudo headers
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	unsigned int tmp = sum;
	__asm__("swap %1\n\t"
		"addw %1, %0\n\t"
		"clrw %1\n\t"
		"addxw %1, %0"
		: "=&d" (sum), "=&d" (tmp)
		: "0" (sum), "1" (sum));
	return ~sum;
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

#if 1
static inline unsigned short
ip_compute_csum(unsigned char * buff, int len)
{
	unsigned int sum;
	unsigned int scratch;

	__asm__("movel %0,%1\n\t"
		"swap  %1\n\t"
		"addw  %1,%0\n\t"
		"clrw  %1\n\t"
		"addxw %1,%0\n\t"
		: "=d" (sum), "=d" (scratch)
		: "0" (csum_partial(buff, len, 0)));
	return ~sum;
}
#else
static inline unsigned short
ip_compute_csum(unsigned char * buff, int len)
{
	unsigned long sum = 0;

  /* Do the first multiple of 4 bytes and convert to 16 bits. */
  if (len > 3)
    {
      int dummy;
      __asm__ ("subql #1,%2\n\t"
	       "1:\t"
	       "movel %1@+,%/d0\n\t"
	       "addxl %/d0,%0\n\t"
	       "dbra %2,1b\n\t"
	       "movel %0,%/d0\n\t"
	       "swap %/d0\n\t"
	       "addxw %/d0,%0\n\t"
	       "clrw %/d0\n\t"
	       "addxw %/d0,%0"
	       : "=d" (sum), "=a" (buff), "=d" (dummy)
	       : "0" (sum), "1" (buff), "2" (len >> 2)
	       : "d0");
    }
  if (len & 2)
    {
      __asm__ ("addw %1@+,%0\n\t"
	       "addxw %2,%0"
	       : "=d" (sum), "=a" (buff)
	       : "d" (0), "0" (sum), "1" (buff));
    }
  if (len & 1)
    {
      __asm__ ("movew %1@,%/d0\n\t"
	       "clrb %/d0\n\t"
	       "addw %/d0,%0\n\t"
	       "clrw %/d0\n\t"
	       "addxw %/d0,%0"
	       : "=d" (sum)
	       : "a" (buff), "0" (sum)
	       : "d0");
    }

	sum =~sum;
	return(sum & 0xffff);
}
#endif

#endif /* _M68K_CHECKSUM_H */
