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
#include <linux/fddidevice.h>
#include <linux/trdevice.h>
#include <linux/ioport.h>
#include <net/neighbour.h>
#include <net/snmp.h>
#include <net/dst.h>
#include <net/checksum.h>
#include <linux/etherdevice.h>
#include <net/pkt_sched.h>

#ifdef CONFIG_BRIDGE
#include <net/br.h>
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
#include <net/scm.h>
#include <net/inet_common.h>
#include <net/pkt_sched.h>
#include <linux/inet.h>
#include <linux/mroute.h>
#include <linux/igmp.h>

extern struct net_proto_family inet_family_ops;

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/dst.h>
#include <net/transp_v6.h>

extern int tcp_tw_death_row_slot;
#endif

#endif

#include <linux/rtnetlink.h>

#include <net/scm.h>

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		|| \
	defined(CONFIG_ES3210)	||	defined(CONFIG_ULTRA32)		|| \
	defined(CONFIG_LNE390)	||	defined(CONFIG_NE3210)		|| \
	defined(CONFIG_NE2K_PCI) ||	defined(CONFIG_APNE)		|| \
	defined(CONFIG_DAYNAPORT)
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
#include <net/dst.h>
#include <net/checksum.h>
#include <linux/etherdevice.h>
#include <net/pkt_sched.h>
#endif

#ifdef CONFIG_SYSCTL
extern int sysctl_max_syn_backlog;
#endif

EXPORT_SYMBOL(dev_lockct);

/* Skbuff symbols. */
EXPORT_SYMBOL(skb_over_panic);
EXPORT_SYMBOL(skb_under_panic);

/* Socket layer registration */
EXPORT_SYMBOL(sock_register);
EXPORT_SYMBOL(sock_unregister);

/* Socket layer support routines */
EXPORT_SYMBOL(memcpy_fromiovec);
EXPORT_SYMBOL(memcpy_tokerneliovec);
EXPORT_SYMBOL(sock_create);
EXPORT_SYMBOL(sock_alloc);
EXPORT_SYMBOL(sock_release);
EXPORT_SYMBOL(sock_setsockopt);
EXPORT_SYMBOL(sock_getsockopt);
EXPORT_SYMBOL(sock_sendmsg);
EXPORT_SYMBOL(sock_recvmsg);
EXPORT_SYMBOL(sk_alloc);
EXPORT_SYMBOL(sk_free);
EXPORT_SYMBOL(sock_wake_async);
EXPORT_SYMBOL(sock_alloc_send_skb);
EXPORT_SYMBOL(sock_init_data);
EXPORT_SYMBOL(sock_no_dup);
EXPORT_SYMBOL(sock_no_release);
EXPORT_SYMBOL(sock_no_bind);
EXPORT_SYMBOL(sock_no_connect);
EXPORT_SYMBOL(sock_no_socketpair);
EXPORT_SYMBOL(sock_no_accept);
EXPORT_SYMBOL(sock_no_getname);
EXPORT_SYMBOL(sock_no_poll);
EXPORT_SYMBOL(sock_no_ioctl);
EXPORT_SYMBOL(sock_no_listen);
EXPORT_SYMBOL(sock_no_shutdown);
EXPORT_SYMBOL(sock_no_getsockopt);
EXPORT_SYMBOL(sock_no_setsockopt);
EXPORT_SYMBOL(sock_no_fcntl);
EXPORT_SYMBOL(sock_no_sendmsg);
EXPORT_SYMBOL(sock_no_recvmsg);
EXPORT_SYMBOL(sock_rfree);
EXPORT_SYMBOL(sock_wfree);
EXPORT_SYMBOL(sock_wmalloc);
EXPORT_SYMBOL(sock_rmalloc);
EXPORT_SYMBOL(sock_rspace);
EXPORT_SYMBOL(skb_recv_datagram);
EXPORT_SYMBOL(skb_free_datagram);
EXPORT_SYMBOL(skb_copy_datagram);
EXPORT_SYMBOL(skb_copy_datagram_iovec);
EXPORT_SYMBOL(skb_realloc_headroom);
EXPORT_SYMBOL(datagram_poll);
EXPORT_SYMBOL(put_cmsg);
EXPORT_SYMBOL(net_families);
EXPORT_SYMBOL(sock_kmalloc);
EXPORT_SYMBOL(sock_kfree_s);
EXPORT_SYMBOL(skb_queue_lock);

