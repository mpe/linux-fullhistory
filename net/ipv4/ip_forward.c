/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP forwarding functionality.
 *		
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip_input.c for 
 *					history.
 *		Dave Gregorich	:	NULL ip_rt_put fix for multicast 
 *					routing.
 *		Jos Vos		:	Add call_out_firewall before sending,
 *					use output device for accounting.
 *		Jos Vos		:	Call forward firewall after routing
 *					(always use output device).
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#include <net/checksum.h>
#include <linux/route.h>
#include <net/route.h>

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check the packet against our socket administration to see
 *	if it is related to a connection on our system.
 *	Needed for transparent proxying.
 */

int ip_chksock(struct sk_buff *skb)
{
	switch (skb->nh.iph->protocol) {
	case IPPROTO_ICMP:
		return icmp_chkaddr(skb);
	case IPPROTO_TCP:
		return tcp_chkaddr(skb);
	case IPPROTO_UDP:
		return udp_chkaddr(skb);
	default:
		return 0;
	}
}
#endif


int ip_forward(struct sk_buff *skb)
{
	struct device *dev2;	/* Output device */
	struct iphdr *iph;	/* Our header */
	struct rtable *rt;	/* Route we use */
	struct ip_options * opt	= &(IPCB(skb)->opt);
	unsigned short mtu;
#if defined(CONFIG_FIREWALL) || defined(CONFIG_IP_MASQUERADE)
	int fw_res = 0;
#endif

	if (skb->pkt_type != PACKET_HOST) {
		kfree_skb(skb,FREE_WRITE);
		return 0;
	}
	
	/*
	 *	According to the RFC, we must first decrease the TTL field. If
	 *	that reaches zero, we must reply an ICMP control message telling
	 *	that the packet's lifetime expired.
	 */

	iph = skb->nh.iph;
	rt = (struct rtable*)skb->dst;

#ifdef CONFIG_TRANSPARENT_PROXY
	if (ip_chk_sock(skb))
		return ip_local_deliver(skb);
#endif

	if (ip_decrease_ttl(iph) <= 0) {
		/* Tell the sender its packet died... */
		icmp_send(skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, 0);
		kfree_skb(skb, FREE_WRITE);
		return -1;
	}

	if (opt->is_strictroute && (rt->rt_flags&RTF_GATEWAY)) {
		/*
		 *	Strict routing permits no gatewaying
		 */
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_SR_FAILED, 0);
		kfree_skb(skb, FREE_WRITE);
		return -1;
	}


	/*
	 *	Having picked a route we can now send the frame out
	 *	after asking the firewall permission to do so.
	 */

	skb->priority = rt->u.dst.priority;
	dev2 = rt->u.dst.dev;
	mtu = dev2->mtu;

#ifdef CONFIG_NET_SECURITY
	call_fw_firewall(PF_SECURITY, dev2, NULL, &mtu, NULL);
#endif	
	
	/*
	 *	In IP you never have to forward a frame on the interface that it 
	 *	arrived upon. We now generate an ICMP HOST REDIRECT giving the route
	 *	we calculated.
	 */
	if (rt->rt_flags&RTCF_DOREDIRECT && !opt->srr)
		ip_rt_send_redirect(skb);
	
	/*
	 * We now may allocate a new buffer, and copy the datagram into it.
	 * If the indicated interface is up and running, kick it.
	 */

	if (dev2->flags & IFF_UP) {
		if (skb->len > mtu && (ntohs(iph->frag_off) & IP_DF)) {
			ip_statistics.IpFragFails++;
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
			kfree_skb(skb, FREE_WRITE);
			return -1;
		}

		if (rt->rt_flags&RTCF_NAT) {
			if (ip_do_nat(skb)) {
				kfree_skb(skb, FREE_WRITE);
				return -1;
			}
		}

#ifdef CONFIG_IP_MASQUERADE
		if(!(IPCB(skb)->flags&IPSKB_MASQUERADED)) {

			if (rt->rt_flags&RTCF_VALVE) {
				icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PKT_FILTERED, 0);
				kfree_skb(skb, FREE_READ);
				return -1;
			}

			/* 
			 *	Check that any ICMP packets are not for a 
			 *	masqueraded connection.  If so rewrite them
			 *	and skip the firewall checks
			 */
			if (iph->protocol == IPPROTO_ICMP) {
				if ((fw_res = ip_fw_masq_icmp(&skb, dev2)) < 0) {
					kfree_skb(skb, FREE_READ);
					return -1;
				}

				if (fw_res)
					/* ICMP matched - skip firewall */
					goto skip_call_fw_firewall;
			}
			if (rt->rt_flags&RTCF_MASQ)
				goto skip_call_fw_firewall;
#endif
#ifdef CONFIG_FIREWALL
		fw_res=call_fw_firewall(PF_INET, dev2, iph, NULL, &skb);
		switch (fw_res) {
		case FW_ACCEPT:
		case FW_MASQUERADE:
			break;
		case FW_REJECT:
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
			/* fall thru */
		default:
			kfree_skb(skb, FREE_READ);
			return -1;
		}
#endif

#ifdef CONFIG_IP_MASQUERADE
		}

skip_call_fw_firewall:
		/*
		 * If this fragment needs masquerading, make it so...
		 * (Don't masquerade de-masqueraded fragments)
		 */
		if (!(IPCB(skb)->flags&IPSKB_MASQUERADED) &&
		    (fw_res==FW_MASQUERADE || rt->rt_flags&RTCF_MASQ)) {
			if (ip_fw_masquerade(&skb, dev2) < 0) {
				kfree_skb(skb, FREE_READ);
				return -1;
			}
		}
#endif

		if (skb_headroom(skb) < dev2->hard_header_len || skb_cloned(skb)) {
			struct sk_buff *skb2;
			skb2 = skb_realloc_headroom(skb, (dev2->hard_header_len + 15)&~15);
			kfree_skb(skb, FREE_WRITE);

			if (skb2 == NULL) {
				NETDEBUG(printk(KERN_ERR "\nIP: No memory available for IP forward\n"));
				return -1;
			}
			skb = skb2;
			iph = skb2->nh.iph;
		}

#ifdef CONFIG_FIREWALL
		if ((fw_res = call_out_firewall(PF_INET, dev2, iph, NULL,&skb)) < FW_ACCEPT) {
			/* FW_ACCEPT and FW_MASQUERADE are treated equal:
			   masquerading is only supported via forward rules */
			if (fw_res == FW_REJECT)
				icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
			kfree_skb(skb,FREE_WRITE);
			return -1;
		}
#endif

		ip_statistics.IpForwDatagrams++;

		if (opt->optlen)
			ip_forward_options(skb);

		ip_send(skb);
	}
	return 0;
}
