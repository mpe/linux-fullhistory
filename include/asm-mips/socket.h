#ifndef __ASM_MIPS_SOCKET_H
#define __ASM_MIPS_SOCKET_H

#include <linux/types.h>
#include <asm/ioctl.h>

/* Socket-level I/O control calls. */
#define FIOGETOWN	_IOR('f', 123, int)
#define FIOSETOWN 	_IOW('f', 124, int)

#define SIOCATMARK	_IOR('s', 7, int)
#define SIOCSPGRP	_IOW('s', 8, pid_t)
#define SIOCGPGRP	_IOR('s', 9, pid_t)

#define SIOCGSTAMP	0x8906			/* Get stamp - linux-specific */

/*
 * For setsockoptions(2)
 *
 * This defines are ABI conformant as far as Linux supports these ...
 */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001
#define SO_REUSEADDR	0x0004
#define SO_KEEPALIVE	0x0008
#define SO_DONTROUTE	0x0010
#define SO_BROADCAST	0x0020
#define SO_LINGER	0x0080
#define SO_OOBINLINE	0x0100
/* To add: #define SO_REUSEPORT 0x0200 */

#define SO_TYPE		0x1008
#define SO_ERROR	0x1007
#define SO_SNDBUF	0x1001
#define SO_RCVBUF	0x1002

/* linux-specific, might as well be the same as on i386 */
#define SO_NO_CHECK	11
#define SO_PRIORITY	12
#define SO_BSDCOMPAT	14

/*
 * Weird.  There are two ABIs; in the old one SOCK_STREAM and SOCK_DGRAM
 * have swapped values.  I choose to be compatible with the new one.
 */
#define SOCK_DGRAM	1		/* datagram (conn.less) socket	*/
#define SOCK_STREAM	2		/* stream (connection) socket	*/
#define SOCK_RAW	3		/* raw socket			*/
#define SOCK_RDM	4		/* reliably-delivered message	*/
#define SOCK_SEQPACKET	5		/* sequential packet socket	*/
#define SOCK_PACKET	10		/* linux specific way of	*/
					/* getting packets at the dev	*/
					/* level.  For writing rarp and	*/
					/* other similar things on the	*/
					/* user level.			*/

#endif /* __ASM_MIPS_SOCKET_H */
