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
#include <net/unix.h>
#endif
#ifdef	CONFIG_INET
#include <linux/inet.h>
#endif
#ifdef CONFIG_IPX
#include <net/ipxcall.h>
#include <net/p8022call.h>
#endif
#ifdef CONFIG_AX25
#include <net/ax25call.h>
#ifdef CONFIG_NETROM
#include <net/nrcall.h>
#endif
#endif
#ifdef CONFIG_ATALK
#ifndef CONFIG_IPX
#include <net/p8022call.h>
#endif
#include <net/atalkcall.h>
#endif
#include <net/psnapcall.h>
#ifdef CONFIG_TR
#include <linux/netdevice.h>
#include <linux/trdevice.h>
extern void rif_init(struct net_proto *);
#endif
/*
 *	Protocol Table
 */
 
struct net_proto protocols[] = {
#ifdef	CONFIG_UNIX
  { "UNIX",	unix_proto_init	},			/* Unix domain socket family 	*/
#endif
#if defined(CONFIG_IPX)||defined(CONFIG_ATALK)  
  { "802.2",	p8022_proto_init },			/* 802.2 demultiplexor		*/
  { "SNAP",	snap_proto_init },			/* SNAP demultiplexor		*/
#endif
#ifdef CONFIG_TR
  { "RIF",	rif_init },				/* RIF for Token ring		*/
#endif  
#ifdef CONFIG_AX25  
  { "AX.25",	ax25_proto_init },
#ifdef CONFIG_NETROM
  { "NET/ROM",	nr_proto_init },
#endif
#endif  
#ifdef	CONFIG_INET
  { "INET",	inet_proto_init	},			/* TCP/IP			*/
#endif
#ifdef  CONFIG_IPX
  { "IPX",	ipx_proto_init },			/* IPX				*/
#endif
#ifdef CONFIG_ATALK
  { "DDP",	atalk_proto_init },			/* Netatalk Appletalk driver	*/
#endif
  { NULL,	NULL		}			/* End marker			*/
};


