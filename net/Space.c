/*
 * Space.c	Defines which protocol modules and I/O device drivers get
 *		linked into the LINUX kernel.  Currently, this is only used
 *		by the NET layer of LINUX, but it eventually might move to
 *		an upper directory of the system.
 *
 * Version:	@(#)Space.c	1.0.2	04/22/93
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 *	Please see the comments in ddi.c - Alan
 * 
 */
 
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ddi.h>


#define CONFIG_UNIX		YES		/* always present...	*/


/*
 * Section A:	Networking Protocol Handlers.
 *		This section defines which networking protocols get
 *		linked into the SOCKET layer of the Linux kernel.
 *		Currently, these are AF_UNIX (always) and AF_INET.
 */
#ifdef	CONFIG_UNIX
#  include "unix/unix.h"
#endif
#ifdef	CONFIG_INET
#  include <linux/inet.h>
#endif
#ifdef CONFIG_IPX
#include "inet/ipxcall.h"
#endif
#ifdef CONFIG_AX25
#include "inet/ax25call.h"
#endif

struct ddi_proto protocols[] = {
#ifdef	CONFIG_UNIX
  { "UNIX",	unix_proto_init	},
#endif
#ifdef  CONFIG_IPX
  { "IPX",	ipx_proto_init },
#endif
#ifdef CONFIG_AX25  
  { "AX.25",	ax25_proto_init },
#endif  
#ifdef	CONFIG_INET
  { "INET",	inet_proto_init	},
#endif
  { NULL,	NULL		}
};


