/*
 * $Id: socket.h,v 1.5 1998/03/15 09:52:54 ralf Exp $
 */
#ifndef __ASM_MIPS_SOCKET_H
#define __ASM_MIPS_SOCKET_H

#include <asm/sockios.h>

/*
 * For setsockoptions(2)
 *
 * This defines are ABI conformant as far as Linux supports these ...
 */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001	/* Record debugging information.  */
#define SO_REUSEADDR	0x0004	/* Allow reuse of local addresses.  */
#define SO_KEEPALIVE	0x0008	/* Keep connections alive and send
				   SIGPIPE when they die.  */
#define SO_DONTROUTE	0x0010	/* Don't do local routing.  */
#define SO_BROADCAST	0x0020	/* Allow transmission of
				   broadcast messages.  */
#define SO_LINGER	0x0080	/* Block on close of a reliable
				   socket to transmit pending data.  */
#define SO_OOBINLINE 0x0100	/* Receive out-of-band data in-band.  */
#if 0
To add: #define SO_REUSEPORT 0x0200	/* Allow local address and port reuse.  */
#endif

#define SO_TYPE		0x1008	/* Compatible name for SO_STYLE.  */
#define SO_STYLE	SO_TYPE	/* Synonym */
#define SO_ERROR	0x1007	/* get error status and clear */
#define SO_SNDBUF	0x1001	/* Send buffer size. */
#define SO_RCVBUF	0x1002	/* Receive buffer. */
#define SO_SNDLOWAT	0x1003	/* send low-water mark */
#define SO_RCVLOWAT	0x1004	/* receive low-water mark */
#define SO_SNDTIMEO	0x1005	/* send timeout */
#define SO_RCVTIMEO 	0x1006	/* receive timeout */

/* linux-specific, might as well be the same as on i386 */
#define SO_NO_CHECK	11
#define SO_PRIORITY	12
#define SO_BSDCOMPAT	14

#define SO_PASSCRED	17
#define SO_PEERCRED	18

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		22
#define SO_SECURITY_ENCRYPTION_TRANSPORT	23
#define SO_SECURITY_ENCRYPTION_NETWORK		24

#define SO_BINDTODEVICE		25

/* Socket filtering */
#define SO_ATTACH_FILTER        26
#define SO_DETACH_FILTER        27

/* Types of sockets.  */
#define SOCK_DGRAM 1		/* Connectionless, unreliable datagrams
				   of fixed maximum length.  */
#define SOCK_STREAM 2		/* Sequenced, reliable, connection-based
				   byte streams.  */
#define SOCK_RAW 3		/* Raw protocol interface.  */
#define SOCK_RDM 4		/* Reliably-delivered messages.  */
#define SOCK_SEQPACKET 5	/* Sequenced, reliable, connection-based,
				   datagrams of fixed maximum length.  */
#define SOCK_PACKET 10		/* Linux specific way of getting packets at
				   the dev level.  For writing rarp and
				   other similar things on the user level.  */

#endif /* __ASM_MIPS_SOCKET_H */
