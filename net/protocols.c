/*
 *	Protocol initializer table. Here separately for convenience
 *
 */
 
 
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/fs.h>

#ifdef	CONFIG_UNIX
#include <linux/un.h>
#include <net/af_unix.h>
#endif

#ifdef	CONFIG_INET
#include <linux/inet.h>
#ifdef	CONFIG_IPV6
extern void inet6_proto_init(struct net_proto *pro);
#endif
#endif	/* INET */

#ifdef CONFIG_ECONET
extern void econet_proto_init(struct net_proto *pro);
#endif

#ifdef CONFIG_NETLINK
extern void netlink_proto_init(struct net_proto *pro);
#endif

#ifdef CONFIG_PACKET
extern void packet_proto_init(struct net_proto *pro);
#endif

#if defined(CONFIG_IPX) || defined(CONFIG_IPX_MODULE)
#define NEED_802
#include <net/ipxcall.h>
#endif

#ifdef CONFIG_X25
#include <net/x25call.h>
#endif

#ifdef CONFIG_LAPB
#include <net/lapbcall.h>
#endif

#ifdef CONFIG_AX25
#include <net/ax25call.h>
#ifdef CONFIG_NETROM
#include <net/nrcall.h>
#endif
#ifdef CONFIG_ROSE
#include <net/rosecall.h>
#endif
#endif

#ifdef CONFIG_IRDA
#include <net/irda/irdacall.h>
#endif

#if defined(CONFIG_DECNET)
#include <net/decnet_call.h>
#endif

#if defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE)
#define NEED_802
#include <net/atalkcall.h>
#endif

#if defined(CONFIG_NETBEUI)
#define NEED_LLC
#include <net/netbeuicall.h>
#endif

#if defined(CONFIG_LLC)
#define NEED_LLC
#endif

#include <net/psnapcall.h>

#ifdef CONFIG_TR
#include <linux/netdevice.h>
#include <linux/trdevice.h>
extern void rif_init(struct net_proto *);
#endif

#ifdef NEED_LLC
#define NEED_802
#include <net/llccall.h>
#endif

#ifdef NEED_802
#include <net/p8022call.h>
#endif

/*
 *	Protocol Table
 */
 
struct net_proto protocols[] = {
#ifdef  CONFIG_NETLINK
  { "NETLINK",	netlink_proto_init	},
#endif

#ifdef  CONFIG_PACKET
  { "PACKET",	packet_proto_init	},
#endif

#ifdef	CONFIG_UNIX
  { "UNIX",	unix_proto_init	},			/* Unix domain socket family 	*/
#endif

#ifdef NEED_802
  { "802.2",	p8022_proto_init },			/* 802.2 demultiplexor		*/
  { "SNAP",	snap_proto_init },			/* SNAP demultiplexor		*/
#endif

#ifdef CONFIG_TR
  { "RIF",	rif_init },				/* RIF for Token ring		*/
#endif  

#ifdef NEED_LLC
  { "802.2LLC", llc_init },				/* 802.2 LLC */
#endif  

#ifdef CONFIG_AX25  
  { "AX.25",	ax25_proto_init },			/* Amateur Radio AX.25 */
#ifdef CONFIG_NETROM
  { "NET/ROM",	nr_proto_init },			/* Amateur Radio NET/ROM */
#endif
#ifdef CONFIG_ROSE
  { "Rose",	rose_proto_init },			/* Amateur Radio X.25 PLP */
#endif
#endif  
#ifdef CONFIG_DECNET
  { "DECnet",   decnet_proto_init },                    /* DECnet */
#endif
#ifdef	CONFIG_INET
  { "INET",	inet_proto_init	},			/* TCP/IP */
#ifdef	CONFIG_IPV6
  { "INET6",	inet6_proto_init},			/* IPv6	*/
#endif
#endif

#ifdef  CONFIG_IPX
  { "IPX",	ipx_proto_init },			/* IPX				*/
#endif

#ifdef CONFIG_ATALK
  { "DDP",	atalk_proto_init },			/* Netatalk Appletalk driver	*/
#endif

#ifdef CONFIG_LAPB
  { "LAPB",     lapb_proto_init },			/* LAPB protocols */
#endif

#ifdef CONFIG_X25
  { "X.25",	x25_proto_init },			/* CCITT X.25 Packet Layer */
#endif

#ifdef CONFIG_ECONET
  { "Econet",	econet_proto_init },			/* Acorn Econet */
#endif

#ifdef CONFIG_IRDA
  { "IrDA",     irda_proto_init },                     /* IrDA protocols */
#endif

  { NULL,	NULL		}			/* End marker			*/
};