#ifdef CONFIG_FILTER
EXPORT_SYMBOL(sk_run_filter);
#endif

EXPORT_SYMBOL(neigh_table_init);
EXPORT_SYMBOL(neigh_table_clear);
EXPORT_SYMBOL(__neigh_lookup);
EXPORT_SYMBOL(neigh_resolve_output);
EXPORT_SYMBOL(neigh_connected_output);
EXPORT_SYMBOL(neigh_update);
EXPORT_SYMBOL(__neigh_event_send);
EXPORT_SYMBOL(neigh_event_ns);
EXPORT_SYMBOL(neigh_ifdown);
#ifdef CONFIG_ARPD
EXPORT_SYMBOL(neigh_app_ns);
#endif
#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(neigh_sysctl_register);
#endif
EXPORT_SYMBOL(pneigh_lookup);
EXPORT_SYMBOL(pneigh_enqueue);
EXPORT_SYMBOL(neigh_destroy);
EXPORT_SYMBOL(neigh_parms_alloc);
EXPORT_SYMBOL(neigh_parms_release);
EXPORT_SYMBOL(neigh_rand_reach_time);

/*	dst_entry	*/
EXPORT_SYMBOL(dst_alloc);
EXPORT_SYMBOL(__dst_free);
EXPORT_SYMBOL(dst_total);
EXPORT_SYMBOL(dst_destroy);

/*	misc. support routines */
EXPORT_SYMBOL(net_ratelimit);
EXPORT_SYMBOL(net_random);
EXPORT_SYMBOL(net_srandom);

/* Needed by smbfs.o */
EXPORT_SYMBOL(__scm_destroy);
EXPORT_SYMBOL(__scm_send);

/* Needed by unix.o */
EXPORT_SYMBOL(scm_fp_dup);
EXPORT_SYMBOL(max_files);
EXPORT_SYMBOL(do_mknod);
EXPORT_SYMBOL(memcpy_toiovec);
EXPORT_SYMBOL(csum_partial);

#ifdef CONFIG_IPX_MODULE
EXPORT_SYMBOL(make_8023_client);
EXPORT_SYMBOL(destroy_8023_client);
EXPORT_SYMBOL(make_EII_client);
EXPORT_SYMBOL(destroy_EII_client);
#endif

EXPORT_SYMBOL(sklist_destroy_socket);
EXPORT_SYMBOL(sklist_insert_socket);

EXPORT_SYMBOL(scm_detach_fds);

#ifdef CONFIG_BRIDGE 
EXPORT_SYMBOL(br_ioctl);
#endif

#ifdef CONFIG_INET
/* Internet layer registration */
EXPORT_SYMBOL(inet_add_protocol);
EXPORT_SYMBOL(inet_del_protocol);
EXPORT_SYMBOL(rarp_ioctl_hook);
EXPORT_SYMBOL(init_etherdev);
EXPORT_SYMBOL(ip_route_output);
EXPORT_SYMBOL(icmp_send);
EXPORT_SYMBOL(ip_options_compile);
EXPORT_SYMBOL(arp_send);
EXPORT_SYMBOL(arp_broken_ops);
EXPORT_SYMBOL(ip_id_count);
EXPORT_SYMBOL(ip_send_check);
EXPORT_SYMBOL(ip_fragment);
EXPORT_SYMBOL(inet_family_ops);
EXPORT_SYMBOL(in_aton);
EXPORT_SYMBOL(ip_mc_inc_group);
EXPORT_SYMBOL(ip_mc_dec_group);
EXPORT_SYMBOL(__ip_finish_output);
EXPORT_SYMBOL(inet_dgram_ops);
EXPORT_SYMBOL(ip_cmsg_recv);
EXPORT_SYMBOL(__release_sock);

