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

#ifdef CONFIG_ATALK_MODULE
#include <net/sock.h>
#endif

extern char *skb_push_errstr;
extern char *skb_put_errstr;

/* Skbuff symbols. */
EXPORT_SYMBOL(skb_push_errstr);
EXPORT_SYMBOL(skb_put_errstr);

/* Socket layer registration */
EXPORT_SYMBOL(sock_register);
EXPORT_SYMBOL(sock_unregister);

/* Socket layer support routines */
EXPORT_SYMBOL(memcpy_fromiovec);
EXPORT_SYMBOL(sock_setsockopt);
EXPORT_SYMBOL(sock_getsockopt);
EXPORT_SYMBOL(sock_sendmsg);
EXPORT_SYMBOL(sock_recvmsg);
EXPORT_SYMBOL(sk_alloc);
EXPORT_SYMBOL(sk_free);
EXPORT_SYMBOL(sock_wake_async);
EXPORT_SYMBOL(sock_alloc_send_skb);
EXPORT_SYMBOL(sock_no_fcntl);
EXPORT_SYMBOL(sock_rfree);
EXPORT_SYMBOL(sock_wfree);
EXPORT_SYMBOL(skb_recv_datagram);
EXPORT_SYMBOL(skb_free_datagram);
EXPORT_SYMBOL(skb_copy_datagram);
EXPORT_SYMBOL(skb_copy_datagram_iovec);
EXPORT_SYMBOL(skb_realloc_headroom);
EXPORT_SYMBOL(datagram_poll);

/* Needed by smbfs.o */
EXPORT_SYMBOL(__scm_destroy);
EXPORT_SYMBOL(__scm_send);

#ifdef CONFIG_IPX_MODULE
EXPORT_SYMBOL(make_8023_client);
EXPORT_SYMBOL(destroy_8023_client);
EXPORT_SYMBOL(make_EII_client);
EXPORT_SYMBOL(destroy_EII_client);
#endif

#ifdef CONFIG_ATALK_MODULE
EXPORT_SYMBOL(sklist_destroy_socket);
EXPORT_SYMBOL(sklist_insert_socket);
#endif

#ifdef CONFIG_INET
/* Internet layer registration */
EXPORT_SYMBOL(get_new_socknum);
EXPORT_SYMBOL(inet_add_protocol);
EXPORT_SYMBOL(inet_del_protocol);
EXPORT_SYMBOL(rarp_ioctl_hook);
EXPORT_SYMBOL(init_etherdev);
EXPORT_SYMBOL(ip_route_output);
EXPORT_SYMBOL(icmp_send);
EXPORT_SYMBOL(ip_options_compile);
EXPORT_SYMBOL(ip_rt_put);
EXPORT_SYMBOL(arp_send);
EXPORT_SYMBOL(ip_id_count);
EXPORT_SYMBOL(ip_send_check);
EXPORT_SYMBOL(ip_fragment);
EXPORT_SYMBOL(ip_dev_find_tunnel);
EXPORT_SYMBOL(inet_family_ops);

