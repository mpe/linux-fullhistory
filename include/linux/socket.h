#ifndef _LINUX_SOCKET_H
#define _LINUX_SOCKET_H

struct sockaddr {
	u_short sa_family;		/* address family, AF_xxx */
	char sa_data[14];		/* 14 bytes of protocol address */
};

/*
 * socket types
 */
#define SOCK_STREAM	1		/* stream (connection) socket */
#define SOCK_DGRAM	2		/* datagram (connectionless) socket */
#define SOCK_SEQPACKET	3		/* sequential packet socket */
#define SOCK_RAW	4		/* raw socket */

/*
 * supported address families
 */
#define AF_UNSPEC	0
#define AF_UNIX		1
#define AF_INET		2

/*
 * protocol families, same as address families
 */
#define PF_UNIX		AF_UNIX
#define PF_INET		AF_INET

#endif
