/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/ioport.h>

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
#include <net/scm.h>
#include <net/inet_common.h>
#include <linux/net_alias.h>

extern struct net_proto_family inet_family_ops;

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/in6.h>
#include <net/ndisc.h>
#include <net/transp_v6.h>
#endif

#endif

#ifdef CONFIG_NETLINK
#include <net/netlink.h>
#endif

#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif

#include <net/scm.h>

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		|| \
	defined(CONFIG_ES3210)
#include "../drivers/net/8390.h"
#endif

extern int (*rarp_ioctl_hook)(int,void*);

#ifdef CONFIG_IPX_MODULE
extern struct datalink_proto   *make_EII_client(void);
extern struct datalink_proto   *make_8023_client(void);
extern void destroy_EII_client(struct datalink_proto *);
extern void destroy_8023_client(struct datalink_proto *);
#endif

extern char *skb_push_errstr;
extern char *skb_put_errstr;

static struct symbol_table net_syms = {
#include <linux/symtab_begin.h>

	/* Skbuff symbols. */
	X(skb_push_errstr),
	X(skb_put_errstr),

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

	/* ?? needed by smbfs.o */
	X(__scm_destroy),
	X(__scm_send),

#ifdef CONFIG_IPX_MODULE
	X(make_8023_client),
	X(destroy_8023_client),
	X(make_EII_client),
	X(destroy_EII_client),
#endif

#ifdef CONFIG_INET
	/* Internet layer registration */
	X(get_new_socknum),
	X(inet_add_protocol),
	X(inet_del_protocol),
	X(rarp_ioctl_hook),
	X(init_etherdev),
	X(ip_route_output),
	X(icmp_send),
	X(ip_options_compile),
	X(ip_rt_put),
	X(arp_send),
	X(ip_id_count),
	X(ip_send_check),
	X(inet_family_ops),

	X(__scm_send),
	X(__scm_destroy),

#ifdef CONFIG_IP_FORWARD
	X(ip_forward),
#endif

#ifdef CONFIG_IPV6_MODULE
	/* inet functions common to v4 and v6 */
	X(inet_stream_ops),
	X(inet_dgram_ops),
	X(inet_remove_sock),
	X(inet_release),
	X(inet_stream_connect),
	X(inet_dgram_connect),
	X(inet_accept),
	X(inet_select),
	X(inet_listen),
	X(inet_shutdown),
	X(inet_setsockopt),
	X(inet_getsockopt),
	X(inet_fcntl),
	X(inet_sendmsg),
	X(inet_recvmsg),
	X(tcp_sock_array),
	X(udp_sock_array),
	X(destroy_sock),
	X(ip_queue_xmit),
	X(csum_partial),
	X(skb_copy),
	X(dev_lockct),
	X(ndisc_eth_hook),
	X(memcpy_fromiovecend),
	X(csum_partial_copy),
	X(csum_partial_copy_fromiovecend),
	X(__release_sock),
	X(net_timer),
	X(inet_put_sock),
	/* UDP/TCP exported functions for TCPv6 */
	X(udp_ioctl),
	X(udp_connect),
	X(udp_sendmsg),
	X(tcp_cache_zap),
	X(tcp_close),
	X(tcp_accept),
	X(tcp_write_wakeup),
	X(tcp_read_wakeup),
	X(tcp_select),
	X(tcp_ioctl),
	X(tcp_shutdown),
	X(tcp_setsockopt),
	X(tcp_getsockopt),
	X(tcp_recvmsg),
	X(tcp_send_synack),
	X(sock_wmalloc),
	X(tcp_reset_xmit_timer),
	X(tcp_parse_options),
	X(tcp_rcv_established),
	X(tcp_init_xmit_timers),
	X(tcp_clear_xmit_timers),
	X(tcp_slt_array),
	X(__tcp_inc_slow_timer),
	X(tcp_statistics),
	X(tcp_rcv_state_process),
	X(tcp_do_sendmsg),
	X(tcp_v4_build_header),
	X(tcp_v4_rebuild_header),
	X(tcp_v4_send_check),
	X(tcp_v4_conn_request),
	X(tcp_v4_syn_recv_sock),
	X(tcp_v4_backlog_rcv),
	X(tcp_v4_connect),
	X(__ip_chk_addr),
	X(net_reset_timer),
	X(net_delete_timer),
	X(udp_prot),
	X(tcp_prot),
	X(ipv4_specific),
#endif

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		|| \
	defined(CONFIG_ES3210)
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
#ifdef CONFIG_INET
	X(register_netdev),
	X(unregister_netdev),
	X(ether_setup),
	X(eth_type_trans),
	X(eth_copy_and_sum),
	X(arp_query),
	X(alloc_skb),
	X(__kfree_skb),
	X(skb_clone),
	X(skb_copy),
	X(dev_alloc_skb),
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
	X(ip_rcv),
	X(arp_rcv),
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
