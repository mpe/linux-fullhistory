/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/in.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/ioport.h>

#ifdef CONFIG_AX25
#include <net/ax25.h>
#endif

#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <net/route.h>
#include <linux/net_alias.h>
#endif

#ifdef CONFIG_NETLINK
#include <net/netlink.h>
#endif

#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif

#if     defined(CONFIG_ULTRA)   ||      defined(CONFIG_WD80x3)          || \
        defined(CONFIG_EL2)     ||      defined(CONFIG_NE2000)          || \
        defined(CONFIG_E2100)   ||      defined(CONFIG_HPLAN_PLUS)      || \
        defined(CONFIG_HPLAN)   ||      defined(CONFIG_AC3200)
#include "../drivers/net/8390.h"
#endif

extern int (*rarp_ioctl_hook)(int,void*);

#ifdef CONFIG_IPX_MODULE
extern struct datalink_proto   *make_EII_client(void);
extern struct datalink_proto   *make_8023_client(void);
extern void destroy_EII_client(struct datalink_proto *);
extern void destroy_8023_client(struct datalink_proto *);
#endif

#ifdef CONFIG_DLCI_MODULE
extern int (*dlci_ioctl_hook)(unsigned int, void *);
#endif

static struct symbol_table net_syms = {
#include <linux/symtab_begin.h>

	/* Socket layer registration */
	X(sock_register),
	X(sock_unregister),

	/* Socket layer support routines */
	X(memcpy_fromiovec),
	X(sock_setsockopt),
	X(sock_getsockopt),
	X(sk_alloc),
	X(sk_free),
	X(sock_wake_async),
	X(sock_alloc_send_skb),
	X(skb_recv_datagram),
	X(skb_free_datagram),
	X(skb_copy_datagram),
	X(skb_copy_datagram_iovec),
	X(datagram_select),

#ifdef CONFIG_IPX_MODULE
	X(make_8023_client),
	X(destroy_8023_client),
	X(make_EII_client),
	X(destroy_EII_client),
#endif

#ifdef CONFIG_INET
	/* Internet layer registration */
	X(inet_add_protocol),
	X(inet_del_protocol),
	X(rarp_ioctl_hook),

#ifdef CONFIG_DLCI_MODULE
        X(dlci_ioctl_hook),
#endif

	X(init_etherdev),
	X(ip_rt_route),
	X(icmp_send),
	X(ip_options_compile),
	X(ip_rt_put),
	X(arp_send),
	X(ip_id_count),
	X(ip_send_check),
#ifdef CONFIG_IP_FORWARD
	X(ip_forward),
#endif

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)
	/* If 8390 NIC support is built in, we will need these. */
	X(ei_open),
	X(ei_close),
	X(ei_debug),
	X(ei_interrupt),
	X(ethdev_init),
	X(NS8390_init),
#endif

#ifdef CONFIG_TR
	X(tr_setup),
	X(tr_type_trans),
#endif
                          
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif

#endif  /* CONFIG_INET */

	/* Device callback registration */
	X(register_netdevice_notifier),
	X(unregister_netdevice_notifier),

#ifdef CONFIG_NET_ALIAS
	X(register_net_alias_type),
	X(unregister_net_alias_type),
#endif

        /* support for loadable net drivers */
#ifdef CONFIG_AX25
	X(ax25_encapsulate),
	X(ax25_rebuild_header),
#endif
#ifdef CONFIG_INET
	X(register_netdev),
	X(unregister_netdev),
	X(ether_setup),
	X(eth_type_trans),
	X(eth_copy_and_sum),
	X(arp_query),
	X(alloc_skb),
	X(kfree_skb),
	X(skb_clone),
	X(dev_alloc_skb),
	X(dev_kfree_skb),
	X(netif_rx),
	X(dev_tint),
	X(irq2dev_map),
	X(dev_add_pack),
	X(dev_remove_pack),
	X(dev_get),
	X(dev_ioctl),
	X(dev_queue_xmit),
	X(dev_base),
	X(dev_close),
	X(dev_mc_add),
	X(arp_find),
	X(n_tty_ioctl),
	X(tty_register_ldisc),
	X(kill_fasync),
	X(arp_query),
#endif  /* CONFIG_INET */

#ifdef CONFIG_NETLINK
	X(netlink_attach),
	X(netlink_detach),
	X(netlink_donothing),
	X(netlink_post),
#endif /* CONFIG_NETLINK */
	
#include <linux/symtab_end.h>
};

void export_net_symbols(void)
{
	register_symtab(&net_syms);
}