/* needed for ip_gre -cw */
EXPORT_SYMBOL(ip_statistics);

#ifdef CONFIG_DLCI_MODULE
extern int (*dlci_ioctl_hook)(unsigned int, void *);
EXPORT_SYMBOL(dlci_ioctl_hook);
#endif


#ifdef CONFIG_IPV6
EXPORT_SYMBOL(ipv6_addr_type);
EXPORT_SYMBOL(icmpv6_send);
#endif
#ifdef CONFIG_IPV6_MODULE
/* inet functions common to v4 and v6 */
EXPORT_SYMBOL(inet_stream_ops);
EXPORT_SYMBOL(inet_release);
EXPORT_SYMBOL(inet_stream_connect);
EXPORT_SYMBOL(inet_dgram_connect);
EXPORT_SYMBOL(inet_accept);
EXPORT_SYMBOL(inet_poll);
EXPORT_SYMBOL(inet_listen);
EXPORT_SYMBOL(inet_shutdown);
EXPORT_SYMBOL(inet_setsockopt);
EXPORT_SYMBOL(inet_getsockopt);
EXPORT_SYMBOL(inet_sendmsg);
EXPORT_SYMBOL(inet_recvmsg);

/* Socket demultiplexing. */
EXPORT_SYMBOL(tcp_good_socknum);
EXPORT_SYMBOL(tcp_established_hash);
EXPORT_SYMBOL(tcp_listening_hash);
EXPORT_SYMBOL(tcp_bound_hash);
EXPORT_SYMBOL(udp_good_socknum);
EXPORT_SYMBOL(udp_hash);

EXPORT_SYMBOL(destroy_sock);
EXPORT_SYMBOL(ip_queue_xmit);
EXPORT_SYMBOL(memcpy_fromiovecend);
EXPORT_SYMBOL(csum_partial_copy_fromiovecend);
EXPORT_SYMBOL(net_timer);
/* UDP/TCP exported functions for TCPv6 */
EXPORT_SYMBOL(udp_ioctl);
EXPORT_SYMBOL(udp_connect);
EXPORT_SYMBOL(udp_sendmsg);
EXPORT_SYMBOL(tcp_close);
EXPORT_SYMBOL(tcp_accept);
EXPORT_SYMBOL(tcp_write_wakeup);
EXPORT_SYMBOL(tcp_read_wakeup);
EXPORT_SYMBOL(tcp_write_space);
EXPORT_SYMBOL(tcp_poll);
EXPORT_SYMBOL(tcp_ioctl);
EXPORT_SYMBOL(tcp_shutdown);
EXPORT_SYMBOL(tcp_setsockopt);
EXPORT_SYMBOL(tcp_getsockopt);
EXPORT_SYMBOL(tcp_recvmsg);
EXPORT_SYMBOL(tcp_send_synack);
EXPORT_SYMBOL(tcp_check_req);
EXPORT_SYMBOL(tcp_reset_xmit_timer);
EXPORT_SYMBOL(tcp_parse_options);
EXPORT_SYMBOL(tcp_rcv_established);
EXPORT_SYMBOL(tcp_init_xmit_timers);
EXPORT_SYMBOL(tcp_clear_xmit_timers);
EXPORT_SYMBOL(tcp_slt_array);
EXPORT_SYMBOL(__tcp_inc_slow_timer);
EXPORT_SYMBOL(tcp_statistics);
EXPORT_SYMBOL(tcp_rcv_state_process);
EXPORT_SYMBOL(tcp_timewait_state_process);
EXPORT_SYMBOL(tcp_do_sendmsg);
EXPORT_SYMBOL(tcp_v4_rebuild_header);
EXPORT_SYMBOL(tcp_v4_send_check);
EXPORT_SYMBOL(tcp_v4_conn_request);
EXPORT_SYMBOL(tcp_create_openreq_child);
EXPORT_SYMBOL(tcp_bucket_create);
EXPORT_SYMBOL(tcp_bucket_unlock);
EXPORT_SYMBOL(tcp_v4_syn_recv_sock);
EXPORT_SYMBOL(tcp_v4_do_rcv);
EXPORT_SYMBOL(tcp_v4_connect);
EXPORT_SYMBOL(inet_addr_type);
EXPORT_SYMBOL(net_reset_timer);
EXPORT_SYMBOL(net_delete_timer);
EXPORT_SYMBOL(udp_prot);
EXPORT_SYMBOL(tcp_prot);
EXPORT_SYMBOL(tcp_openreq_cachep);
EXPORT_SYMBOL(ipv4_specific);
EXPORT_SYMBOL(tcp_simple_retransmit);
EXPORT_SYMBOL(tcp_transmit_skb);
EXPORT_SYMBOL(tcp_connect);
EXPORT_SYMBOL(tcp_make_synack);
EXPORT_SYMBOL(tcp_tw_death_row_slot);
EXPORT_SYMBOL(tcp_sync_mss);
EXPORT_SYMBOL(net_statistics); 

