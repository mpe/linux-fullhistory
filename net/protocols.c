/*
 *	Protocol initializer table. Here separately for convenience
 *
 */
 
 
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/net.h>


#define CONFIG_UNIX		/* always present...	*/

#ifdef	CONFIG_UNIX
#include "unix/unix.h"
#endif
#ifdef	CONFIG_INET
#include <linux/inet.h>
#endif
#ifdef CONFIG_IPX
#include "inet/ipxcall.h"
#include "inet/p8022call.h"
#endif
#ifdef CONFIG_AX25
#include "inet/ax25call.h"
#endif
#ifdef CONFIG_ATALK
#ifndef CONFIG_IPX
#include "inet/p8022call.h"
#endif
#include "inet/atalkcall.h"
#endif
#include "inet/psnapcall.h"

/*
 *	Protocol Table
 */
 
struct net_proto protocols[] = {
#ifdef	CONFIG_UNIX
  { "UNIX",	unix_proto_init	},
#endif
#if defined(CONFIG_IPX)||defined(CONFIG_ATALK)  
  { "802.2",	p8022_proto_init },
  { "SNAP",	snap_proto_init },
#endif
#ifdef CONFIG_AX25  
  { "AX.25",	ax25_proto_init },
#endif  
#ifdef	CONFIG_INET
  { "INET",	inet_proto_init	},
#endif
#ifdef  CONFIG_IPX
  { "IPX",	ipx_proto_init },
#endif
#ifdef CONFIG_ATALK
  { "DDP",	atalk_proto_init },
#endif
  { NULL,	NULL		}
};


