/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Adapted for PowerPC by Gary Thomas <gdt@mc.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <net/checksum.h>

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
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
{
	unsigned long result = ~_csum_partial(buff, len, sum);
#if 0	
printk("Csum partial(%x, %d, %x) = %x\n", buff, len, sum, result);
dump_buf(buff, len);
#endif
	return result;
}

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit boundary
 */

unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum)
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

extern unsigned short _ip_fast_csum(unsigned char *buf);

unsigned short
ip_fast_csum(unsigned char *buf, unsigned int len)
{
	unsigned short _val;
	_val = _ip_fast_csum(buf);
#if 0
	printk("IP CKSUM(%x, %d) = %x\n", buf, len, _val);
	dump_buf(buf, len*4);
#endif	
	return (_val);
}

extern unsigned short _ip_compute_csum(unsigned char *buf, int len);

unsigned short
ip_compute_csum(unsigned char *buf, int len)
{
	unsigned short _val;
	_val = _ip_compute_csum(buf, len);
#if 0
	printk("Compute IP CKSUM(%x, %d) = %x\n", buf, len, _val);
	dump_buf(buf, len);
#endif	
	return (_val);
}

unsigned short
_udp_check(unsigned char *buf, int len, int saddr, int daddr, int hdr);

unsigned short
udp_check(unsigned char *buf, int len, int saddr, int daddr)
{
	unsigned short _val;
	int hdr;
	hdr = (len << 16) + IPPROTO_UDP;
	_val = _udp_check(buf, len, saddr, daddr, hdr);
#if 0
	printk("UDP CSUM(%x,%d,%x,%x) = %x\n", buf, len, saddr, daddr, _val);
	dump_buf(buf, len);
#endif	
	return (_val);
}

unsigned short
_tcp_check(unsigned char *buf, int len, int saddr, int daddr, int hdr);

unsigned short
csum_tcpudp_magic(unsigned long saddr, unsigned long daddr, unsigned short len, unsigned short proto, unsigned int sum)
{
	unsigned short _val;
	_val = _csum_tcpudp_magic(saddr, daddr, sum, (len<<16)+proto);
#if 0
	printk("TCP Magic(%x, %x, %x, %x) = %x\n", saddr, daddr, (len<<16)+proto, sum, _val);
#endif
	return (_val);
}

/*
 *	Fold a partial checksum without adding pseudo headers
 */

unsigned int csum_fold(unsigned int sum)
{
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}

