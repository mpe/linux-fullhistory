/* $Id: socket.h,v 1.6 1996/04/04 12:51:26 davem Exp $ */
#ifndef _ASM_SOCKET_H
#define _ASM_SOCKET_H

#include <asm/sockios.h>

/* For setsockoptions(2) */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001
#define SO_REUSEADDR	0x0004
#define SO_KEEPALIVE	0x0008
#define SO_DONTROUTE	0x0010
#define SO_BROADCAST	0x0020
#define SO_LINGER	0x0080
#define SO_OOBINLINE	0x0100
/* To add :#define SO_REUSEPORT 0x0200 */
#define SO_BSDCOMPAT    0x0400

/* wha!??? */
#define SO_DONTLINGER   (~SO_LINGER)  /* Older SunOS compat. hack */

#define SO_SNDBUF	0x1001
#define SO_RCVBUF	0x1002
#define SO_ERROR	0x1007
#define SO_TYPE		0x1008

/* Linux specific, keep the same. */
#define SO_NO_CHECK	0x000b
#define SO_PRIORITY	0x000c

#endif /* _ASM_SOCKET_H */