#ifdef CONFIG_IPV6_MODULE
/* inet functions common to v4 and v6 */
EXPORT_SYMBOL(inet_stream_ops);
EXPORT_SYMBOL(inet_dgram_ops);
EXPORT_SYMBOL(inet_remove_sock);
EXPORT_SYMBOL(inet_release);
EXPORT_SYMBOL(inet_stream_connect);
EXPORT_SYMBOL(inet_dgram_connect);
EXPORT_SYMBOL(inet_accept);
EXPORT_SYMBOL(inet_poll);
EXPORT_SYMBOL(inet_listen);
EXPORT_SYMBOL(inet_shutdown);
EXPORT_SYMBOL(inet_setsockopt);
EXPORT_SYMBOL(inet_getsockopt);
EXPORT_SYMBOL(inet_fcntl);
EXPORT_SYMBOL(inet_sendmsg);
EXPORT_SYMBOL(inet_recvmsg);
EXPORT_SYMBOL(tcp_sock_array);
EXPORT_SYMBOL(udp_sock_array);
EXPORT_SYMBOL(destroy_sock);
EXPORT_SYMBOL(ip_queue_xmit);
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(dev_lockct);
EXPORT_SYMBOL(ndisc_eth_hook);
EXPORT_SYMBOL(memcpy_fromiovecend);
EXPORT_SYMBOL(csum_partial_copy);
EXPORT_SYMBOL(csum_partial_copy_fromiovecend);
EXPORT_SYMBOL(__release_sock);
EXPORT_SYMBOL(net_timer);
EXPORT_SYMBOL(inet_put_sock);
/* UDP/TCP exported functions for TCPv6 */
EXPORT_SYMBOL(udp_ioctl);
EXPORT_SYMBOL(udp_connect);
EXPORT_SYMBOL(udp_sendmsg);
EXPORT_SYMBOL(tcp_cache_zap);
EXPORT_SYMBOL(tcp_close);
EXPORT_SYMBOL(tcp_accept);
EXPORT_SYMBOL(tcp_write_wakeup);
EXPORT_SYMBOL(tcp_read_wakeup);
EXPORT_SYMBOL(tcp_poll);
EXPORT_SYMBOL(tcp_ioctl);
EXPORT_SYMBOL(tcp_shutdown);
EXPORT_SYMBOL(tcp_setsockopt);
EXPORT_SYMBOL(tcp_getsockopt);
EXPORT_SYMBOL(tcp_recvmsg);
EXPORT_SYMBOL(tcp_send_synack);
EXPORT_SYMBOL(sock_wmalloc);
EXPORT_SYMBOL(tcp_reset_xmit_timer);
EXPORT_SYMBOL(tcp_parse_options);
EXPORT_SYMBOL(tcp_rcv_established);
EXPORT_SYMBOL(tcp_init_xmit_timers);
EXPORT_SYMBOL(tcp_clear_xmit_timers);
EXPORT_SYMBOL(tcp_slt_array);
EXPORT_SYMBOL(__tcp_inc_slow_timer);
EXPORT_SYMBOL(tcp_statistics);
EXPORT_SYMBOL(tcp_rcv_state_process);
EXPORT_SYMBOL(tcp_do_sendmsg);
EXPORT_SYMBOL(tcp_v4_build_header);
EXPORT_SYMBOL(tcp_v4_rebuild_header);
EXPORT_SYMBOL(tcp_v4_send_check);
EXPORT_SYMBOL(tcp_v4_conn_request);
EXPORT_SYMBOL(tcp_v4_syn_recv_sock);
EXPORT_SYMBOL(tcp_v4_backlog_rcv);
EXPORT_SYMBOL(tcp_v4_connect);
EXPORT_SYMBOL(__ip_chk_addr);
EXPORT_SYMBOL(net_reset_timer);
EXPORT_SYMBOL(net_delete_timer);
EXPORT_SYMBOL(udp_prot);
EXPORT_SYMBOL(tcp_prot);
EXPORT_SYMBOL(ipv4_specific);
#endif

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		|| \
	defined(CONFIG_ES3210)
/* If 8390 NIC support is built in, we will need these. */
EXPORT_SYMBOL(ei_open);
EXPORT_SYMBOL(ei_close);
EXPORT_SYMBOL(ei_debug);
EXPORT_SYMBOL(ei_interrupt);
EXPORT_SYMBOL(ethdev_init);
EXPORT_SYMBOL(NS8390_init);
#endif

#ifdef CONFIG_TR
EXPORT_SYMBOL(tr_setup);
EXPORT_SYMBOL(tr_type_trans);
EXPORT_SYMBOL(register_trdev);
EXPORT_SYMBOL(unregister_trdev);
EXPORT_SYMBOL(init_trdev);
#endif
                          
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif

#endif  /* CONFIG_INET */

/* Device callback registration */
EXPORT_SYMBOL(register_netdevice_notifier);
EXPORT_SYMBOL(unregister_netdevice_notifier);

#ifdef CONFIG_NET_ALIAS
EXPORT_SYMBOL(register_net_alias_type);
EXPORT_SYMBOL(unregister_net_alias_type);
#endif

/* support for loadable net drivers */
#ifdef CONFIG_INET
EXPORT_SYMBOL(register_netdev);
EXPORT_SYMBOL(unregister_netdev);
EXPORT_SYMBOL(ether_setup);
EXPORT_SYMBOL(eth_type_trans);
EXPORT_SYMBOL(eth_copy_and_sum);
EXPORT_SYMBOL(alloc_skb);
EXPORT_SYMBOL(__kfree_skb);
EXPORT_SYMBOL(skb_clone);
EXPORT_SYMBOL(skb_copy);
EXPORT_SYMBOL(dev_alloc_skb);
EXPORT_SYMBOL(netif_rx);
EXPORT_SYMBOL(dev_tint);
EXPORT_SYMBOL(irq2dev_map);
EXPORT_SYMBOL(dev_add_pack);
EXPORT_SYMBOL(dev_remove_pack);
EXPORT_SYMBOL(dev_get);
EXPORT_SYMBOL(dev_ioctl);
EXPORT_SYMBOL(dev_queue_xmit);
EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_close);
EXPORT_SYMBOL(dev_mc_add);
EXPORT_SYMBOL(arp_find);
EXPORT_SYMBOL(n_tty_ioctl);
EXPORT_SYMBOL(tty_register_ldisc);
EXPORT_SYMBOL(kill_fasync);
EXPORT_SYMBOL(ip_rcv);
EXPORT_SYMBOL(arp_rcv);
#endif  /* CONFIG_INET */

#ifdef CONFIG_NETLINK
EXPORT_SYMBOL(netlink_attach);
EXPORT_SYMBOL(netlink_detach);
EXPORT_SYMBOL(netlink_donothing);
EXPORT_SYMBOL(netlink_post);
#endif /* CONFIG_NETLINK */