EXPORT_SYMBOL(xrlim_allow);

EXPORT_SYMBOL(tcp_write_xmit);
EXPORT_SYMBOL(dev_loopback_xmit);
EXPORT_SYMBOL(tcp_regs);

#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(sysctl_max_syn_backlog);
#endif
#endif

#ifdef CONFIG_NETLINK
EXPORT_SYMBOL(netlink_set_err);
EXPORT_SYMBOL(netlink_broadcast);
EXPORT_SYMBOL(netlink_unicast);
EXPORT_SYMBOL(netlink_kernel_create);
EXPORT_SYMBOL(netlink_dump_start);
EXPORT_SYMBOL(netlink_ack);
#if defined(CONFIG_NETLINK_DEV) || defined(CONFIG_NETLINK_DEV_MODULE)
EXPORT_SYMBOL(netlink_attach);
EXPORT_SYMBOL(netlink_detach);
EXPORT_SYMBOL(netlink_post);
#endif
#endif

#ifdef CONFIG_RTNETLINK
EXPORT_SYMBOL(rtattr_parse);
EXPORT_SYMBOL(rtnetlink_links);
EXPORT_SYMBOL(__rta_fill);
EXPORT_SYMBOL(rtnetlink_dump_ifinfo);
EXPORT_SYMBOL(rtnl_wlockct);
EXPORT_SYMBOL(rtnl);
EXPORT_SYMBOL(neigh_delete);
EXPORT_SYMBOL(neigh_add);
EXPORT_SYMBOL(neigh_dump_info);
#endif

EXPORT_SYMBOL(dev_set_allmulti);
EXPORT_SYMBOL(dev_set_promiscuity);
EXPORT_SYMBOL(sklist_remove_socket);
EXPORT_SYMBOL(rtnl_wait);
EXPORT_SYMBOL(rtnl_rlockct);
EXPORT_SYMBOL(rtnl_lock);
EXPORT_SYMBOL(rtnl_unlock);

                  
/* Used by at least ipip.c.  */
EXPORT_SYMBOL(ipv4_config);
EXPORT_SYMBOL(dev_open);

EXPORT_SYMBOL(ip_rcv);
EXPORT_SYMBOL(arp_rcv);
EXPORT_SYMBOL(arp_tbl);
EXPORT_SYMBOL(arp_find);

#endif  /* CONFIG_INET */

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		|| \
	defined(CONFIG_ES3210)	||	defined(CONFIG_ULTRA32)		|| \
	defined(CONFIG_LNE390)	||	defined(CONFIG_NE3210)		|| \
	defined(CONFIG_NE2K_PCI) ||	defined(CONFIG_APNE)		|| \
	defined(CONFIG_DAYNAPORT)
/* If 8390 NIC support is built in, we will need these. */
EXPORT_SYMBOL(ei_open);
EXPORT_SYMBOL(ei_close);
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
EXPORT_SYMBOL(tr_freedev);
#endif

/* Device callback registration */
EXPORT_SYMBOL(register_netdevice_notifier);
EXPORT_SYMBOL(unregister_netdevice_notifier);

/* support for loadable net drivers */
#ifdef CONFIG_NET
EXPORT_SYMBOL(loopback_dev);
EXPORT_SYMBOL(register_netdevice);
EXPORT_SYMBOL(unregister_netdevice);
EXPORT_SYMBOL(register_netdev);
EXPORT_SYMBOL(unregister_netdev);
EXPORT_SYMBOL(ether_setup);
EXPORT_SYMBOL(dev_new_index);
EXPORT_SYMBOL(dev_get_by_index);
EXPORT_SYMBOL(eth_type_trans);
#ifdef CONFIG_FDDI
EXPORT_SYMBOL(fddi_type_trans);
#endif /* CONFIG_FDDI */
EXPORT_SYMBOL(eth_copy_and_sum);
EXPORT_SYMBOL(alloc_skb);
EXPORT_SYMBOL(__kfree_skb);
EXPORT_SYMBOL(skb_clone);
EXPORT_SYMBOL(skb_copy);
EXPORT_SYMBOL(netif_rx);
EXPORT_SYMBOL(dev_add_pack);
EXPORT_SYMBOL(dev_remove_pack);
EXPORT_SYMBOL(dev_get);
EXPORT_SYMBOL(dev_alloc);
EXPORT_SYMBOL(dev_alloc_name);
EXPORT_SYMBOL(dev_ioctl);
EXPORT_SYMBOL(dev_queue_xmit);
EXPORT_SYMBOL(netdev_dropping);
#ifdef CONFIG_NET_FASTROUTE
EXPORT_SYMBOL(dev_fastroute_stat);
#endif
#ifdef CONFIG_NET_HW_FLOWCONTROL
EXPORT_SYMBOL(netdev_register_fc);
EXPORT_SYMBOL(netdev_unregister_fc);
EXPORT_SYMBOL(netdev_fc_xoff);
#endif
EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_close);
EXPORT_SYMBOL(dev_mc_add);
EXPORT_SYMBOL(dev_mc_delete);
EXPORT_SYMBOL(dev_mc_upload);
EXPORT_SYMBOL(n_tty_ioctl);
EXPORT_SYMBOL(tty_register_ldisc);
EXPORT_SYMBOL(kill_fasync);

EXPORT_SYMBOL(if_port_text);

#if defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE) 
#include<linux/if_ltalk.h>
EXPORT_SYMBOL(ltalk_setup);
#endif


/* Packet scheduler modules want these. */
EXPORT_SYMBOL(qdisc_destroy);
EXPORT_SYMBOL(qdisc_reset);
EXPORT_SYMBOL(qdisc_restart);
EXPORT_SYMBOL(qdisc_head);
EXPORT_SYMBOL(qdisc_create_dflt);
EXPORT_SYMBOL(noop_qdisc);
#ifdef CONFIG_NET_SCHED
EXPORT_SYMBOL(pfifo_qdisc_ops);
EXPORT_SYMBOL(register_qdisc);
EXPORT_SYMBOL(unregister_qdisc);
EXPORT_SYMBOL(qdisc_get_rtab);
EXPORT_SYMBOL(qdisc_put_rtab);
#ifdef CONFIG_NET_ESTIMATOR
EXPORT_SYMBOL(qdisc_new_estimator);
EXPORT_SYMBOL(qdisc_kill_estimator);
#endif
#ifdef CONFIG_NET_CLS_POLICE
EXPORT_SYMBOL(tcf_police);
EXPORT_SYMBOL(tcf_police_locate);
EXPORT_SYMBOL(tcf_police_destroy);
#ifdef CONFIG_RTNETLINK
EXPORT_SYMBOL(tcf_police_dump);
#endif
#endif
#endif
#ifdef CONFIG_NET_CLS
EXPORT_SYMBOL(register_tcf_proto_ops);
EXPORT_SYMBOL(unregister_tcf_proto_ops);
#endif

EXPORT_SYMBOL(register_gifconf);

#endif  /* CONFIG_NET */
